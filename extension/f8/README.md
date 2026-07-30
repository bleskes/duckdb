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

The example module is C, and deliberately so: the wasm ABI is language agnostic, and the engine has
no opinion about what produced the module it runs. Only the host that *runs* modules is Rust, because
it embeds wasmtime.

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
tools/build_f8_module examples/min_max/f8_min_max_module.c module.wasm
examples/min_max/f8_min_max_metadata_generator data.csv metadata.bin
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
| `examples/min_max/f8_min_max_module.c` | the example filter module: freestanding C, no SIMD, imports nothing |
| `examples/regenerate-fixtures.sh` | rewrites `test/data` using only the tools |
| `tools/` | `build_f8_module` and `embed_f8_in_parquet`, both usable with any module and metadata |
| `runtime/rust/` | the wasm engine: wasmtime and nothing else |
| `runtime/include/` | `f8.hpp` C++ wrapper over the hand-written C header |
| `f8_module_runner.cpp` | runs modules: the skip provider and the `f8_can_skip_equal` scalar |

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
```

The value travels in a register, so only the metadata crosses into linear memory.

The metadata this particular module understands is a sparse map from column index to bounds, so
metadataing one column of fifty costs one entry:

```text
0..4    magic "F8S1"
4..8    entry count, little endian
8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
```

A column with no entry has no bounds and is never skipped, which is also how a column of an
unsupported type is represented.

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

The committed fixtures - `test/data/f8_min_max_module.wasm`, the metadata blobs and the plain parquet files -
are produced by the tools, so tests need no wasm toolchain:

```bash
cd examples
./regenerate-fixtures.sh
```

The module is 433 bytes, which matters: it is small enough to sit in a parquet footer without the
footer bloat a full decoder module would cause.
