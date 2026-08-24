#include "catch.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/parallel/interrupt.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/storage/table/row_group_scan_source.hpp"
#include "test_helpers.hpp"

#include <thread>

using namespace duckdb; // NOLINT

namespace {

//! How the test source messes with the row groups it pulls out of its child
enum class TestSourceMode {
	//! Hand out the row groups of the child in reverse
	REVERSE,
	//! Only hand out the first row group of the child
	FIRST_ONLY,
	//! Block the scan once before handing out the row groups of the child
	BLOCK_ONCE
};

struct TestSourceStats {
	atomic<idx_t> created {0};
	atomic<idx_t> created_persistent {0};
	atomic<idx_t> created_transaction_local {0};
	atomic<idx_t> initialized {0};
	atomic<idx_t> row_groups_handed_out {0};
	atomic<idx_t> blocks {0};
};

class TestRowGroupScanSource : public RowGroupScanSource {
public:
	TestRowGroupScanSource(unique_ptr<RowGroupScanSource> child_p, TestSourceMode mode, TestSourceStats &stats)
	    : child(std::move(child_p)), mode(mode), stats(stats) {
		// drain the child up front - this is what a source that sorts or filters row groups does. The child is already
		// fully constructed (it has the collection's row groups), and the built-in child sources never block, so we
		// pull them out directly with no context and no interrupt state
		RowGroupScanSourceInput next_input(nullptr, nullptr);
		while (true) {
			auto result = child->Next(next_input);
			if (result.type != AsyncResultType::HAVE_MORE_OUTPUT) {
				break;
			}
			row_groups.push_back(*result.row_group);
		}
		if (mode == TestSourceMode::REVERSE) {
			std::reverse(row_groups.begin(), row_groups.end());
		} else if (mode == TestSourceMode::FIRST_ONLY && row_groups.size() > 1) {
			row_groups.erase(row_groups.begin() + 1, row_groups.end());
		}
		stats.initialized++;
	}

public:
	RowGroupScanResult Next(RowGroupScanSourceInput &input) override {
		if (mode == TestSourceMode::BLOCK_ONCE && TryBlockOnce(input)) {
			return RowGroupScanResult::Blocked();
		}
		const auto index = next_index++;
		if (index >= row_groups.size()) {
			return RowGroupScanResult::Finished();
		}
		stats.row_groups_handed_out++;
		return RowGroupScanResult::WithRowGroup(row_groups[index].get());
	}

private:
	//! Park the scan exactly once, on the first pull, and resume it from another thread after a short delay. This uses
	//! the lost-wakeup-safe pattern: register the interrupt under the state's lock (BlockTask) and resume under the
	//! same lock (UnblockTasks). Every later pull makes progress - a source that keeps blocking without progressing
	//! would spin forever
	bool TryBlockOnce(RowGroupScanSourceInput &input) {
		auto guard = block_state.Lock();
		if (has_blocked || !input.interrupt_state) {
			return false;
		}
		has_blocked = true;
		stats.blocks++;
		if (!block_state.BlockTask(guard, *input.interrupt_state)) {
			// the scan can no longer block - proceed instead
			return false;
		}
		unblock_thread = std::thread([this]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			auto g = block_state.Lock();
			block_state.UnblockTasks(g);
		});
		unblock_thread.detach();
		return true;
	}

private:
	unique_ptr<RowGroupScanSource> child;
	const TestSourceMode mode;
	TestSourceStats &stats;

	vector<reference<SegmentNode<RowGroup>>> row_groups;
	atomic<idx_t> next_index {0};

	StateWithBlockableTasks block_state;
	bool has_blocked = false;
	std::thread unblock_thread;
};

class TestRowGroupScanAdapter : public RowGroupScanAdapter {
public:
	TestRowGroupScanAdapter(TestSourceMode mode, TestSourceStats &stats) : mode(mode), stats(stats) {
	}

public:
	string Name() const override {
		return "test_row_group_scan_source";
	}

	unique_ptr<RowGroupScanSource> Wrap(const RowGroupScanSourceInfo &info,
	                                    unique_ptr<RowGroupScanSource> child) override {
		// wrap every source of the scan - one per collection (persistent + transaction-local). info.transaction_local
		// tells them apart, so an adapter that only cares about persistent data could return `child` unchanged here
		stats.created++;
		if (info.transaction_local) {
			stats.created_transaction_local++;
		} else {
			stats.created_persistent++;
		}
		auto result = make_uniq<TestRowGroupScanSource>(std::move(child), mode, stats);
		return std::move(result);
	}

private:
	const TestSourceMode mode;
	TestSourceStats &stats;
};

struct TestOptimizerInfo : public OptimizerExtensionInfo {
	TestOptimizerInfo(TestSourceMode mode, TestSourceStats &stats) : mode(mode), stats(stats) {
	}

	TestSourceMode mode;
	TestSourceStats &stats;
};

void AttachSources(LogicalOperator &op, TestOptimizerInfo &info) {
	if (op.type == LogicalOperatorType::LOGICAL_GET) {
		auto &get = op.Cast<LogicalGet>();
		if (get.function.add_row_group_scan_adapter) {
			get.AddRowGroupScanAdapter(make_shared_ptr<TestRowGroupScanAdapter>(info.mode, info.stats));
		}
	}
	for (auto &child : op.children) {
		AttachSources(*child, info);
	}
}

//! Runs after all built-in optimizers, so the plan is final when we decide which scans to hook
void TestOptimize(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
	auto &info = reinterpret_cast<TestOptimizerInfo &>(*input.info);
	AttachSources(*plan, info);
}

//! A table with `row_group_count` row groups holding 0..n-1 in column i
unique_ptr<DuckDB> MakeDatabase(TestSourceMode mode, TestSourceStats &stats, idx_t &row_count, idx_t threads = 4) {
	DBConfig config;
	OptimizerExtension extension;
	extension.optimize_function = TestOptimize;
	extension.optimizer_info = make_shared_ptr<TestOptimizerInfo>(mode, stats);
	OptimizerExtension::Register(config, std::move(extension));

	auto db = make_uniq<DuckDB>(nullptr, &config);
	Connection conn(*db);
	REQUIRE_NO_FAIL(conn.Query("SET threads=" + to_string(threads)));
	// three row groups worth of data
	row_count = 3 * DEFAULT_ROW_GROUP_SIZE;
	REQUIRE_NO_FAIL(conn.Query("CREATE TABLE integers AS SELECT i FROM range(" + to_string(row_count) + ") t(i)"));
	return db;
}

} // namespace

TEST_CASE("Test row group scan source - reordering", "[api]") {
	TestSourceStats stats;
	idx_t row_count;
	auto db = MakeDatabase(TestSourceMode::REVERSE, stats, row_count, 1);
	Connection conn(*db);

	auto result = conn.Query("SELECT i FROM integers");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->RowCount() == row_count);
	REQUIRE(stats.initialized == 1);
	REQUIRE(stats.row_groups_handed_out == 3);
	// the source is built (and the adapter wraps it) per collection that exists at scan setup. This query has no
	// transaction-local storage, so only the persistent source is wrapped (info.transaction_local tells them apart)
	REQUIRE(stats.created_persistent == 1);
	REQUIRE(stats.created_transaction_local == 0);
	// the last row group is scanned first
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(NumericCast<int64_t>(2 * DEFAULT_ROW_GROUP_SIZE)));

	// the source sees every row group of the scan exactly once, so both the row count and the sum of all values add up
	// (the sum would not match if any row were dropped, duplicated or altered by the reordering)
	result = conn.Query("SELECT count(*), sum(i) FROM integers");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(NumericCast<int64_t>(row_count)));
	REQUIRE(result->GetValue(1, 0) == Value::BIGINT(NumericCast<int64_t>(row_count * (row_count - 1) / 2)));
}

TEST_CASE("Test row group scan source - filtering out row groups", "[api]") {
	TestSourceStats stats;
	idx_t row_count;
	auto db = MakeDatabase(TestSourceMode::FIRST_ONLY, stats, row_count);
	Connection conn(*db);

	// only the row groups that the source hands out are scanned - by any of the scan threads
	auto result = conn.Query("SELECT count(*), max(i) FROM integers");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(NumericCast<int64_t>(DEFAULT_ROW_GROUP_SIZE)));
	REQUIRE(result->GetValue(1, 0) == Value::BIGINT(NumericCast<int64_t>(DEFAULT_ROW_GROUP_SIZE - 1)));
	REQUIRE(stats.row_groups_handed_out == 1);

	// transaction-local data flows through correctly: the (single) local row group survives the "keep first" source
	REQUIRE_NO_FAIL(conn.Query("BEGIN TRANSACTION"));
	REQUIRE_NO_FAIL(conn.Query("INSERT INTO integers VALUES (-1)"));
	result = conn.Query("SELECT count(*) FROM integers WHERE i = -1");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(1));
	REQUIRE_NO_FAIL(conn.Query("ROLLBACK"));
}

TEST_CASE("Test row group scan source - blocking and resuming the scan", "[api]") {
	TestSourceStats stats;
	idx_t row_count;
	auto db = MakeDatabase(TestSourceMode::BLOCK_ONCE, stats, row_count);
	Connection conn(*db);

	auto result = conn.Query("SELECT count(*), sum(i) FROM integers");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(NumericCast<int64_t>(row_count)));
	// every row survives the block/resume exactly once - the value sum confirms none were dropped or duplicated
	REQUIRE(result->GetValue(1, 0) == Value::BIGINT(NumericCast<int64_t>(row_count * (row_count - 1) / 2)));
	REQUIRE(stats.blocks == 1);
	REQUIRE(stats.row_groups_handed_out == 3);

	// a second scan blocks - and resumes - again, with a fresh source
	stats.blocks = 0;
	stats.row_groups_handed_out = 0;
	result = conn.Query("SELECT count(*) FROM integers");
	REQUIRE_NO_FAIL(*result);
	REQUIRE(result->GetValue(0, 0) == Value::BIGINT(NumericCast<int64_t>(row_count)));
}
