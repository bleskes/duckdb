#!/usr/bin/env bash
# Regenerates the committed test fixtures in ../test/data, using only the tools in ../tools.
#
# a and b are the demo CSVs in this directory, doubling as fixtures. two and wide exist only to drive
# tests - per-column bounds and sparse metadata - so they live next to the fixtures they produce.
#
# Needs a vanilla duckdb on PATH, or $F8_DUCKDB pointing at one - no f8 required to write these files.
#
# The point of generating them this way is that the fixtures the tests run against are produced by
# the same path a user would take, rather than by something that only exists inside the test.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tools="$here/../tools"
data="$here/../test/data"

echo "== building the filter module =="
"$tools/build_f8_module" "$here/min_max/f8_min_max_module.c" "$data/f8_min_max_module.wasm"

# Only plain parquet files are committed. The tests embed the module and metadata themselves, which is
# what exercises the read path against bytes it did not create.
for csv in "$here/a.csv" "$here/b.csv" "$data/two.csv" "$data/wide.csv"; do
    name="$(basename "$csv" .csv)"
    echo
    echo "== $name =="
    "$here/min_max/f8_min_max_metadata_generator" "$csv" "$data/metadata_$name.bin"
    "$tools/embed_f8_in_parquet" "$csv" "$data/plain_$name.parquet"
done

echo
echo "== fixtures =="
ls -l "$data" | awk 'NR>1 {printf "  %8d  %s\n", $5, $9}'
