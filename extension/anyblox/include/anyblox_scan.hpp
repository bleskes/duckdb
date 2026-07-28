//===----------------------------------------------------------------------===//
//                         DuckDB
//
// anyblox_scan.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Extension setting names. Registered by anyblox_extension.cpp, read by the
//! scan implementation when it initializes the AnyBlox runtime.
constexpr const char *ANYBLOX_CONFIG_WASM_VIRTUAL_MEMORY_LIMIT = "wasm_virtual_memory_limit";
constexpr const char *ANYBLOX_CONFIG_WASM_CACHE_LIMIT = "wasm_cache_limit";
constexpr const char *ANYBLOX_CONFIG_COMPILE_WITH_DEBUG = "compile_wasm_with_debug";
constexpr const char *ANYBLOX_CONFIG_VALIDATE_UTF8 = "validate_utf8";
constexpr int64_t ANYBLOX_DEFAULT_VIRTUAL_MEMORY_LIMIT = 16LL * 1024 * 1024 * 1024;
constexpr int64_t ANYBLOX_DEFAULT_WASM_CACHE_LIMIT = 256LL * 1024 * 1024;

//! Returns the `anyblox` table function. Provided either by the AnyBlox-backed
//! implementation (anyblox_scan.cpp, requires the AnyBloxCpp runtime) or by the
//! stub (anyblox_scan_stub.cpp) in builds without it.
TableFunction GetAnyBloxTableFunction();

//! Whether this build has the AnyBlox runtime linked in. False for stub builds,
//! where scanning a bundle throws NotImplementedException.
bool AnyBloxRuntimeAvailable();

} // namespace duckdb
