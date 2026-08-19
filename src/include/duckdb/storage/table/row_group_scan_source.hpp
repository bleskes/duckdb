//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/table/row_group_scan_source.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/enums/operator_result_type.hpp"
#include "duckdb/common/optional_ptr.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/storage/storage_index.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include "duckdb/storage/table/segment_tree.hpp"
#include "duckdb/transaction/transaction_data.hpp"

namespace duckdb {

class ClientContext;
class InterruptState;
class RowGroupCollection;
class RowGroupSegmentTree;
struct RowGroupOrderOptions;
struct RowGroupScanSourceInfo;
struct TableFunctionInitInput;
struct TableScanBindData;

//===--------------------------------------------------------------------===//
// Results
//===--------------------------------------------------------------------===//
//! The result of pulling a row group out of a RowGroupScanSource. We reuse AsyncResultType: HAVE_MORE_OUTPUT means a
//! row group was handed out, FINISHED means the scan is done, BLOCKED means the scan is parked (see Blocked())
struct RowGroupScanResult {
	AsyncResultType type = AsyncResultType::FINISHED;
	//! The row group to scan - only set for HAVE_MORE_OUTPUT
	optional_ptr<SegmentNode<RowGroup>> row_group;

	//! Hand out a row group to scan
	DUCKDB_API static RowGroupScanResult WithRowGroup(SegmentNode<RowGroup> &row_group);
	DUCKDB_API static RowGroupScanResult Finished();
	//! Park the scan. The source must have stashed the input's InterruptState and must resume the scan by calling
	//! InterruptState::Callback once it can hand out row groups again. Only allowed when the input carries an
	//! InterruptState (i.e. the scan can be suspended)
	DUCKDB_API static RowGroupScanResult Blocked();

	//! Whether the result is internally consistent: a row group is set exactly for HAVE_MORE_OUTPUT. Meant to be
	//! called under a D_ASSERT
	bool Verify() const {
		return (type == AsyncResultType::HAVE_MORE_OUTPUT) == static_cast<bool>(row_group);
	}
};

//! The result of assigning the next row group to a scan thread
struct RowGroupScanAssignment {
	AsyncResultType type = AsyncResultType::FINISHED;
	//! The number of rows in the assigned row group - only set for HAVE_MORE_OUTPUT
	idx_t rows = 0;

	bool HasRowGroup() const {
		return type == AsyncResultType::HAVE_MORE_OUTPUT;
	}
	bool IsBlocked() const {
		return type == AsyncResultType::BLOCKED;
	}

	DUCKDB_API static RowGroupScanAssignment RowGroupAssigned(idx_t rows);
	DUCKDB_API static RowGroupScanAssignment Finished();
	DUCKDB_API static RowGroupScanAssignment Blocked();
};

//===--------------------------------------------------------------------===//
// Row group scan source
//===--------------------------------------------------------------------===//
struct RowGroupScanSourceInitInput {
	RowGroupScanSourceInitInput(RowGroupCollection &collection, shared_ptr<RowGroupSegmentTree> row_groups)
	    : collection(collection), row_groups(std::move(row_groups)) {
	}

	//! The collection that is being scanned - the persistent storage of the table, or its transaction-local storage
	RowGroupCollection &collection;
	//! The segment tree holding the row groups of the collection. Sources that hand out row groups of the collection
	//! must keep this alive for as long as they hand out row groups
	shared_ptr<RowGroupSegmentTree> row_groups;
};

struct RowGroupScanSourceInput {
	RowGroupScanSourceInput(optional_ptr<ClientContext> context, optional_ptr<const InterruptState> interrupt_state)
	    : context(context), interrupt_state(interrupt_state) {
	}

	//! The context of the query that is scanning (not set for scans that run outside of a query)
	optional_ptr<ClientContext> context;
	//! The interrupt state of the scanning task, if the scan can be suspended. A source may only return BLOCKED when
	//! this is set. To avoid lost wakeups, register it with a StateWithBlockableTasks: under the state's lock, check
	//! whether the source is ready and, if not, call BlockTask(guard, *interrupt_state) and return BLOCKED; when the
	//! source becomes ready, call UnblockTasks(guard) under the same lock. When not set, the source must not block -
	//! it either hands out a row group, finishes, or waits inline
	optional_ptr<const InterruptState> interrupt_state;
};

//! A RowGroupScanSource hands out the row groups that a scan should read, one row group at a time.
//! The scan pulls from the source: the source is the only thing that decides which row group is scanned next, so
//! implementations are free to reorder, filter out, delay or replace what the underlying source hands out.
//!
//! Sources are chained: extensions wrap the source that DuckDB would otherwise have used (see
//! RowGroupScanAdapter), pull row groups out of that child source, and hand out whatever they want.
//!
//! A single source is shared by all threads of a scan, so that a source always sees - and controls - every row group
//! that reaches the scan.
class RowGroupScanSource {
public:
	DUCKDB_API virtual ~RowGroupScanSource();

public:
	//! Called exactly once, single-threaded, before any call to Next, by the scan state that creates the source.
	//! Implementations that wrap a child source must forward this call. This may do bounded work (e.g. reading row
	//! group statistics or computing an order), but it must not block: there is nothing to suspend during
	//! initialization. Work that has to wait on something belongs in Next, which can return BLOCKED
	virtual void Initialize(RowGroupScanSourceInitInput &input) = 0;

	//! Hand out the next row group to scan. This is called concurrently by all threads of the scan, so
	//! implementations must synchronize themselves. Do not perform long-running work here - return BLOCKED instead,
	//! and hand out row groups once the work has completed
	virtual RowGroupScanResult Next(RowGroupScanSourceInput &input) = 0;

	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}
	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

//! The built-in row group scan sources
struct RowGroupScanSources {
	//! Hands out all row groups of the collection in storage (i.e. row id) order - the default source
	DUCKDB_API static unique_ptr<RowGroupScanSource> Storage();
	//! Hands out row groups in the order dictated by the given order options, which are pushed into the scan by the
	//! optimizer for queries that can be answered by scanning row groups in a specific order (e.g. ORDER BY + LIMIT)
	DUCKDB_API static unique_ptr<RowGroupScanSource> Reordered(const RowGroupOrderOptions &options,
	                                                           TransactionData transaction);
};

//===--------------------------------------------------------------------===//
// Row group scan adapter
//===--------------------------------------------------------------------===//
//! Describes the scan that a row group scan source is created for. Adapters read what they need from the scan's
//! bind data and init input (e.g. the pushed-down filters and scan order); the source is told which collection it
//! operates on when it is initialized (RowGroupScanSourceInitInput::collection)
struct RowGroupScanSourceInfo {
	RowGroupScanSourceInfo(ClientContext &context, const TableScanBindData &bind_data, TableFunctionInitInput &input,
	                       bool parallel, vector<StorageIndex> column_ids)
	    : context(context), bind_data(bind_data), input(input), parallel(parallel), column_ids(std::move(column_ids)) {
	}

	//! The context of the query that is scanning
	ClientContext &context;
	//! The bind data of the scan - read e.g. the table or the pushed-down scan order from it
	const TableScanBindData &bind_data;
	//! The init input of the scan - holds the pushed-down filters, sample options and projection
	TableFunctionInitInput &input;
	//! Whether or not the source feeds a multi-threaded scan
	bool parallel;
	//! The columns that are being scanned
	vector<StorageIndex> column_ids;
};

//! An adapter allows an extension to sit between the threads of a table scan and the row groups that the scan reads.
//! Adapters are attached to a LogicalGet by an optimizer extension (see LogicalGet::AddRowGroupScanAdapter), which
//! runs after all built-in optimizers, so the full optimized plan is available when deciding whether to hook a scan.
//!
//! Wrap is called once per row group source of the scan: once for the persistent storage of the table, and once for
//! its transaction-local storage.
class RowGroupScanAdapter {
public:
	DUCKDB_API virtual ~RowGroupScanAdapter();

public:
	//! The name of this adapter, used in error messages
	virtual string Name() const = 0;
	//! Wrap the source that the scan pulls its row groups from. `child` is the source that DuckDB would otherwise
	//! have used - either the storage-order source, or the source implementing the pushed-down row group order.
	//! Return `child` unchanged to stay out of this scan
	virtual unique_ptr<RowGroupScanSource> Wrap(const RowGroupScanSourceInfo &info,
	                                            unique_ptr<RowGroupScanSource> child) = 0;
};

} // namespace duckdb
