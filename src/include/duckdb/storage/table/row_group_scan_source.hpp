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
class TableFilterSet;
struct RowGroupOrderOptions;

//! The result of pulling a row group out of a RowGroupScanSource
struct RowGroupScanResult {
	AsyncResultType type = AsyncResultType::FINISHED;
	//! The row group to scan. Set exactly for HAVE_MORE_OUTPUT; FINISHED and BLOCKED never carry a row group
	optional_ptr<SegmentNode<RowGroup>> row_group;

	//! Hand out a row group to scan (HAVE_MORE_OUTPUT). The scan is not done - Next is called again for the next one
	DUCKDB_API static RowGroupScanResult WithRowGroup(SegmentNode<RowGroup> &row_group);
	//! No more row groups to hand out - the scan of this collection is done. Carries no row group
	DUCKDB_API static RowGroupScanResult Finished();
	//! Park the scan. The source must have stashed the input's InterruptState and must resume the scan by calling
	//! InterruptState::Callback once it can hand out row groups again. Only allowed when the input carries an
	//! InterruptState (i.e. the scan can be suspended). Carries no row group
	DUCKDB_API static RowGroupScanResult Blocked();

	//! Whether the result is internally consistent: a row group is present exactly for HAVE_MORE_OUTPUT, and neither
	//! FINISHED nor BLOCKED carries one
	bool Verify() const {
		return (type == AsyncResultType::HAVE_MORE_OUTPUT) == static_cast<bool>(row_group);
	}
};

//===--------------------------------------------------------------------===//
// Row group scan source
//===--------------------------------------------------------------------===//

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
	//! Hand out the next row group to scan. The scan serializes calls to Next (it holds a lock while pulling), so
	//! implementations do not need to synchronize themselves. Do not perform long-running work here - return BLOCKED
	//! instead, and hand out row groups once the work has completed
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
	//! Hands out all row groups of the collection in storage (i.e. row id) order - the default source. resume_after,
	//! if set, is the row group to resume after: the source hands out its successors, not the whole collection (used by
	//! offset scans that are already positioned on a row group)
	DUCKDB_API static unique_ptr<RowGroupScanSource>
	Storage(shared_ptr<RowGroupSegmentTree> row_groups, optional_ptr<SegmentNode<RowGroup>> resume_after = nullptr);
	//! Hands out row groups in the order dictated by the given order options, which are pushed into the scan by the
	//! optimizer for queries that can be answered by scanning row groups in a specific order (e.g. ORDER BY + LIMIT).
	//! The order is computed up front, so all row groups of the collection must be present in row_groups
	DUCKDB_API static unique_ptr<RowGroupScanSource> Reordered(const RowGroupOrderOptions &options,
	                                                           TransactionData transaction,
	                                                           shared_ptr<RowGroupSegmentTree> row_groups);
};

//===--------------------------------------------------------------------===//
// Row group scan adapter
//===--------------------------------------------------------------------===//
//! What a scan tells its row group scan adapters about itself
struct RowGroupScanInfo {
	//! True when wrapping the table's transaction-local storage rather than its persistent storage
	bool transaction_local = false;
	//! The filters the scan will apply - the final set, including filters pushed in dynamically at execution time
	optional_ptr<TableFilterSet> filters;
	//! The columns being scanned
	const vector<StorageIndex> *column_ids = nullptr;
};

//! An adapter allows an extension to sit between the threads of a table scan and the row groups that the scan reads.
//! Adapters are attached to a LogicalGet by an optimizer extension (see LogicalGet::AddRowGroupScanAdapter), which
//! runs after all built-in optimizers, so the full optimized plan is available when deciding whether to hook a scan.
//!
//! Wrap is called once per row group source of the scan: once for the persistent storage of the table, and once for
//! its transaction-local storage. The two are distinguished by RowGroupScanInfo::transaction_local.
class RowGroupScanAdapter {
public:
	DUCKDB_API virtual ~RowGroupScanAdapter();

public:
	//! The name of this adapter, used in error messages
	virtual string Name() const = 0;
	//! Wrap the source that the scan pulls its row groups from. `child` is the source that DuckDB would otherwise have
	//! used - either the storage-order source, or the source implementing the pushed-down row group order. Return
	//! `child` unchanged to stay out of this scan
	virtual unique_ptr<RowGroupScanSource> Wrap(ClientContext &context, const RowGroupScanInfo &info,
	                                            unique_ptr<RowGroupScanSource> child) = 0;
};

} // namespace duckdb
