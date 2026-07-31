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
echo "   who drank how many beers in which room:"
echo "     a.csv    id 1 alice  room 10  0     id 2 bob    room 50  1"
echo "              id 3 carol  room 90  3     id 103 frank room 50  2"
echo "     b.csv    id 101 dave room 20  2     id 102 erin  room 60  3"
echo "              id 103 frank room 100 1    id 2 bob     room 60  2"
echo
echo "   bob and frank each appear in both files, so no column's range separates them: a holds ids"
echo "   1..103 and rooms 10..90, b holds ids 2..103 and rooms 20..100. Bounds - parquet's own included -"
echo "   can rule out almost nothing here. A record of which (id, room_id) pairs occur still can."

# The beers summed over one source for one (id, room_id), plus what the scan had to open to answer it.
# The source is SQL, not a path, so it can be a read_parquet over both files.
beers() {
    local source=$1 where=$2
    local beers files
    beers=$($duckdb -noheader -list -c "SELECT coalesce(sum(beer_count), 0) FROM $source WHERE $where")
    files=$($duckdb -c "EXPLAIN ANALYZE SELECT sum(beer_count) FROM $source WHERE $where" 2>&1 |
        grep -oE "Total Files Read: [0-9]+|Files Skipped: [0-9]+" | paste -sd', ' -)
    printf "%s beers, %s\n" "$beers" "$files"
}

echo
row() { printf "   %-12s %-20s %s\n" "$1" "$2" "$(beers "$3" "$4")"; }
printf "   %-12s %-20s %s\n" module question answer
both_maxtrix="['$work/a_min_maxtrix_f8.parquet','$work/b_min_maxtrix_f8.parquet']"
both_minmax="['$work/a_min_max_f8.parquet','$work/b_min_max_f8.parquet']"

row min_maxtrix "carol in room 90"  "read_parquet($both_maxtrix)" "id = 3 AND room_id = 90"
row min_max     "carol in room 90"  "read_parquet($both_minmax)"  "id = 3 AND room_id = 90"
row min_maxtrix "bob in room 90"    "read_parquet($both_maxtrix)" "id = 2 AND room_id = 90"
row min_maxtrix "bob in room 60"    "read_parquet($both_maxtrix)" "id = 2 AND room_id = 60"
echo
echo "   Carol is in a and nowhere near b, but b's id and room ranges both cover her, so bounds have to"
echo "   open both files to answer. The matrix opens one: b holds no (3, 90) pair. Same 3 beers either way."
echo
echo "   The third row is a pair in neither file, so both are skipped. The fourth is bob in room 60, which"
echo "   only b holds - so a is skipped and b is read, the opposite way round."
