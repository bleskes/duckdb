//===----------------------------------------------------------------------===//
//                         DuckDB
//
// planner/expression_binder/order_binder.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "common/unordered_map.hpp"
#include "parser/expression_map.hpp"
#include "parser/parsed_expression.hpp"

namespace duckdb {
class Expression;
class SelectNode;

//! The ORDER binder is responsible for binding an expression within the ORDER BY clause of a SQL statement
class OrderBinder {
public:
	OrderBinder(size_t projection_index, SelectNode &node, unordered_map<string, uint32_t> &alias_map,
	            expression_map_t<uint32_t> &projection_map);

	unique_ptr<Expression> Bind(unique_ptr<ParsedExpression> expr);

private:
	unique_ptr<Expression> CreateProjectionReference(ParsedExpression &expr, size_t index);

	size_t projection_index;
	SelectNode &node;
	unordered_map<string, uint32_t> &alias_map;
	expression_map_t<uint32_t> &projection_map;
};

} // namespace duckdb
