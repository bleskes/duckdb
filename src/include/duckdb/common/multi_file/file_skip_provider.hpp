//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/multi_file/file_skip_provider.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/reference_map.hpp"
#include "duckdb/main/extension_callback_manager.hpp"

namespace duckdb {

class ClientContext;
class TableFilter;
struct DBConfig;

//! Static information a provider is registered with, e.g. handles it needs at scan time.
struct FileSkipProviderInfo {
	virtual ~FileSkipProviderInfo() {
	}
};

//! Everything a provider gets to decide with. Deliberately format-agnostic: the file's own
//! key-value metadata is handed over as opaque strings, so a provider can interpret metadata that
//! neither the core nor the file reader knows anything about.
struct FileSkipProviderInput {
	ClientContext &context;
	//! Key-value metadata read from the file itself, via BaseFileReader::TryGetFileKeyValueMetadata.
	const case_insensitive_map_t<string> &file_metadata;
	//! Filters that apply to this file, keyed by the file's own 0-based column order index.
	//!
	//! Core has already resolved the projection and any global-to-local column mapping, so a
	//! provider can index the file's metadata directly and does not have to know about virtual
	//! columns, hive partitions or renamed columns. Empty when the query has no filters - a
	//! provider is still called then, so that it can report metadata that is malformed.
	const map<idx_t, const_reference<TableFilter>> &filters;
	//! Path of the file being considered, for diagnostics.
	const string &file_path;
	optional_ptr<FileSkipProviderInfo> info;
};

//! Returns true when the file provably contains no row matching the filters, so it can be skipped
//! without being read.
//!
//! This may only ever *prune*: the engine still applies the filters to every row it does read, so
//! answering false is always safe. A provider that cannot make sense of a file's metadata should
//! answer false rather than fail the query, unless the metadata is malformed in a way worth
//! reporting.
typedef bool (*file_can_skip_function_t)(FileSkipProviderInput &input);

//! Lets an extension decide that a whole file can be skipped, based on metadata the file carries
//! itself. Registered at extension load time and consulted per file, once the file's metadata has
//! been read and the query's filters are known.
class FileSkipProvider {
public:
	//! Name of the provider, for diagnostics.
	string name;
	file_can_skip_function_t can_skip_function = nullptr;
	shared_ptr<FileSkipProviderInfo> provider_info;

	DUCKDB_API static void Register(DBConfig &config, FileSkipProvider provider);
	static ExtensionCallbackIteratorHelper<FileSkipProvider> Iterate(ClientContext &context) {
		return ExtensionCallbackManager::Get(context).FileSkipProviders();
	}
	static bool Any(ClientContext &context) {
		return ExtensionCallbackManager::Get(context).HasFileSkipProviders();
	}
};

} // namespace duckdb
