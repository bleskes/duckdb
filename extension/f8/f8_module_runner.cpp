//! Runs the wasm filter module a file carries in its own key-value metadata, and decides from the
//! answer whether the whole file can be skipped.
//!
//! Two entry points, one engine:
//!
//!   * a FileSkipProvider registered with core, which is how a scan reaches this
//!   * the `f8_can_skip_equal` and `f8_can_skip_conj` scalars, for evaluating a module directly
//!     without a scan
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

bool SkippingEnabled(ClientContext &context) {
	Value enabled(true);
	context.TryGetCurrentSetting(F8_ENABLED_SETTING, enabled);
	return enabled.GetValue<bool>();
}

//! Runs `ask` against the module, turning any failure into "read the file". Never throws: skipping is an
//! optimisation, so a conservative answer is always correct. Honours the setting that turns it off.
template <class ASK>
bool AskModule(ClientContext &context, ASK &&ask) {
	if (!SkippingEnabled(context)) {
		return false;
	}
	try {
		return ask(GetRuntime()) == f8::SkipVerdict::CAN_SKIP;
	} catch (const std::exception &error) {
		DUCKDB_LOG_WARNING(
		    context, StringUtil::Format("f8: could not run the filter module, reading the file: %s", error.what()));
		return false;
	}
}

//! Asks the module whether `value` can match no row of `column_index`.
bool CanSkipEqual(ClientContext &context, const string &module, const string &metadata, uint32_t column_index,
                  int64_t value) {
	return AskModule(context, [&](const f8::Runtime &runtime) {
		return runtime.CanSkipEqual(const_data_ptr_cast(module.c_str()), module.size(),
		                            const_data_ptr_cast(metadata.c_str()), metadata.size(), column_index, value);
	});
}

//! Asks the module about two terms that must both hold, in one call.
bool CanSkipConjunction(ClientContext &context, const string &module, const string &metadata, const f8::SkipTerm &lhs,
                        const f8::SkipTerm &rhs) {
	return AskModule(context, [&](const f8::Runtime &runtime) {
		return runtime.CanSkipConjunction(const_data_ptr_cast(module.c_str()), module.size(),
		                                  const_data_ptr_cast(metadata.c_str()), metadata.size(), lhs, rhs);
	});
}

//===--------------------------------------------------------------------===//
// Turning the query's filters into questions the module can answer
//===--------------------------------------------------------------------===//

//! An equality predicate we were able to understand: this column must equal this integer.
struct EqualityPredicate {
	idx_t metadata_column;
	int64_t value;
};

f8::SkipTerm AsSkipTerm(const EqualityPredicate &predicate) {
	return f8::SkipTerm {f8::SkipType::EQUAL, NumericCast<uint32_t>(predicate.metadata_column), predicate.value};
}

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

	// Every predicate has to hold, so any one of them proving no match rules the whole file out. A
	// single predicate goes as itself, down the direct entry point.
	if (predicates.size() == 1) {
		return CanSkipEqual(input.context, module_entry->second, metadata_entry->second,
		                    NumericCast<uint32_t>(predicates[0].metadata_column), predicates[0].value);
	}
	// Several go as pairs, so a module can answer from metadata that only means something jointly. Every
	// pair rather than only adjacent ones: which columns such metadata covers is not something the engine
	// can guess. Quadratic in the predicate count, which in practice is two or three.
	for (idx_t lhs = 0; lhs < predicates.size(); lhs++) {
		for (idx_t rhs = lhs + 1; rhs < predicates.size(); rhs++) {
			if (CanSkipConjunction(input.context, module_entry->second, metadata_entry->second,
			                       AsSkipTerm(predicates[lhs]), AsSkipTerm(predicates[rhs]))) {
				return true;
			}
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

//! `f8_can_skip_conj(module, metadata, lhs_type, lhs_column, lhs_value, rhs_type, rhs_column, rhs_value)`
//!
//! Skip types are raw rather than assumed, so a test can hand a module a type it does not know and check
//! that it answers conservatively. The scan path can never produce one.
void F8CanSkipConjFunction(DataChunk &args, ExpressionState &, Vector &result) {
	auto count = args.size();
	static constexpr idx_t ARGUMENT_COUNT = 8;
	UnifiedVectorFormat formats[ARGUMENT_COUNT];
	for (idx_t argument = 0; argument < ARGUMENT_COUNT; argument++) {
		args.data[argument].ToUnifiedFormat(count, formats[argument]);
	}

	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto result_data = FlatVector::GetData<bool>(result);
	auto &result_validity = FlatVector::Validity(result);

	auto &runtime = GetRuntime();
	for (idx_t i = 0; i < count; i++) {
		auto row_is_valid = true;
		for (idx_t argument = 0; argument < ARGUMENT_COUNT; argument++) {
			if (!formats[argument].validity.RowIsValid(formats[argument].sel->get_index(i))) {
				row_is_valid = false;
				break;
			}
		}
		if (!row_is_valid) {
			result_validity.SetInvalid(i);
			continue;
		}

		//! Reads argument `argument` of row i.
		auto blob = [&](idx_t argument) {
			return UnifiedVectorFormat::GetData<string_t>(formats[argument])[formats[argument].sel->get_index(i)];
		};
		auto integer = [&](idx_t argument) {
			auto value = UnifiedVectorFormat::GetData<int32_t>(formats[argument])[formats[argument].sel->get_index(i)];
			if (value < 0) {
				throw InvalidInputException("f8_can_skip_conj: skip types and column indexes must not be negative");
			}
			return NumericCast<uint32_t>(value);
		};
		auto big_integer = [&](idx_t argument) {
			return UnifiedVectorFormat::GetData<int64_t>(formats[argument])[formats[argument].sel->get_index(i)];
		};

		f8::SkipTerm lhs {static_cast<f8::SkipType>(integer(2)), integer(3), big_integer(4)};
		f8::SkipTerm rhs {static_cast<f8::SkipType>(integer(5)), integer(6), big_integer(7)};

		auto module = blob(0);
		auto metadata = blob(1);
		try {
			auto verdict = runtime.CanSkipConjunction(BlobData(module), module.GetSize(), BlobData(metadata),
			                                          metadata.GetSize(), lhs, rhs);
			result_data[i] = verdict == f8::SkipVerdict::CAN_SKIP;
		} catch (const f8::F8Error &error) {
			// Unlike a scan, a direct call reports the problem: the caller asked for this module.
			throw InvalidInputException("f8_can_skip_conj: %s", error.what());
		}
	}
}

} // namespace

ScalarFunction GetF8CanSkipEqualFunction() {
	return ScalarFunction("f8_can_skip_equal",
	                      {LogicalType::BLOB, LogicalType::BLOB, LogicalType::INTEGER, LogicalType::BIGINT},
	                      LogicalType::BOOLEAN, F8CanSkipEqualFunction);
}

ScalarFunction GetF8CanSkipConjFunction() {
	return ScalarFunction("f8_can_skip_conj",
	                      {LogicalType::BLOB, LogicalType::BLOB, LogicalType::INTEGER, LogicalType::INTEGER,
	                       LogicalType::BIGINT, LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BIGINT},
	                      LogicalType::BOOLEAN, F8CanSkipConjFunction);
}

void RegisterF8SkipProvider(DatabaseInstance &db) {
	FileSkipProvider provider;
	provider.name = "f8";
	provider.can_skip_function = F8CanSkipFile;
	FileSkipProvider::Register(DBConfig::GetConfig(db), std::move(provider));
}

} // namespace duckdb
