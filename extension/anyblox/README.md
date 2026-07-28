# AnyBlox extension

In-tree port of [duckdb-anyblox](https://github.com/AnyBlox/duckdb-anyblox), which
reads AnyBlox bundles (self-describing datasets carrying their own WebAssembly
decoder) through an `anyblox` table function:

```sql
SELECT * FROM anyblox('dataset.anyblox');
SELECT * FROM anyblox('decoder.anyblox', data => 'dataset.bin');
```

Upstream targets a December 2024 DuckDB commit (the v1.2.0 development line); this
copy is ported to the current API - `Extension::Load` takes an `ExtensionLoader`,
`ExtensionUtil` is gone, and the C entry point is `DUCKDB_CPP_EXTENSION_ENTRY`.

## Layout

| File | Depends on |
| --- | --- |
| `anyblox_extension.cpp` | DuckDB only - registers the function and the settings |
| `anyblox_scan.cpp` | AnyBloxCpp + Arrow C++ - decodes bundles |
| `anyblox_scan_stub.cpp` | DuckDB only - same interface, binding throws |

`CMakeLists.txt` runs `find_package(AnyBloxCpp QUIET)` and compiles the real scan
when it is found, the stub otherwise. That keeps the extension buildable and its
registration testable without the AnyBlox runtime, which is a large Rust +
WebAssembly + Arrow C++ stack.

## Building without the runtime

```bash
EXTENSION_CONFIGS=.github/config/anyblox_extensions.cmake GEN=ninja make reldebug
build/reldebug/test/unittest "*extension/anyblox/test/sql/*"
```

`anyblox_load.test` passes; `anyblox.test` is skipped (it needs `ANYBLOX_RUNTIME`).

## Building with the runtime

The runtime lives in [AnyBlox/anyblox](https://github.com/AnyBlox/anyblox) and is
built in two steps: a Rust static library, then the C++ wrapper that DuckDB links
against.

Prerequisites: Arrow C++ and OpenSSL (`brew install apache-arrow openssl@3`) plus a
nightly Rust toolchain - `vortex-error` uses `#![feature(error_generic_member_access)]`,
so stable will not do, and the repo pins `channel = "nightly"` in
`rust-toolchain.toml`. Note that a Homebrew-installed `cargo` is not a rustup shim,
so put the toolchain's `bin` first on `PATH` rather than relying on `rustup run`.

```bash
git clone --recurse-submodules https://github.com/AnyBlox/anyblox
cd anyblox
export PATH="$HOME/.rustup/toolchains/<nightly>/bin:$PATH"
cargo build -p anyblox-cpplib --release          # -> target/release/libanyblox_ffi.a

ln -sf "$PWD/target/release/libanyblox_ffi.a" anyblox-cpplib/cpp/lib/libanyblox_ffi.a
ln -sf "$PWD/target/release/libanyblox_ffi.a" anyblox-cpplib/cpp/lib/libanyblox_ffid.a
cmake -S anyblox-cpplib/cpp -B build-rel -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=<prefix> -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build-rel --target install
```

Then point DuckDB at the install prefix:

```bash
CMAKE_PREFIX_PATH=<prefix> EXTENSION_CONFIGS=.github/config/anyblox_extensions.cmake \
  GEN=ninja make reldebug
ANYBLOX_RUNTIME=1 build/reldebug/test/unittest "*extension/anyblox/test/sql/*"
```

On arm64 macOS the runtime needs local patches: the `rle_simd_*` reference decoders
and two `#![feature]` attributes are x86-only, one `madvise` call uses a Linux-only
advice flag, and `sysutil::MemFd` uses `memfd_create`. The C++ wrapper also has to
link `CoreFoundation` because the Rust archive pulls in `iana_time_zone`.

## Test data

`test/data/taxpayer-sample.any` is a self-contained bundle holding 1000 rows of
three FSST-compressed string columns, with `test/data/taxpayer-sample.csv` as the
expected output. The `.ignition` samples in the upstream `anyblox-cpplib/cpp/res`
directory predate the current format and are rejected with
`required metadata field is missing: ANYBLOX:VERSION`; this bundle was regenerated
with the current `bundler` from that sample's data:

```bash
cargo build --target wasm32-unknown-unknown --manifest-path ./decompress/Cargo.toml \
      -p taxpayer-fsst --release --features log
cargo build -p anyblox-tools --release --bin bundler
./target/release/bundler --metadata-path <def>.toml \
      --wasm-path ./decompress/target/wasm32-unknown-unknown/release/taxpayer_fsst.wasm \
      --data-path ./anyblox-cpplib/cpp/res/taxpayer-sample.bin \
      -o taxpayer-sample.anyblox
```

Note that sqllogictest runs extension tests with the extension directory as the
working directory, so paths in `test/sql/*.test` are relative to
`extension/anyblox`, not to the repository root.

## Caveats

Errors inside the AnyBlox runtime surface as Rust panics that abort the process
rather than as DuckDB exceptions, so tests must avoid feeding it invalid bundles.
