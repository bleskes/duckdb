#include "anyblox_extension.hpp"

#include "anyblox_scan.hpp"

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {
namespace {

void LoadInternal(ExtensionLoader &loader) {
	loader.RegisterFunction(GetAnyBloxTableFunction());

	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption(ANYBLOX_CONFIG_WASM_VIRTUAL_MEMORY_LIMIT,
	                          "Upper limit for all virtual allocations performed by the wasm runtime",
	                          LogicalTypeId::BIGINT, Value(ANYBLOX_DEFAULT_VIRTUAL_MEMORY_LIMIT));
	config.AddExtensionOption(ANYBLOX_CONFIG_WASM_CACHE_LIMIT, "Upper limit for the wasm compiled decoder cache",
	                          LogicalTypeId::BIGINT, Value(ANYBLOX_DEFAULT_WASM_CACHE_LIMIT));
	config.AddExtensionOption(ANYBLOX_CONFIG_COMPILE_WITH_DEBUG, "Whether to compile wasm decoders with debuginfo",
	                          LogicalTypeId::BOOLEAN, Value(false));
	config.AddExtensionOption(
	    ANYBLOX_CONFIG_VALIDATE_UTF8,
	    "Whether to check string columns returned from decoders are valid UTF8. WARNING: disabling this is unsafe, "
	    "untrusted wasm decoders can cause memory security vulnerabilities with invalid strings.",
	    LogicalTypeId::BOOLEAN, Value(true));
}

} // namespace

void AnybloxExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string AnybloxExtension::Name() {
	return "anyblox";
}

std::string AnybloxExtension::Version() const {
#ifdef EXT_VERSION_ANYBLOX
	return EXT_VERSION_ANYBLOX;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(anyblox, loader) {
	duckdb::LoadInternal(loader);
}
}
