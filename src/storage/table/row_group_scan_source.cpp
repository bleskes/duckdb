#include "duckdb/storage/table/row_group_scan_source.hpp"

#include "duckdb/storage/table/row_group_reorderer.hpp"
#include "duckdb/storage/table/row_group_segment_tree.hpp"

namespace duckdb {

RowGroupScanResult RowGroupScanResult::WithRowGroup(SegmentNode<RowGroup> &row_group) {
	RowGroupScanResult result;
	result.type = AsyncResultType::HAVE_MORE_OUTPUT;
	result.row_group = &row_group;
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

RowGroupScanSource::~RowGroupScanSource() {
}

namespace {

//! Hands out the row groups of the collection in storage order
class StorageRowGroupScanSource : public RowGroupScanSource {
public:
	StorageRowGroupScanSource(shared_ptr<RowGroupSegmentTree> row_groups_p,
	                          optional_ptr<SegmentNode<RowGroup>> resume_after)
	    : row_groups(std::move(row_groups_p)), cursor(resume_after) {
	}

	RowGroupScanResult Next(RowGroupScanSourceInput &input) override {
		D_ASSERT(row_groups);
		// Next() is called serialized by the scan, so the cursor needs no synchronization
		if (finished) {
			return RowGroupScanResult::Finished();
		}
		cursor = cursor ? row_groups->GetNextSegment(*cursor) : row_groups->GetRootSegment();
		if (!cursor) {
			finished = true;
			return RowGroupScanResult::Finished();
		}
		return RowGroupScanResult::WithRowGroup(*cursor);
	}

private:
	shared_ptr<RowGroupSegmentTree> row_groups;
	//! The row group that was handed out last
	optional_ptr<SegmentNode<RowGroup>> cursor;
	//! Whether or not the last row group has been handed out
	bool finished = false;
};

} // namespace

unique_ptr<RowGroupScanSource> RowGroupScanSources::Storage(shared_ptr<RowGroupSegmentTree> row_groups,
                                                            optional_ptr<SegmentNode<RowGroup>> resume_after) {
	return make_uniq<StorageRowGroupScanSource>(std::move(row_groups), resume_after);
}

unique_ptr<RowGroupScanSource> RowGroupScanSources::Reordered(const RowGroupOrderOptions &options,
                                                              TransactionData transaction,
                                                              shared_ptr<RowGroupSegmentTree> row_groups) {
	return make_uniq<RowGroupReorderer>(options, transaction, std::move(row_groups));
}

RowGroupScanAdapter::~RowGroupScanAdapter() {
}

} // namespace duckdb
