#pragma once

#include "duckdb_python/pybind11/pybind_wrapper.hpp"
#include "duckdb/common/arrow/arrow_options.hpp"

namespace duckdb {

namespace pyarrow {

py::object ToArrowTable(const vector<LogicalType> &types, const vector<string> &names, const string &timezone_config,
                        py::list batches, ArrowOptions options);

} // namespace pyarrow

} // namespace duckdb
