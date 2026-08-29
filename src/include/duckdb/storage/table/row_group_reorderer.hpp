//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/table/row_group_reorderer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/function/partition_stats.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include "duckdb/storage/table/row_group_order_options.hpp"
#include "duckdb/storage/table/row_group_scan_source.hpp"
#include "duckdb/storage/table/row_group_segment_tree.hpp"
#include "duckdb/storage/table/segment_tree.hpp"

namespace duckdb {

struct OffsetPruningResult {
	idx_t offset_remainder;
	idx_t pruned_row_group_count;
	idx_t leading_null_group_offset;
};

//! Hands out the row groups of a collection in the order dictated by the given order options. The order is computed
//! once during Initialize; handing out row groups afterwards is lock-free
class RowGroupReorderer : public RowGroupScanSource {
public:
	RowGroupReorderer(const RowGroupOrderOptions &options_p, TransactionData transaction_p);

public:
	void Initialize(RowGroupScanSourceInitInput &input) override;
	RowGroupScanResult Next(RowGroupScanSourceInput &input) override;

	static Value RetrieveStat(const BaseStatistics &stats, OrderByStatistics order_by, OrderByColumnType column_type);
	static OffsetPruningResult GetOffsetAfterPruning(OrderByStatistics order_by, OrderByColumnType column_type,
	                                                 OrderType order_type, OrderByNullType null_order,
	                                                 const StorageIndex &column_idx, idx_t row_offset,
	                                                 vector<PartitionStatistics> &stats);

private:
	//! Compute the order in which the row groups are handed out
	void ComputeOrder(RowGroupSegmentTree &row_groups);

private:
	const RowGroupOrderOptions options;
	const TransactionData transaction;

	//! The segment tree we hand out row groups of - kept alive for as long as we do
	shared_ptr<RowGroupSegmentTree> row_groups;
	//! The row groups in the order in which they are handed out - immutable after Initialize
	vector<reference<SegmentNode<RowGroup>>> ordered_row_groups;
	//! The index of the next row group to hand out (Next is called serialized, so this needs no synchronization)
	idx_t next_index;
};

} // namespace duckdb
