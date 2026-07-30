#
# Builds DuckDB with the f8 extension, which runs wasm filter modules that data files
# carry alongside their data in order to skip whole files.
#
# to build duckdb with this configuration run:
#   EXTENSION_CONFIGS=.github/config/f8_extensions.cmake make
#
# parquet is needed because the eventual home for this is parquet key-value metadata.

duckdb_extension_load(core_functions)
duckdb_extension_load(parquet)

# In-tree at extension/f8. Declared without SOURCE_DIR so the in-tree path is used;
# passing SOURCE_DIR takes the local-extension branch, which expects headers under src/include.
duckdb_extension_load(f8 LOAD_TESTS)
