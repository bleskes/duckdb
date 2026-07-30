#!/usr/bin/env bash
# f8 demo: walks the whole chain on a sample CSV and shows a parquet file being skipped.
#
#     EXTENSION_CONFIGS=.github/config/f8_extensions.cmake make reldebug
#     ./run-demo.sh
#
# Run from the repo root - every path here is relative to it, including the duckdb it uses.
#
# The three build steps need no f8, and neither does reading the file: producing an f8 file only uses
# stock parquet KV metadata. Only the skipping queries at the end do.
set -euo pipefail

duckdb=build/reldebug/duckdb
tools=extension/f8/tools
examples=extension/f8/examples

[[ -x $duckdb ]] || {
    echo "$duckdb not found - build it with:" >&2
    echo "    EXTENSION_CONFIGS=.github/config/f8_extensions.cmake make reldebug" >&2
    exit 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "== 1. compile the filter module =="
$tools/build_f8_module $examples/min_max/f8_min_max_module.c "$work/f8_min_max_module.wasm"

echo
echo "== 2. metadata each CSV =="
$examples/min_max/f8_min_max_metadata_generator $examples/a.csv "$work/a.bin"
$examples/min_max/f8_min_max_metadata_generator $examples/b.csv "$work/b.bin"

echo
echo "== 3. write parquet: a.parquet as usual, a_f8.parquet with the blobs attached =="
echo "   the tool takes any vanilla duckdb (\$F8_DUCKDB or PATH) - writing needs no f8"
$tools/embed_f8_in_parquet $examples/a.csv "$work/a.parquet"
$tools/embed_f8_in_parquet $examples/a.csv "$work/a_f8.parquet" \
    --module "$work/f8_min_max_module.wasm" --metadata "$work/a.bin"
$tools/embed_f8_in_parquet $examples/b.csv "$work/b_f8.parquet" \
    --module "$work/f8_min_max_module.wasm" --metadata "$work/b.bin"

echo
echo "== 4. a_f8.parquet is still an ordinary parquet file =="
echo "   f8_enabled = false stands in for a reader that has never heard of f8"
$duckdb -c "
SET f8_enabled = false;
SELECT 'rows in a_f8.parquet' AS check, count(*) AS value FROM '$work/a_f8.parquet'
UNION ALL SELECT 'rows in a.parquet', count(*) FROM '$work/a.parquet'
UNION ALL SELECT 'extra bytes for module+metadata',
    $(wc -c < "$work/a_f8.parquet") - $(wc -c < "$work/a.parquet");
SELECT key::VARCHAR AS kv_key, octet_length(value) AS bytes
FROM parquet_kv_metadata('$work/a_f8.parquet') ORDER BY 1;"

echo "== 5. querying both f8 files: id lives only in b, so a_f8 is skipped =="
$duckdb -c "
SELECT 'id = 102, skipping on' AS query, count(*) AS rows
FROM read_parquet(['$work/a_f8.parquet','$work/b_f8.parquet']) WHERE id = 102;
SET f8_enabled = false;
SELECT 'id = 102, skipping off (same answer, more IO)' AS query, count(*) AS rows
FROM read_parquet(['$work/a_f8.parquet','$work/b_f8.parquet']) WHERE id = 102;"

echo "== 6. proof the file was never opened: the scan's own file count =="
$duckdb -c "
EXPLAIN ANALYZE SELECT * FROM read_parquet(['$work/a_f8.parquet','$work/b_f8.parquet'])
WHERE id = 102;"
