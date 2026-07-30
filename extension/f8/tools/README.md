# f8 tools

Everything needed to produce an f8 parquet file from outside the extension. The extension itself
only *reads*: it has no function that builds metadata and it ships no module, so these tools are the
supported way to make the two blobs a file carries.

```
   data.csv ──► f8_min_max_metadata_generator ──► metadata.bin ─┐
                                                                ├─► embed_f8_in_parquet ─► enhanced.parquet
   module.c ──► build_f8_module ────────────────► module.wasm ──┘                       └─► plain.parquet
```

Generic, usable with any module and metadata:

| tool | does |
| --- | --- |
| `embed_f8_in_parquet` | CSV → parquet, with or without the two blobs embedded |
| `build_f8_module` | compiles a C source file → wasm module |

Example, specific to the min/max dialect that `../examples/min_max/f8_min_max_module.c` implements:

| tool | does |
| --- | --- |
| `../examples/min_max/f8_min_max_metadata_generator` | CSV → `metadata.bin` (min/max per integer column) |
| `../examples/regenerate-fixtures.sh` | rewrites the committed test fixtures from the example CSVs |

The split is the point: **there is no general "make the metadata" tool, and there cannot be.** Metadata
is only meaningful to the module that reads it, so a writer belongs next to its module. Swap in a
bloom filter module and you write a new metadata writer; `embed_f8_in_parquet` is unchanged.

The whole chain, plus queries showing a file being skipped, is `run-demo.sh` at the repo root:

```bash
EXTENSION_CONFIGS=.github/config/f8_extensions.cmake make reldebug
./run-demo.sh
```

## Writing an f8 file needs no f8 extension

`embed_f8_in_parquet` uses only stock DuckDB: `read_blob`, `getvariable`, and the parquet writer's
`KV_METADATA` option. Any DuckDB can write these files - and so can any parquet writer that can set
key-value metadata, in any language. Only *skipping* on them needs the extension.

That is the whole point of the layout: an f8 file is an ordinary parquet file with two extra
metadata entries, and a reader that has never heard of f8 reads it normally.

## The two keys

| key | contents |
| --- | --- |
| `f8_module` | a wasm module implementing the ABI below |
| `f8_metadata` | bytes only that module understands |

Both or neither. A file with one of them is malformed and the extension says so rather than quietly
reading everything.

## Writing your own filter module

The example module is freestanding C (`../examples/min_max/f8_min_max_module.c`, about 120 lines). **Nothing about the
ABI is language specific** - three exported functions and an exported memory, no imports - so C++,
Zig, Rust, AssemblyScript, TinyGo or hand-written WAT work equally well.

```
f8_metadata_buffer()   -> u32   address of a scratch buffer the host writes the metadata into
f8_metadata_capacity() -> u32   its size, so the host can bounds-check before writing
f8_can_skip_equal_i64(column_index: u32, metadata_len: u32, value: i64) -> u32
                              1 = no row of that column can equal value, so skip the file
                              0 = must read
```

`column_index` is the column's 0-based position in the file's own column order. The value arrives in
a register, so only the metadata crosses into wasm memory.

The shape, in C:

```c
// clang --target=wasm32 -nostdlib -O2 -Wl,--no-entry -Wl,--export-memory f.c -o f.wasm
static unsigned char METADATA[4096];
#define EXPORT(name) __attribute__((export_name(name)))

EXPORT("f8_metadata_buffer")   unsigned f8_metadata_buffer(void)   { return (unsigned)(unsigned long)METADATA; }
EXPORT("f8_metadata_capacity") unsigned f8_metadata_capacity(void) { return sizeof(METADATA); }

EXPORT("f8_can_skip_equal_i64")
unsigned f8_can_skip_equal_i64(unsigned column, unsigned len, long long value) {
    // interpret METADATA[0..len) however you like, then:
    return can_prove_no_match ? 1u : 0u;
}
```

**Toolchain note.** A wasm32-capable clang and a `wasm-ld` often come from different places: Apple's
clang has no wasm backend, Homebrew's `llvm` has the backend but ships no `wasm-ld`, and wasi-sdk or
an emsdk checkout has both. `build_f8_module` searches for a workable pair and says what is missing.
The committed module means none of this is needed just to run the tests.

Rules, in order of how badly they bite:

1. **Answer 0 whenever you are not certain.** A wrong 1 silently drops rows. A wrong 0 only costs a
   scan. Unknown metadata version, short buffer, column you know nothing about, inverted range: all 0.
2. **Import nothing.** The host supplies no functions, so `-nostdlib` and no libc. A module with
   imports fails to instantiate.
3. **Never trap.** No panics, no unreachable, no division by zero. Each call also runs under a fuel
   limit, so an infinite loop is bounded but still wasteful.
4. **Freestanding.** No allocator, no syscalls, no threads. SIMD may not load under the host's
   default wasmtime features, so avoid `-msimd128`.

The metadata format is entirely yours. `../examples/min_max/f8_min_max_metadata_generator` writes min/max because that is what the bundled
module reads; a bloom filter or a bounding box module would ship its own writer and the engine would
not change.

## Metadata layout used here ("F8S1")

A sparse map, so metadataing one column of fifty costs one entry:

```
0..4    magic "F8S1"
4..8    entry count, little endian
8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
```

A column with no entry has no bounds and is never skipped, which is also how a non-integer column is
represented. `../examples/min_max/f8_min_max_metadata_generator` leaves out anything it cannot bound.

## Checking a file

```sql
SELECT key::VARCHAR, octet_length(value) FROM parquet_kv_metadata('enhanced.parquet');

-- evaluate a module and metadata directly, without a scan
SELECT f8_can_skip_equal(
    (SELECT content FROM read_blob('module.wasm')),
    (SELECT content FROM read_blob('metadata.bin')), 0, 42);

SET f8_enabled = false;   -- turn skipping off to check an answer is unchanged
```
