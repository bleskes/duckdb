#
# Build config that adds the in-tree `anyblox` extension on top of the default
# in-tree extension set. Requires the AnyBloxCpp library on CMAKE_PREFIX_PATH.
#
# Build with:
#   EXTENSION_CONFIGS=.github/config/anyblox_extensions.cmake \
#   CMAKE_PREFIX_PATH=<anyblox-install-prefix> GEN=ninja make reldebug
#

duckdb_extension_load(autocomplete)
duckdb_extension_load(core_functions)
duckdb_extension_load(icu)
duckdb_extension_load(json)
duckdb_extension_load(parquet)

# AnyBlox integration (in-tree at extension/anyblox)
duckdb_extension_load(anyblox LOAD_TESTS)
