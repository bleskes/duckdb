# f8 tools

Everything needed to produce an f8 parquet file from outside the extension. The extension itself
only *reads*: it has no function that builds metadata and it ships no module, so these tools are the
supported way to make the two blobs a file carries.

```
   data.csv ──► <a metadata generator> ─────────► metadata.bin ─┐
                                                                ├─► embed_f8_in_parquet ─► enhanced.parquet
   module.c ──► build_f8_module ────────────────► module.wasm ──┘                       └─► plain.parquet
```

Generic, usable with any module and metadata:

| tool | does |
| --- | --- |
| `embed_f8_in_parquet` | CSV → parquet, with or without the two blobs embedded |
| `build_f8_module` | compiles a C source file → wasm module |

Examples, each specific to the dialect its own module implements:

| tool | does |
| --- | --- |
| `../examples/min_max/f8_min_max_metadata_generator` | CSV → bounds per integer column |
| `../examples/min_maxtrix/f8_min_maxtrix_metadata_generator` | the same, plus a bit matrix per narrow column pair |

The `min_maxtrix` *module* reads only the matrices, so it answers conjunctions and nothing else; its
generator still writes the bounds, because the same blob is what `min_max` reads.
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

The example modules are freestanding C (`../examples/min_max/f8_min_max_module.c` is the shorter one, about
140 lines). **Nothing about the
ABI is language specific** - four exported functions and an exported memory, no imports - so C++,
Zig, Rust, AssemblyScript, TinyGo or hand-written WAT work equally well.

```
f8_metadata_buffer()   -> u32   address of a scratch buffer the host writes the metadata into
f8_metadata_capacity() -> u32   its size, so the host can bounds-check before writing
f8_can_skip_equal_i64(column_index: u32, metadata_len: u32, value: i64) -> u32
                              1 = no row of that column can equal value, so skip the file
                              0 = must read
f8_conj_i64(metadata_len: u32, lhs_type: u32, lhs_column_index: u32, lhs_value: i64,
                               rhs_type: u32, rhs_column_index: u32, rhs_value: i64) -> u32
                              the same answer for two terms that must both hold
```

Both are required. A lone `WHERE i = 1` takes the first directly; `WHERE a = 2 AND b = 1002` takes the
second, so a module can answer from metadata that only means something jointly - a bloom filter over
`(a, b)` pairs, say. A term's type is 0 for "no term on this side" or 1 for equality, and a type a module
does not know must be answered "no information" rather than guessed at.

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

EXPORT("f8_conj_i64")
unsigned f8_conj_i64(unsigned len, unsigned lhs_type, unsigned lhs_column, long long lhs_value,
                                   unsigned rhs_type, unsigned rhs_column, long long rhs_value) {
    // both terms must hold, so either one matching no row is enough
    return (term(len, lhs_type, lhs_column, lhs_value) || term(len, rhs_type, rhs_column, rhs_value))
               ? 1u : 0u;
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

The metadata format is entirely yours, and the two examples here differ only in it: `min_max` writes
bounds, `min_maxtrix` writes bounds plus a bit matrix per column pair, and each ships the writer for what
its module reads. A bloom filter or a bounding box module would do the same, and the engine would not
change.

## Metadata layout used here ("F8S1")

A sparse map, so bounding one column of fifty costs one entry:

```
0..4    magic "F8S1"
4..8    entry count, little endian
8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
```

A column with no entry has no bounds and is never skipped, which is also how a non-integer column is
represented. Both generators leave out anything they cannot bound.

Then, optionally, a bit matrix per pair of columns spanning fewer than 256 values each - which is the
half a per-column statistic cannot do:

```
0..4    magic "F8M1"
4..8    x column index u32      12..20  x_min i64      28..30  width  u16
8..12   y column index u32      20..28  y_min i64      30..32  height u16
32..    ceil(width * height / 8) bytes; bit (dy * width + dx) set = that pair occurs in some row
```

Given rows `(1,1001) (2,1002) (3,1003)`, `a = 2 AND b = 1003` is inside both columns' bounds but in no
row. Only the matrix can prove that, and only if it is asked about both terms at once.

Matrices come last so a module that knows only about bounds still works: the entry count fixes where the
bounds section ends and the rest is simply not read.

## Checking a file

```sql
SELECT key::VARCHAR, octet_length(value) FROM parquet_kv_metadata('enhanced.parquet');

-- evaluate a module and metadata directly, without a scan
SELECT f8_can_skip_equal(
    (SELECT content FROM read_blob('module.wasm')),
    (SELECT content FROM read_blob('metadata.bin')), 0, 42);

-- the same for two terms that must both hold: type 1 is equality, 0 is "no term on this side"
SELECT f8_can_skip_conj(
    (SELECT content FROM read_blob('module.wasm')),
    (SELECT content FROM read_blob('metadata.bin')), 1, 0, 42, 1, 1, 1002);

SET f8_enabled = false;   -- turn skipping off to check an answer is unchanged
```
