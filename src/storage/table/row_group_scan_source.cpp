#include "duckdb/storage/table/row_group_scan_source.hpp"

#include "duckdb/common/mutex.hpp"
#include "duckdb/storage/table/row_group_reorderer.hpp"
#include "duckdb/storage/table/row_group_segment_tree.hpp"

namespace duckdb {

//===--------------------------------------------------------------------===//
// RowGroupScanResult
//===--------------------------------------------------------------------===//
RowGroupScanResult RowGroupScanResult::RowGroup(optional_ptr<SegmentNode<duckdb::RowGroup>> row_group) {
	if (!row_group) {
		return Finished();
	}
	RowGroupScanResult result;
	result.type = AsyncResultType::HAVE_MORE_OUTPUT;
	result.row_group = row_group;
	return result;
}

RowGroupScanResult RowGroupScanResult::Finished() {
	RowGroupScanResult result;
	result.type = AsyncResultType::FINISHED;
	return result;
}

RowGroupScanResult RowGroupScanResult::Blocked() {
	RowGroupScanResult result;
	result.type = AsyncResultType::BLOCKED;
	return result;
}

//===--------------------------------------------------------------------===//
// RowGroupScanAssignment
//===--------------------------------------------------------------------===//
RowGroupScanAssignment RowGroupScanAssignment::RowGroupAssigned(idx_t rows) {
	RowGroupScanAssignment result;
	result.type = AsyncResultType::HAVE_MORE_OUTPUT;
	result.rows = rows;
	return result;
}

RowGroupScanAssignment RowGroupScanAssignment::Finished() {
	RowGroupScanAssignment result;
	result.type = AsyncResultType::FINISHED;
	return result;
}

RowGroupScanAssignment RowGroupScanAssignment::Blocked() {
	RowGroupScanAssignment result;
	result.type = AsyncResultType::BLOCKED;
	return result;
}

//===--------------------------------------------------------------------===//
// RowGroupScanSource
//===--------------------------------------------------------------------===//
RowGroupScanSource::~RowGroupScanSource() {
}

//===--------------------------------------------------------------------===//
// Built-in sources
//===--------------------------------------------------------------------===//
namespace {

//! Hands out the row groups of the collection in storage order
class StorageRowGroupScanSource : public RowGroupScanSource {
public:
	void Initialize(RowGroupScanSourceInitInput &input) override {
		row_groups = input.row_groups;
	}

	RowGroupScanResult Next(RowGroupScanSourceInput &input) override {
		D_ASSERT(row_groups);
		// walking the segment tree is inherently sequential - serialize the cursor
		lock_guard<mutex> guard(cursor_lock);
		if (finished) {
			return RowGroupScanResult::Finished();
		}
		cursor = cursor ? row_groups->GetNextSegment(*cursor) : row_groups->GetRootSegment();
		if (!cursor) {
			finished = true;
			return RowGroupScanResult::Finished();
		}
		return RowGroupScanResult::RowGroup(cursor);
	}

private:
	shared_ptr<RowGroupSegmentTree> row_groups;
	mutex cursor_lock;
	//! The row group that was handed out last
	optional_ptr<SegmentNode<RowGroup>> cursor;
	//! Whether or not the last row group has been handed out
	bool finished = false;
};

} // namespace

unique_ptr<RowGroupScanSource> RowGroupScanSources::Storage() {
	auto result = make_uniq<StorageRowGroupScanSource>();
	return std::move(result);
}

unique_ptr<RowGroupScanSource> RowGroupScanSources::Reordered(const RowGroupOrderOptions &options,
                                                              TransactionData transaction) {
	auto result = make_uniq<RowGroupReorderer>(options, transaction);
	return std::move(result);
}

//===--------------------------------------------------------------------===//
// RowGroupScanAdapter
//===--------------------------------------------------------------------===//
RowGroupScanAdapter::~RowGroupScanAdapter() {
}

} // namespace duckdb
