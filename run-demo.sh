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

# Kept rather than thrown away, so the files the demo talks about are there to poke at afterwards.
work=extension/f8/build
rm -rf "$work"
mkdir -p "$work"
echo "writing to $work/"

echo
echo "== 1. compile the filter modules =="
echo "   min_max carries bounds per column; min_maxtrix adds a bit matrix per column pair"
$tools/build_f8_module $examples/min_max/f8_min_max_module.c "$work/f8_min_max_module.wasm"
$tools/build_f8_module $examples/min_maxtrix/f8_min_maxtrix_module.c "$work/f8_min_maxtrix_module.wasm"

echo
echo "== 2. metadata each CSV =="
$examples/min_maxtrix/f8_min_maxtrix_metadata_generator $examples/a.csv "$work/a.bin"
$examples/min_maxtrix/f8_min_maxtrix_metadata_generator $examples/b.csv "$work/b.bin"

echo
echo "== 3. write parquet: both CSVs as usual, then again with the blobs attached =="
echo "   the tool takes any vanilla duckdb (\$F8_DUCKDB or PATH) - writing needs no f8"
$tools/embed_f8_in_parquet $examples/a.csv "$work/a.parquet"
$tools/embed_f8_in_parquet $examples/b.csv "$work/b.parquet"
$tools/embed_f8_in_parquet $examples/a.csv "$work/a_min_maxtrix_f8.parquet" \
    --module "$work/f8_min_maxtrix_module.wasm" --metadata "$work/a.bin"
$tools/embed_f8_in_parquet $examples/b.csv "$work/b_min_maxtrix_f8.parquet" \
    --module "$work/f8_min_maxtrix_module.wasm" --metadata "$work/b.bin"
# The same data and the same metadata, read by the module that knows only about bounds.
$tools/embed_f8_in_parquet $examples/a.csv "$work/a_min_max_f8.parquet" \
    --module "$work/f8_min_max_module.wasm" --metadata "$work/a.bin"
$tools/embed_f8_in_parquet $examples/b.csv "$work/b_min_max_f8.parquet" \
    --module "$work/f8_min_max_module.wasm" --metadata "$work/b.bin"

# -echo prints each statement before running it, and one -c per statement keeps that honest: with
# several statements in a single -c the CLI echoes the first one again for each of them. All the -c of
# one invocation share a session, so a SET in one still applies to the next.
both="read_parquet(['$work/a_min_maxtrix_f8.parquet','$work/b_min_maxtrix_f8.parquet'])"
# Computed here rather than inline in the SQL, so the echoed statement shows one number.
extra_bytes=$(( $(wc -c < "$work/a_min_maxtrix_f8.parquet") - $(wc -c < "$work/a.parquet") ))
plain_bytes=$(( $(wc -c < "$work/a.parquet") ))

echo
echo "== 4. an f8 file is still an ordinary parquet file =="
echo "   f8_enabled = false stands in for a reader that has never heard of f8"
echo "   the two blobs add slightly more than their own size: parquet frames each key and value"
$duckdb -echo \
    -c "SET f8_enabled = false" \
    -c "SELECT 'rows in a_min_maxtrix_f8.parquet' AS what, count(*) AS value FROM '$work/a_min_maxtrix_f8.parquet'
UNION ALL SELECT 'rows in a.parquet', count(*) FROM '$work/a.parquet'
UNION ALL SELECT 'bytes of ' || key::VARCHAR, octet_length(value)
    FROM parquet_kv_metadata('$work/a_min_maxtrix_f8.parquet')
UNION ALL SELECT 'bytes of plain a.parquet', $plain_bytes
UNION ALL SELECT 'bytes the two blobs add', $extra_bytes"

echo "== 5. querying both f8 files: id lives only in b, so the a file is skipped =="
$duckdb -echo \
    -c "SELECT 'id = 102, skipping on' AS query, count(*) AS rows FROM $both WHERE id = 102" \
    -c "SET f8_enabled = false" \
    -c "SELECT 'id = 102, skipping off (same answer, more IO)' AS query, count(*) AS rows
FROM $both WHERE id = 102"

echo "== 6. proof the file was never opened: the scan's own file count =="
$duckdb -c "EXPLAIN ANALYZE SELECT * FROM $both WHERE id = 102"

echo "== 7. what the bit matrix buys: a pair no row holds, that no bound can rule out =="
echo "   a.csv is (1,alice,10) (2,bob,50) (3,carol,90). For id = 2 AND cash = 90 both values are in"
echo "   their own column's range, so bounds - parquet's own included - must read the file to answer."
echo "   The matrix records which (id, cash) combinations occur, and that one does not."

# Prints just the scan's file counts for one query against one file.
file_counts() {
    # Matching the text rather than stripping the box drawing around it.
    $duckdb -c "EXPLAIN ANALYZE SELECT * FROM '$1' WHERE $2" 2>&1 |
        grep -oE "Total Files Read: [0-9]+|Files Skipped: [0-9]+" | paste -sd', ' -
}

echo "   module        query                  result"
echo "   min_maxtrix   id = 2 AND cash = 90   $(file_counts "$work/a_min_maxtrix_f8.parquet" "id = 2 AND cash = 90")"
echo "   min_max       id = 2 AND cash = 90   $(file_counts "$work/a_min_max_f8.parquet" "id = 2 AND cash = 90")"
echo "   min_maxtrix   id = 2 AND cash = 50   $(file_counts "$work/a_min_maxtrix_f8.parquet" "id = 2 AND cash = 50")"
echo
echo "   The first two read the same bytes with different modules. The third is a pair that does occur,"
echo "   so it must be read - a matrix that skipped it would be losing rows, not saving reads."
