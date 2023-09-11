//===----------------------------------------------------------------------===//
// This file is automatically generated by scripts/generate_serialization.py
// Do not edit this file manually, your changes will be overwritten
//===----------------------------------------------------------------------===//

#include "duckdb/common/serializer/serializer.hpp"
#include "duckdb/common/serializer/deserializer.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/extra_type_info.hpp"
#include "duckdb/parser/common_table_expression_info.hpp"
#include "duckdb/parser/query_node.hpp"
#include "duckdb/parser/result_modifier.hpp"
#include "duckdb/planner/bound_result_modifier.hpp"
#include "duckdb/parser/expression/case_expression.hpp"
#include "duckdb/planner/expression/bound_case_expression.hpp"
#include "duckdb/parser/parsed_data/sample_options.hpp"
#include "duckdb/parser/tableref/pivotref.hpp"
#include "duckdb/planner/tableref/bound_pivotref.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/parser/column_list.hpp"
#include "duckdb/planner/column_binding.hpp"
#include "duckdb/planner/expression/bound_parameter_data.hpp"
#include "duckdb/planner/joinside.hpp"
#include "duckdb/parser/parsed_data/vacuum_info.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/common/multi_file_reader_options.hpp"
#include "duckdb/common/multi_file_reader.hpp"
#include "duckdb/execution/operator/scan/csv/csv_reader_options.hpp"
#include "duckdb/function/scalar/strftime_format.hpp"
#include "duckdb/function/table/read_csv.hpp"
#include "duckdb/common/types/interval.hpp"

namespace duckdb {

void BoundCaseCheck::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "when_expr", when_expr);
	serializer.WriteProperty(101, "then_expr", then_expr);
}

BoundCaseCheck BoundCaseCheck::Deserialize(Deserializer &deserializer) {
	BoundCaseCheck result;
	deserializer.ReadProperty(100, "when_expr", result.when_expr);
	deserializer.ReadProperty(101, "then_expr", result.then_expr);
	return result;
}

void BoundOrderByNode::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "type", type);
	serializer.WriteProperty(101, "null_order", null_order);
	serializer.WriteProperty(102, "expression", expression);
}

BoundOrderByNode BoundOrderByNode::Deserialize(Deserializer &deserializer) {
	auto type = deserializer.ReadProperty<OrderType>(100, "type");
	auto null_order = deserializer.ReadProperty<OrderByNullType>(101, "null_order");
	auto expression = deserializer.ReadProperty<unique_ptr<Expression>>(102, "expression");
	BoundOrderByNode result(type, null_order, std::move(expression));
	return result;
}

void BoundParameterData::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "value", value);
	serializer.WriteProperty(101, "return_type", return_type);
}

shared_ptr<BoundParameterData> BoundParameterData::Deserialize(Deserializer &deserializer) {
	auto value = deserializer.ReadProperty<Value>(100, "value");
	auto result = duckdb::shared_ptr<BoundParameterData>(new BoundParameterData(value));
	deserializer.ReadProperty(101, "return_type", result->return_type);
	return result;
}

void BoundPivotInfo::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "group_count", group_count);
	serializer.WriteProperty(101, "types", types);
	serializer.WriteProperty(102, "pivot_values", pivot_values);
	serializer.WriteProperty(103, "aggregates", aggregates);
}

BoundPivotInfo BoundPivotInfo::Deserialize(Deserializer &deserializer) {
	BoundPivotInfo result;
	deserializer.ReadProperty(100, "group_count", result.group_count);
	deserializer.ReadProperty(101, "types", result.types);
	deserializer.ReadProperty(102, "pivot_values", result.pivot_values);
	deserializer.ReadProperty(103, "aggregates", result.aggregates);
	return result;
}

void CSVReaderOptions::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "has_delimiter", has_delimiter);
	serializer.WriteProperty(101, "has_quote", has_quote);
	serializer.WriteProperty(102, "has_escape", has_escape);
	serializer.WriteProperty(103, "has_header", has_header);
	serializer.WriteProperty(104, "ignore_errors", ignore_errors);
	serializer.WriteProperty(105, "buffer_sample_size", buffer_sample_size);
	serializer.WriteProperty(106, "null_str", null_str);
	serializer.WriteProperty(107, "compression", compression);
	serializer.WriteProperty(108, "allow_quoted_nulls", allow_quoted_nulls);
	serializer.WriteProperty(109, "skip_rows_set", skip_rows_set);
	serializer.WriteProperty(110, "maximum_line_size", maximum_line_size);
	serializer.WriteProperty(111, "normalize_names", normalize_names);
	serializer.WriteProperty(112, "force_not_null", force_not_null);
	serializer.WriteProperty(113, "all_varchar", all_varchar);
	serializer.WriteProperty(114, "sample_chunk_size", sample_chunk_size);
	serializer.WriteProperty(115, "sample_chunks", sample_chunks);
	serializer.WriteProperty(116, "auto_detect", auto_detect);
	serializer.WriteProperty(117, "file_path", file_path);
	serializer.WriteProperty(118, "decimal_separator", decimal_separator);
	serializer.WriteProperty(119, "null_padding", null_padding);
	serializer.WriteProperty(120, "buffer_size", buffer_size);
	serializer.WriteProperty(121, "file_options", file_options);
	serializer.WriteProperty(122, "force_quote", force_quote);
	serializer.WriteProperty(123, "rejects_table_name", rejects_table_name);
	serializer.WriteProperty(124, "rejects_limit", rejects_limit);
	serializer.WriteProperty(125, "rejects_recovery_columns", rejects_recovery_columns);
	serializer.WriteProperty(126, "rejects_recovery_column_ids", rejects_recovery_column_ids);
	serializer.WriteProperty(127, "dialect_options.state_machine_options.delimiter", dialect_options.state_machine_options.delimiter);
	serializer.WriteProperty(128, "dialect_options.state_machine_options.quote", dialect_options.state_machine_options.quote);
	serializer.WriteProperty(129, "dialect_options.state_machine_options.escape", dialect_options.state_machine_options.escape);
	serializer.WriteProperty(130, "dialect_options.header", dialect_options.header);
	serializer.WriteProperty(131, "dialect_options.num_cols", dialect_options.num_cols);
	serializer.WriteProperty(132, "dialect_options.new_line", dialect_options.new_line);
	serializer.WriteProperty(133, "dialect_options.skip_rows", dialect_options.skip_rows);
	serializer.WriteProperty(134, "dialect_options.date_format", dialect_options.date_format);
	serializer.WriteProperty(135, "dialect_options.has_format", dialect_options.has_format);
}

CSVReaderOptions CSVReaderOptions::Deserialize(Deserializer &deserializer) {
	CSVReaderOptions result;
	deserializer.ReadProperty(100, "has_delimiter", result.has_delimiter);
	deserializer.ReadProperty(101, "has_quote", result.has_quote);
	deserializer.ReadProperty(102, "has_escape", result.has_escape);
	deserializer.ReadProperty(103, "has_header", result.has_header);
	deserializer.ReadProperty(104, "ignore_errors", result.ignore_errors);
	deserializer.ReadProperty(105, "buffer_sample_size", result.buffer_sample_size);
	deserializer.ReadProperty(106, "null_str", result.null_str);
	deserializer.ReadProperty(107, "compression", result.compression);
	deserializer.ReadProperty(108, "allow_quoted_nulls", result.allow_quoted_nulls);
	deserializer.ReadProperty(109, "skip_rows_set", result.skip_rows_set);
	deserializer.ReadProperty(110, "maximum_line_size", result.maximum_line_size);
	deserializer.ReadProperty(111, "normalize_names", result.normalize_names);
	deserializer.ReadProperty(112, "force_not_null", result.force_not_null);
	deserializer.ReadProperty(113, "all_varchar", result.all_varchar);
	deserializer.ReadProperty(114, "sample_chunk_size", result.sample_chunk_size);
	deserializer.ReadProperty(115, "sample_chunks", result.sample_chunks);
	deserializer.ReadProperty(116, "auto_detect", result.auto_detect);
	deserializer.ReadProperty(117, "file_path", result.file_path);
	deserializer.ReadProperty(118, "decimal_separator", result.decimal_separator);
	deserializer.ReadProperty(119, "null_padding", result.null_padding);
	deserializer.ReadProperty(120, "buffer_size", result.buffer_size);
	deserializer.ReadProperty(121, "file_options", result.file_options);
	deserializer.ReadProperty(122, "force_quote", result.force_quote);
	deserializer.ReadProperty(123, "rejects_table_name", result.rejects_table_name);
	deserializer.ReadProperty(124, "rejects_limit", result.rejects_limit);
	deserializer.ReadProperty(125, "rejects_recovery_columns", result.rejects_recovery_columns);
	deserializer.ReadProperty(126, "rejects_recovery_column_ids", result.rejects_recovery_column_ids);
	deserializer.ReadProperty(127, "dialect_options.state_machine_options.delimiter", result.dialect_options.state_machine_options.delimiter);
	deserializer.ReadProperty(128, "dialect_options.state_machine_options.quote", result.dialect_options.state_machine_options.quote);
	deserializer.ReadProperty(129, "dialect_options.state_machine_options.escape", result.dialect_options.state_machine_options.escape);
	deserializer.ReadProperty(130, "dialect_options.header", result.dialect_options.header);
	deserializer.ReadProperty(131, "dialect_options.num_cols", result.dialect_options.num_cols);
	deserializer.ReadProperty(132, "dialect_options.new_line", result.dialect_options.new_line);
	deserializer.ReadProperty(133, "dialect_options.skip_rows", result.dialect_options.skip_rows);
	deserializer.ReadProperty(134, "dialect_options.date_format", result.dialect_options.date_format);
	deserializer.ReadProperty(135, "dialect_options.has_format", result.dialect_options.has_format);
	return result;
}

void CaseCheck::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "when_expr", when_expr);
	serializer.WriteProperty(101, "then_expr", then_expr);
}

CaseCheck CaseCheck::Deserialize(Deserializer &deserializer) {
	CaseCheck result;
	deserializer.ReadProperty(100, "when_expr", result.when_expr);
	deserializer.ReadProperty(101, "then_expr", result.then_expr);
	return result;
}

void ColumnBinding::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "table_index", table_index);
	serializer.WriteProperty(101, "column_index", column_index);
}

ColumnBinding ColumnBinding::Deserialize(Deserializer &deserializer) {
	ColumnBinding result;
	deserializer.ReadProperty(100, "table_index", result.table_index);
	deserializer.ReadProperty(101, "column_index", result.column_index);
	return result;
}

void ColumnDefinition::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "name", name);
	serializer.WriteProperty(101, "type", type);
	serializer.WritePropertyWithDefault(102, "expression", expression, unique_ptr<ParsedExpression>());
	serializer.WriteProperty(103, "category", category);
	serializer.WriteProperty(104, "compression_type", compression_type);
}

ColumnDefinition ColumnDefinition::Deserialize(Deserializer &deserializer) {
	auto name = deserializer.ReadProperty<string>(100, "name");
	auto type = deserializer.ReadProperty<LogicalType>(101, "type");
	auto expression = deserializer.ReadPropertyWithDefault<unique_ptr<ParsedExpression>>(102, "expression", unique_ptr<ParsedExpression>());
	auto category = deserializer.ReadProperty<TableColumnType>(103, "category");
	ColumnDefinition result(std::move(name), std::move(type), std::move(expression), category);
	deserializer.ReadProperty(104, "compression_type", result.compression_type);
	return result;
}

void ColumnInfo::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "names", names);
	serializer.WriteProperty(101, "types", types);
}

ColumnInfo ColumnInfo::Deserialize(Deserializer &deserializer) {
	ColumnInfo result;
	deserializer.ReadProperty(100, "names", result.names);
	deserializer.ReadProperty(101, "types", result.types);
	return result;
}

void ColumnList::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "columns", columns);
}

ColumnList ColumnList::Deserialize(Deserializer &deserializer) {
	auto columns = deserializer.ReadProperty<vector<ColumnDefinition>>(100, "columns");
	ColumnList result(std::move(columns));
	return result;
}

void CommonTableExpressionInfo::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "aliases", aliases);
	serializer.WriteProperty(101, "query", query);
	serializer.WriteProperty(102, "materialized", materialized);
}

unique_ptr<CommonTableExpressionInfo> CommonTableExpressionInfo::Deserialize(Deserializer &deserializer) {
	auto result = duckdb::unique_ptr<CommonTableExpressionInfo>(new CommonTableExpressionInfo());
	deserializer.ReadProperty(100, "aliases", result->aliases);
	deserializer.ReadProperty(101, "query", result->query);
	deserializer.ReadProperty(102, "materialized", result->materialized);
	return result;
}

void CommonTableExpressionMap::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "map", map);
}

CommonTableExpressionMap CommonTableExpressionMap::Deserialize(Deserializer &deserializer) {
	CommonTableExpressionMap result;
	deserializer.ReadProperty(100, "map", result.map);
	return result;
}

void HivePartitioningIndex::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "value", value);
	serializer.WriteProperty(101, "index", index);
}

HivePartitioningIndex HivePartitioningIndex::Deserialize(Deserializer &deserializer) {
	auto value = deserializer.ReadProperty<string>(100, "value");
	auto index = deserializer.ReadProperty<idx_t>(101, "index");
	HivePartitioningIndex result(std::move(value), index);
	return result;
}

void JoinCondition::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "left", left);
	serializer.WriteProperty(101, "right", right);
	serializer.WriteProperty(102, "comparison", comparison);
}

JoinCondition JoinCondition::Deserialize(Deserializer &deserializer) {
	JoinCondition result;
	deserializer.ReadProperty(100, "left", result.left);
	deserializer.ReadProperty(101, "right", result.right);
	deserializer.ReadProperty(102, "comparison", result.comparison);
	return result;
}

void LogicalType::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "id", id_);
	serializer.WritePropertyWithDefault(101, "type_info", type_info_, shared_ptr<ExtraTypeInfo>());
}

LogicalType LogicalType::Deserialize(Deserializer &deserializer) {
	auto id = deserializer.ReadProperty<LogicalTypeId>(100, "id");
	auto type_info = deserializer.ReadPropertyWithDefault<shared_ptr<ExtraTypeInfo>>(101, "type_info", shared_ptr<ExtraTypeInfo>());
	LogicalType result(id, std::move(type_info));
	return result;
}

void MultiFileReaderBindData::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "filename_idx", filename_idx);
	serializer.WriteProperty(101, "hive_partitioning_indexes", hive_partitioning_indexes);
}

MultiFileReaderBindData MultiFileReaderBindData::Deserialize(Deserializer &deserializer) {
	MultiFileReaderBindData result;
	deserializer.ReadProperty(100, "filename_idx", result.filename_idx);
	deserializer.ReadProperty(101, "hive_partitioning_indexes", result.hive_partitioning_indexes);
	return result;
}

void MultiFileReaderOptions::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "filename", filename);
	serializer.WriteProperty(101, "hive_partitioning", hive_partitioning);
	serializer.WriteProperty(102, "auto_detect_hive_partitioning", auto_detect_hive_partitioning);
	serializer.WriteProperty(103, "union_by_name", union_by_name);
	serializer.WriteProperty(104, "hive_types_autocast", hive_types_autocast);
	serializer.WriteProperty(105, "hive_types_schema", hive_types_schema);
}

MultiFileReaderOptions MultiFileReaderOptions::Deserialize(Deserializer &deserializer) {
	MultiFileReaderOptions result;
	deserializer.ReadProperty(100, "filename", result.filename);
	deserializer.ReadProperty(101, "hive_partitioning", result.hive_partitioning);
	deserializer.ReadProperty(102, "auto_detect_hive_partitioning", result.auto_detect_hive_partitioning);
	deserializer.ReadProperty(103, "union_by_name", result.union_by_name);
	deserializer.ReadProperty(104, "hive_types_autocast", result.hive_types_autocast);
	deserializer.ReadProperty(105, "hive_types_schema", result.hive_types_schema);
	return result;
}

void OrderByNode::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "type", type);
	serializer.WriteProperty(101, "null_order", null_order);
	serializer.WriteProperty(102, "expression", expression);
}

OrderByNode OrderByNode::Deserialize(Deserializer &deserializer) {
	auto type = deserializer.ReadProperty<OrderType>(100, "type");
	auto null_order = deserializer.ReadProperty<OrderByNullType>(101, "null_order");
	auto expression = deserializer.ReadProperty<unique_ptr<ParsedExpression>>(102, "expression");
	OrderByNode result(type, null_order, std::move(expression));
	return result;
}

void PivotColumn::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "pivot_expressions", pivot_expressions);
	serializer.WriteProperty(101, "unpivot_names", unpivot_names);
	serializer.WriteProperty(102, "entries", entries);
	serializer.WriteProperty(103, "pivot_enum", pivot_enum);
}

PivotColumn PivotColumn::Deserialize(Deserializer &deserializer) {
	PivotColumn result;
	deserializer.ReadProperty(100, "pivot_expressions", result.pivot_expressions);
	deserializer.ReadProperty(101, "unpivot_names", result.unpivot_names);
	deserializer.ReadProperty(102, "entries", result.entries);
	deserializer.ReadProperty(103, "pivot_enum", result.pivot_enum);
	return result;
}

void PivotColumnEntry::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "values", values);
	serializer.WritePropertyWithDefault(101, "star_expr", star_expr, unique_ptr<ParsedExpression>());
	serializer.WriteProperty(102, "alias", alias);
}

PivotColumnEntry PivotColumnEntry::Deserialize(Deserializer &deserializer) {
	PivotColumnEntry result;
	deserializer.ReadProperty(100, "values", result.values);
	deserializer.ReadPropertyWithDefault(101, "star_expr", result.star_expr, unique_ptr<ParsedExpression>());
	deserializer.ReadProperty(102, "alias", result.alias);
	return result;
}

void ReadCSVData::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "files", files);
	serializer.WriteProperty(101, "csv_types", csv_types);
	serializer.WriteProperty(102, "csv_names", csv_names);
	serializer.WriteProperty(103, "return_types", return_types);
	serializer.WriteProperty(104, "return_names", return_names);
	serializer.WriteProperty(105, "filename_col_idx", filename_col_idx);
	serializer.WriteProperty(106, "options", options);
	serializer.WriteProperty(107, "single_threaded", single_threaded);
	serializer.WriteProperty(108, "reader_bind", reader_bind);
	serializer.WriteProperty(109, "column_info", column_info);
}

unique_ptr<ReadCSVData> ReadCSVData::Deserialize(Deserializer &deserializer) {
	auto result = duckdb::unique_ptr<ReadCSVData>(new ReadCSVData());
	deserializer.ReadProperty(100, "files", result->files);
	deserializer.ReadProperty(101, "csv_types", result->csv_types);
	deserializer.ReadProperty(102, "csv_names", result->csv_names);
	deserializer.ReadProperty(103, "return_types", result->return_types);
	deserializer.ReadProperty(104, "return_names", result->return_names);
	deserializer.ReadProperty(105, "filename_col_idx", result->filename_col_idx);
	deserializer.ReadProperty(106, "options", result->options);
	deserializer.ReadProperty(107, "single_threaded", result->single_threaded);
	deserializer.ReadProperty(108, "reader_bind", result->reader_bind);
	deserializer.ReadProperty(109, "column_info", result->column_info);
	return result;
}

void SampleOptions::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "sample_size", sample_size);
	serializer.WriteProperty(101, "is_percentage", is_percentage);
	serializer.WriteProperty(102, "method", method);
	serializer.WriteProperty(103, "seed", seed);
}

unique_ptr<SampleOptions> SampleOptions::Deserialize(Deserializer &deserializer) {
	auto result = duckdb::unique_ptr<SampleOptions>(new SampleOptions());
	deserializer.ReadProperty(100, "sample_size", result->sample_size);
	deserializer.ReadProperty(101, "is_percentage", result->is_percentage);
	deserializer.ReadProperty(102, "method", result->method);
	deserializer.ReadProperty(103, "seed", result->seed);
	return result;
}

void StrpTimeFormat::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "format_specifier", format_specifier);
}

StrpTimeFormat StrpTimeFormat::Deserialize(Deserializer &deserializer) {
	auto format_specifier = deserializer.ReadProperty<string>(100, "format_specifier");
	StrpTimeFormat result(format_specifier);
	return result;
}

void TableFilterSet::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "filters", filters);
}

TableFilterSet TableFilterSet::Deserialize(Deserializer &deserializer) {
	TableFilterSet result;
	deserializer.ReadProperty(100, "filters", result.filters);
	return result;
}

void VacuumOptions::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(100, "vacuum", vacuum);
	serializer.WriteProperty(101, "analyze", analyze);
}

VacuumOptions VacuumOptions::Deserialize(Deserializer &deserializer) {
	VacuumOptions result;
	deserializer.ReadProperty(100, "vacuum", result.vacuum);
	deserializer.ReadProperty(101, "analyze", result.analyze);
	return result;
}

void interval_t::Serialize(Serializer &serializer) const {
	serializer.WriteProperty(1, "months", months);
	serializer.WriteProperty(2, "days", days);
	serializer.WriteProperty(3, "micros", micros);
}

interval_t interval_t::Deserialize(Deserializer &deserializer) {
	interval_t result;
	deserializer.ReadProperty(1, "months", result.months);
	deserializer.ReadProperty(2, "days", result.days);
	deserializer.ReadProperty(3, "micros", result.micros);
	return result;
}

} // namespace duckdb
