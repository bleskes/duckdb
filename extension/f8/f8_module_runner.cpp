//! Runs the wasm filter module a file carries in its own key-value metadata, and decides from the
//! answer whether the whole file can be skipped.
//!
//! Two entry points, one engine:
//!
//!   * a FileSkipProvider registered with core, which is how a scan reaches this
//!   * the `f8_can_skip_equal` scalar, for evaluating a module directly without a scan
//!
//! They differ in one deliberate way: a module that cannot be run is an *error* for the scalar,
//! because the caller asked for it, and is *ignored* during a scan, because one unreadable file
//! should cost a scan rather than fail the query.
//!
//! Nothing here is specific to a file format. The provider is handed a key-value map, filters keyed
//! by the file's own column order, and a path. Parquet is simply the only reader that implements
//! BaseFileReader::TryGetFileKeyValueMetadata today; give another format that override and this
//! file starts working for it without being recompiled.

#include "f8_module_runner.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/multi_file/file_skip_provider.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/logging/logger.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/planner/table_filter.hpp"

#include <f8.hpp>

namespace duckdb {
namespace {

//! One engine per process. Compiled modules are cached inside it keyed by their bytes, so a scan
//! over many files sharing a module compiles it once. It locks internally.
f8::Runtime &GetRuntime() {
	static f8::Runtime runtime;
	return runtime;
}

const_data_ptr_t BlobData(const string_t &blob) {
	return const_data_ptr_cast(blob.GetData());
}

//===--------------------------------------------------------------------===//
// Running a module during a scan
//===--------------------------------------------------------------------===//

//! Asks the module whether `value` can match no row of `column_index`.
//!
//! Never throws: skipping is an optimisation, so anything that goes wrong here answers "read the
//! file", which is always correct. Also honours the setting that turns skipping off.
bool CanSkipEqual(ClientContext &context, const string &module, const string &metadata, uint32_t column_index,
                  int64_t value) {
	Value enabled(true);
	context.TryGetCurrentSetting(F8_ENABLED_SETTING, enabled);
	if (!enabled.GetValue<bool>()) {
		return false;
	}
	try {
		auto verdict =
		    GetRuntime().CanSkipEqual(const_data_ptr_cast(module.c_str()), module.size(),
		                              const_data_ptr_cast(metadata.c_str()), metadata.size(), column_index, value);
		return verdict == f8::SkipVerdict::CAN_SKIP;
	} catch (const std::exception &error) {
		DUCKDB_LOG_WARNING(
		    context, StringUtil::Format("f8: could not run the filter module, reading the file: %s", error.what()));
		return false;
	}
}

//===--------------------------------------------------------------------===//
// Turning the query's filters into questions the module can answer
//===--------------------------------------------------------------------===//

//! An equality predicate we were able to understand: this column must equal this integer.
struct EqualityPredicate {
	idx_t metadata_column;
	int64_t value;
};

//! Collects integer equality predicates the metadata could rule out.
//!
//! Anything not understood is simply not collected - a filter we ignore only means the file is read,
//! which is always safe.
void CollectEqualityPredicates(const TableFilter &filter, idx_t metadata_column, vector<EqualityPredicate> &result) {
	switch (filter.filter_type) {
	case TableFilterType::CONSTANT_COMPARISON: {
		auto &constant_filter = filter.Cast<ConstantFilter>();
		if (constant_filter.comparison_type != ExpressionType::COMPARE_EQUAL) {
			return;
		}
		auto &constant = constant_filter.constant;
		if (constant.IsNull() || !constant.type().IsIntegral()) {
			return;
		}
		// Cast rather than assume BIGINT: the filter's constant carries the column's own width, and a
		// value outside the BIGINT range cannot be compared against an i64 bound.
		Value as_bigint;
		string error;
		if (!constant.DefaultTryCastAs(LogicalType::BIGINT, as_bigint, &error)) {
			return;
		}
		result.push_back(EqualityPredicate {metadata_column, BigIntValue::Get(as_bigint)});
		return;
	}
	case TableFilterType::CONJUNCTION_AND: {
		// Every child of an AND must hold, so any child that rules the file out rules it out.
		auto &conjunction = filter.Cast<ConjunctionAndFilter>();
		for (auto &child : conjunction.child_filters) {
			CollectEqualityPredicates(*child, metadata_column, result);
		}
		return;
	}
	case TableFilterType::OPTIONAL_FILTER: {
		auto &optional_filter = filter.Cast<OptionalFilter>();
		if (optional_filter.child_filter) {
			CollectEqualityPredicates(*optional_filter.child_filter, metadata_column, result);
		}
		return;
	}
	default:
		// Ranges, IN lists, IS NULL, dynamic join filters: not understood yet, so not used.
		return;
	}
}

//===--------------------------------------------------------------------===//
// The provider core consults per file
//===--------------------------------------------------------------------===//

bool F8CanSkipFile(FileSkipProviderInput &input) {
	auto module_entry = input.file_metadata.find(F8_KEY_MODULE);
	auto metadata_entry = input.file_metadata.find(F8_KEY_METADATA);
	auto has_module = module_entry != input.file_metadata.end();
	auto has_metadata = metadata_entry != input.file_metadata.end();

	if (!has_module && !has_metadata) {
		// Neither present: a file that has nothing to do with this.
		return false;
	}
	// The two entries describe one another, so a file carrying only one of them is malformed rather
	// than simply uninterested. Reporting that is better than silently reading every row: the file was
	// written meaning to be skippable, and is not.
	if (!has_module || !has_metadata) {
		throw InvalidInputException("File \"%s\" carries incomplete f8 metadata: missing %s. Both %s and %s must be "
		                            "present together, or neither of them.",
		                            input.file_path, has_module ? F8_KEY_METADATA : F8_KEY_MODULE, F8_KEY_MODULE,
		                            F8_KEY_METADATA);
	}

	// Well formed. Without filters there is nothing left to prune - validating the file was the point
	// of being called at all.
	if (input.filters.empty()) {
		return false;
	}

	// Filters arrive keyed by the file's own column order, which is exactly how the metadata is
	// indexed, so no name resolution is needed: the metadata is written alongside the file and is
	// therefore consistent with its column order by construction.
	vector<EqualityPredicate> predicates;
	for (auto &entry : input.filters) {
		CollectEqualityPredicates(entry.second.get(), entry.first, predicates);
	}

	// Any single predicate proving no match rules the whole file out.
	for (auto &predicate : predicates) {
		if (CanSkipEqual(input.context, module_entry->second, metadata_entry->second,
		                 NumericCast<uint32_t>(predicate.metadata_column), predicate.value)) {
			return true;
		}
	}
	return false;
}

//===--------------------------------------------------------------------===//
// The scalar function, for running a module without a scan
//===--------------------------------------------------------------------===//

void F8CanSkipEqualFunction(DataChunk &args, ExpressionState &, Vector &result) {
	auto count = args.size();

	UnifiedVectorFormat module_format, metadata_format, column_format, value_format;
	args.data[0].ToUnifiedFormat(count, module_format);
	args.data[1].ToUnifiedFormat(count, metadata_format);
	args.data[2].ToUnifiedFormat(count, column_format);
	args.data[3].ToUnifiedFormat(count, value_format);

	auto module_data = UnifiedVectorFormat::GetData<string_t>(module_format);
	auto metadata_data = UnifiedVectorFormat::GetData<string_t>(metadata_format);
	auto column_data = UnifiedVectorFormat::GetData<int32_t>(column_format);
	auto value_data = UnifiedVectorFormat::GetData<int64_t>(value_format);

	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto result_data = FlatVector::GetData<bool>(result);
	auto &result_validity = FlatVector::Validity(result);

	auto &runtime = GetRuntime();
	for (idx_t i = 0; i < count; i++) {
		auto module_idx = module_format.sel->get_index(i);
		auto metadata_idx = metadata_format.sel->get_index(i);
		auto column_idx = column_format.sel->get_index(i);
		auto value_idx = value_format.sel->get_index(i);

		if (!module_format.validity.RowIsValid(module_idx) || !metadata_format.validity.RowIsValid(metadata_idx) ||
		    !column_format.validity.RowIsValid(column_idx) || !value_format.validity.RowIsValid(value_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}
		auto column_index = column_data[column_idx];
		if (column_index < 0) {
			throw InvalidInputException("f8_can_skip_equal: column_index must not be negative");
		}

		auto &module = module_data[module_idx];
		auto &metadata = metadata_data[metadata_idx];
		try {
			auto verdict =
			    runtime.CanSkipEqual(BlobData(module), module.GetSize(), BlobData(metadata), metadata.GetSize(),
			                         NumericCast<uint32_t>(column_index), value_data[value_idx]);
			result_data[i] = verdict == f8::SkipVerdict::CAN_SKIP;
		} catch (const f8::F8Error &error) {
			// Unlike a scan, a direct call reports the problem: the caller asked for this module.
			throw InvalidInputException("f8_can_skip_equal: %s", error.what());
		}
	}
}

} // namespace

ScalarFunction GetF8CanSkipEqualFunction() {
	return ScalarFunction("f8_can_skip_equal",
	                      {LogicalType::BLOB, LogicalType::BLOB, LogicalType::INTEGER, LogicalType::BIGINT},
	                      LogicalType::BOOLEAN, F8CanSkipEqualFunction);
}

void RegisterF8SkipProvider(DatabaseInstance &db) {
	FileSkipProvider provider;
	provider.name = "f8";
	provider.can_skip_function = F8CanSkipFile;
	FileSkipProvider::Register(DBConfig::GetConfig(db), std::move(provider));
}

} // namespace duckdb
