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
#include "duckdb/storage/table/row_group_scan_source.hpp"
#include "duckdb/storage/table/row_group_segment_tree.hpp"
#include "duckdb/storage/table/segment_tree.hpp"
#include "duckdb/common/enums/order_type.hpp"

namespace duckdb {

enum class OrderByStatistics : uint8_t { MIN, MAX };
enum class OrderByColumnType : uint8_t { NUMERIC, STRING };

struct RowGroupOrderOptions {
	RowGroupOrderOptions(const StorageIndex &column_idx_p, OrderByStatistics order_by_p, OrderType order_type_p,
	                     OrderByNullType null_order_p, OrderByColumnType column_type_p,
	                     optional_idx row_limit_p = optional_idx(), idx_t row_group_offset_p = 0,
	                     idx_t leading_null_group_offset_p = 0)
	    : column_idx(column_idx_p), order_by(order_by_p), order_type(order_type_p), null_order(null_order_p),
	      column_type(column_type_p), row_limit(row_limit_p), row_group_offset(row_group_offset_p),
	      leading_null_group_offset(leading_null_group_offset_p) {
	}

	const StorageIndex column_idx;
	const OrderByStatistics order_by;
	const OrderType order_type;
	const OrderByNullType null_order;
	const OrderByColumnType column_type;
	const optional_idx row_limit;
	const idx_t row_group_offset;
	const idx_t leading_null_group_offset;

	void Serialize(Serializer &serializer) const;
	static unique_ptr<RowGroupOrderOptions> Deserialize(Deserializer &deserializer);
};

struct OffsetPruningResult {
	idx_t offset_remainder;
	idx_t pruned_row_group_count;
	idx_t leading_null_group_offset;
};

//! Hands out the row groups of a collection in the order dictated by the given order options. The order is computed
//! once during Initialize; handing out row groups afterwards is lock-free
class RowGroupReorderer : public RowGroupScanSource {
public:
	//! Computes the order up front, so row_groups must hold all row groups of the collection
	RowGroupReorderer(const RowGroupOrderOptions &options_p, TransactionData transaction_p,
	                  shared_ptr<RowGroupSegmentTree> row_groups_p);

public:
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
	//! The row groups in the order in which they are handed out - immutable after construction
	vector<reference<SegmentNode<RowGroup>>> ordered_row_groups;
	//! The index of the next row group to hand out (Next is called serialized, so this needs no synchronization)
	idx_t next_index;
};

} // namespace duckdb
