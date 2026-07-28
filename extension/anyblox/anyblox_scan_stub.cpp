//! Stub scan implementation, compiled when the AnyBloxCpp runtime is not
//! available at configure time. It keeps the extension buildable and testable
//! (function registration, settings, catalog wiring) without pulling in the
//! AnyBlox runtime and Arrow C++. Binding a bundle throws.

#include "anyblox_scan.hpp"

#include "duckdb/common/exception.hpp"

namespace duckdb {
namespace {

unique_ptr<FunctionData> AnyBloxBindStub(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                         vector<string> &) {
	throw NotImplementedException("This DuckDB build was compiled without the AnyBlox runtime, so AnyBlox bundles "
	                              "cannot be read. Rebuild with the AnyBloxCpp library on CMAKE_PREFIX_PATH.");
}

void AnyBloxFunctionStub(ClientContext &, TableFunctionInput &, DataChunk &) {
	throw InternalException("AnyBlox stub scan should never execute - binding always throws");
}

} // namespace

TableFunction GetAnyBloxTableFunction() {
	TableFunction function("anyblox", {LogicalTypeId::VARCHAR}, AnyBloxFunctionStub, AnyBloxBindStub);
	function.named_parameters.insert({"data", LogicalTypeId::VARCHAR});
	return function;
}

bool AnyBloxRuntimeAvailable() {
	return false;
}

} // namespace duckdb
