# f8 extension

Runs a small WebAssembly module that a data file carries **alongside** its data, to decide
whether the whole file can be skipped for a given predicate. The file stays readable by any
engine: the module and its metadata ride along as extra parquet metadata, and a reader that does not know about
them just reads the data normally.

This is a prototype of "F5: Flexible Filtering for File Formats" (Dagstuhl 26311, group 4).
The idea is that the *file* decides, using whatever index or metadata suits its data, instead of
the engine being limited to the statistics it happens to understand.

The module and the metadata live in the parquet file's key-value metadata, which preserves BLOB
values byte-exact and is parsed with the footer before any column data is read - so the decision
to skip a file can be made without reading any of it.

## What this is *not*

The module never decodes anything - parquet does that. It only answers predicates over the metadata,
which is why the engine here needs no Arrow, no dataset mapping and no custom linear memory.

The example modules are C, and deliberately so: the wasm ABI is language agnostic, and the engine has
no opinion about what produced the module it runs. Only the host that *runs* modules is Rust, because
it embeds wasmtime.

There are two of them, and the pair is the argument for the whole design. `min_max` records bounds per
column, which is what an engine's own statistics already do. `min_maxtrix` records which *combinations*
of two columns occur, and answers only conjunctions - a single value tells a record of pairs nothing. It
prunes `a = 2 AND b = 1003` on a file holding `(1,1001) (2,1002) (3,1003)`: both values are in range, the
combination is in no row, and no per-column statistic can tell.

Keeping them apart is deliberate - one idea per example, and both read the same metadata blob, so which
prunes what is a property of the module rather than of the file or the engine.

## Writing a file that can skip itself

Two key-value entries opt a file in. Both or neither - a file carrying one of them is malformed and
is reported rather than quietly read in full. A file with neither behaves exactly as before.

| key | contents |
| --- | --- |
| `f8_module` | the wasm filter module |
| `f8_metadata` | bytes only that module understands |

**This extension does not produce either blob.** It has no metadata builder and ships no module, so
the engine cannot be the reason a particular metadata dialect wins. Use `tools/` (see
`tools/README.md`), or write your own producer in any language:

```bash
# from extension/f8. Any duckdb will do here - writing needs no f8.
export F8_DUCKDB=../../build/reldebug/duckdb
tools/build_f8_module examples/min_maxtrix/f8_min_maxtrix_module.c module.wasm
examples/min_maxtrix/f8_min_maxtrix_metadata_generator data.csv metadata.bin
tools/embed_f8_in_parquet data.csv enhanced.parquet --module module.wasm --metadata metadata.bin
```

Embedding blobs is stock DuckDB - no f8 needed to *write* a file, only to skip on one:

```sql
SET VARIABLE m = (SELECT content FROM read_blob('module.wasm'));
SET VARIABLE s = (SELECT content FROM read_blob('metadata.bin'));
COPY t TO 'f.parquet' (FORMAT parquet,
    KV_METADATA {f8_module: getvariable('m'), f8_metadata: getvariable('s')});
```

Metadata entries are keyed by the column's 0-based position in the **file's own** column order. Core
resolves the query's projection and any column mapping before the metadata is consulted, so a
projection or `union_by_name` cannot pair a filter with another column's bounds.

Reading needs nothing special - `SELECT ... WHERE i = 42` over those files simply opens fewer of
them. `SET f8_enabled = false` turns skipping off, which is both the escape hatch and the way to
check that skipping has not changed an answer.

## Evaluating a module directly

```sql
-- true means: no row can match, so the file may be skipped.
SELECT f8_can_skip_equal(module, metadata, column_index, value);

-- two terms that must both hold. The skip types are passed raw, so a test can hand a module one it
-- does not know and check that it answers conservatively.
SELECT f8_can_skip_conj(module, metadata, lhs_type, lhs_column, lhs_value,
                                          rhs_type, rhs_column, rhs_value);
```

A `false` answer never means "no matching rows" - it means "not proven", so the caller still has
to read and filter. Only `true` is actionable. This is used to *prune* and never to replace
filtering: DuckDB still applies the predicate to every row it reads, so an over-cautious answer
costs a scan and nothing more.

The converse is worth stating plainly: metadata that *lies* does lose rows. Nothing validates a
metadata against the data it describes, so writing one is a correctness-bearing act. The test suite
exploits this deliberately, using a file whose metadata excludes rows it really contains to prove
the file is never opened.

## How the scan reaches this

Parquet has no idea any of this exists. Core grows three small pieces:

- `BaseFileReader::TryGetFileKeyValueMetadata` - an optional virtual, false by default, which
  `ParquetReader` implements by handing over its key-value metadata.
- `FileSkipProvider` (`duckdb/common/multi_file/file_skip_provider.hpp`) - a registry of
  providers, kept in `ExtensionCallbackRegistry` so reads are lock-free snapshots.
- One consult site in `MultiFileFunction::InitializeReader`, the single point both existing
  `SKIP_READING_FILE` consumers already go through.

Everything that knows about wasm is in `f8_module_runner.cpp`, which registers a provider at
extension load.

## Layout

| Path | What |
| --- | --- |
| `../../run-demo.sh` | the whole chain end to end, at the repo root so its paths are the ones you build with |
| `examples/min_max/` | per-column bounds. Answers a lone equality, and a conjunction term by term |
| `examples/min_maxtrix/` | a bit matrix per column pair. Answers *only* conjunctions - nothing else |
| `examples/regenerate-fixtures.sh` | rewrites `test/data` using only the tools |
| `tools/` | `build_f8_module` and `embed_f8_in_parquet`, both usable with any module and metadata |
| `runtime/rust/` | the wasm engine: wasmtime and nothing else |
| `runtime/include/` | `f8.hpp` C++ wrapper over the hand-written C header |
| `f8_module_runner.cpp` | runs modules: the skip provider and the `f8_can_skip_equal` / `f8_can_skip_conj` scalars |

`CMakeLists.txt` builds `runtime/rust` with `ExternalProject_Add` + cargo, as
`extension/delta` does. Note it does **not** simply take the first `cargo` on `PATH`: wasmtime's
dependencies need `edition2024` (cargo >= 1.85), and an older Homebrew or distro cargo fails
with a message that does not name the real cause, so the build searches rustup's toolchains and
picks the newest cargo that is new enough. If none is, the build fails saying so.

## ABI

The module imports nothing, so instantiating it requires no host functions, and exports:

```text
f8_metadata_buffer()   -> u32   address of the buffer the host writes the metadata into
f8_metadata_capacity() -> u32   its size, so the host can bounds-check
f8_can_skip_equal_i64(column_index: u32, metadata_len: u32, value: i64) -> u32
                                      1 = can skip, 0 = must read
f8_conj_i64(metadata_len: u32,
            lhs_type: u32, lhs_column_index: u32, lhs_value: i64,
            rhs_type: u32, rhs_column_index: u32, rhs_value: i64) -> u32
                                      the same answer for two terms that must both hold
```

Values travel in registers, so only the metadata crosses into linear memory.

`WHERE i = 1` is the common case and takes `f8_can_skip_equal_i64` directly - three arguments, no skip
type to dispatch on. `WHERE a = 2 AND b = 1002` reaches the scan as two entries in the filter map, one
per column, and the extension pairs them into a single `f8_conj_i64` call. More than two predicates go as
*every* pair, not just adjacent ones - which columns a module's joint metadata covers is not something
the engine can guess. A skip type says what a term compares with:

| type | meaning |
| --- | --- |
| 0 | no term on this side |
| 1 | equality |

A module answers "no information" for a type it does not know, so the list can grow without breaking
modules already sitting in files.

Asking about a conjunction in one call is what lets a module answer from metadata that only means
something jointly: a bloom filter over `(a, b)` pairs can prune `a = 2 AND b = 1002` when both values
occur in the file but never in the same row. Per-column bounds cannot, so this module gains nothing
from it - but the ABI has to carry the shape for one that can.

The metadata this particular module understands has two parts. First a sparse map from column index to
bounds, so bounding one column of fifty costs one entry:

```text
0..4    magic "F8S1"
4..8    entry count, little endian
8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
```

A column with no entry has no bounds and is never skipped, which is also how a column of an
unsupported type is represented.

Then, optionally, a **bit matrix** per pair of columns that both span fewer than 256 values - one bit per
possible combination, set when that combination occurs in some row:

```text
0..4    magic "F8M1"
4..8    x column index u32
8..12   y column index u32
12..20  x_min i64
20..28  y_min i64
28..30  width  u16   (x_max - x_min + 1)
30..32  height u16   (y_max - y_min + 1)
32..    ceil(width * height / 8) bytes; bit (dy * width + dx)
```

This is the part per-column statistics cannot imitate. A file holding `(1,1001) (2,1002) (3,1003)` allows
`a = 2` and `b = 1003` individually, so min/max - parquet's own included - must read it to answer
`a = 2 AND b = 1003`. The matrix says that pair is in no row, and the file is never opened.

Matrices come last on purpose: the entry count fixes where the bounds end, so a module that predates
them reads the bounds and ignores the rest. Worst case is 256 x 256 bits, 8 KiB per pair, which is why
the module's buffer is 64 KiB and why a wider pair gets no matrix rather than a huge one.

The metadata format is the *module's* business, not the engine's - the extension never parses it, so
a different module could carry a bloom filter or a geometry bounding box and nothing outside that
module and its metadata writer would change.

## Safety

Everything unexpected answers "must read": bad magic, a short or truncated metadata, a column with no
entry, and an inverted range (which read naively would make every value look skippable). A wrong
"can skip" silently loses rows; a wrong "must read" only costs a scan.

Running a module out of a data file is executing untrusted code. wasmtime sandboxes memory, and
each call gets a fuel limit so a module cannot spin forever. Errors - a malformed module, missing
exports, exhausted fuel - are returned across the C boundary as a tri-state and raised as DuckDB
errors; nothing panics or aborts the process. There is a test for exactly that.

## Rebuilding the fixtures

The committed fixtures - `test/data/f8_min_maxtrix_module.wasm`, the metadata blobs and the plain parquet files -
are produced by the tools, so tests need no wasm toolchain:

```bash
cd examples
./regenerate-fixtures.sh
```

The module is 433 bytes, which matters: it is small enough to sit in a parquet footer without the
footer bloat a full decoder module would cause.
