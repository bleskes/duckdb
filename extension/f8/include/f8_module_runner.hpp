//===----------------------------------------------------------------------===//
//                         DuckDB
//
// f8_module_runner.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

// The parquet key-value metadata entries this extension reads. A file carrying neither is left
// alone; carrying only one of them is an error.
//
// Note what is deliberately absent: the extension has no idea what is *inside* either blob. The
// module is opaque code and the metadata is a private contract between whoever wrote it and whoever
// wrote the module, so a module using bloom filters or bounding boxes needs no change here.
// Producing these blobs is the job of the tools in tools/, not of the engine.
//
// Namespace scope rather than class statics because the tree is built as C++11, where a static
// constexpr member needs an out-of-line definition once it is passed by reference.

//! The wasm filter module to run.
constexpr const char *F8_KEY_MODULE = "f8_module";
//! The metadata to hand to that module.
constexpr const char *F8_KEY_METADATA = "f8_metadata";

//! Setting that turns file skipping off, both as an escape hatch and to A/B the read path.
constexpr const char *F8_ENABLED_SETTING = "f8_enabled";

//! `f8_can_skip_equal(module, metadata, column_index, value)` - runs a module directly, without a
//! scan. True means the module proved no row of that column can equal the value.
ScalarFunction GetF8CanSkipEqualFunction();

//! `f8_can_skip_conj(module, metadata, lhs_type, lhs_column, lhs_value, rhs_type, rhs_column, rhs_value)`
//! - the same, for two terms that must both hold. The skip types are passed raw so that a module can be
//! handed one it does not know.
ScalarFunction GetF8CanSkipConjFunction();

//! Registers the file-skipping provider with the database. Format-agnostic: it works for any reader
//! that can hand over the file's key-value metadata.
void RegisterF8SkipProvider(DatabaseInstance &db);

} // namespace duckdb
