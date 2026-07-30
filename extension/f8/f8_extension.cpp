#include "f8_extension.hpp"

#include "f8_module_runner.hpp"

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {
namespace {

void LoadInternal(ExtensionLoader &loader) {
	loader.RegisterFunction(GetF8CanSkipEqualFunction());
	loader.RegisterFunction(GetF8CanSkipConjFunction());

	auto &db = loader.GetDatabaseInstance();
	DBConfig::GetConfig(db).AddExtensionOption(
	    F8_ENABLED_SETTING,
	    "Whether to consult wasm filter modules embedded in data files in order to skip whole files",
	    LogicalTypeId::BOOLEAN, Value(true));
	RegisterF8SkipProvider(db);
}

} // namespace

void F8Extension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string F8Extension::Name() {
	return "f8";
}

std::string F8Extension::Version() const {
#ifdef EXT_VERSION_F8
	return EXT_VERSION_F8;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(f8, loader) {
	duckdb::LoadInternal(loader);
}
}
