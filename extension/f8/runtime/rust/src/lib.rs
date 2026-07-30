//! Evaluates file-skipping predicates by running a wasm filter module that a data file
//! carries alongside its data - for parquet, in the file's key-value metadata.
//!
//! This is deliberately the whole runtime. Filtering needs a wasm engine and nothing else:
//! the module decodes no data, so there is no Arrow, no dataset mapping and no custom linear
//! memory here, and it builds on stable Rust.
//!
//! Two rules the C boundary keeps:
//!
//! * **No panic ever crosses it.** Every entry point catches unwinding and reports an error
//!   instead, so a malformed module cannot take the host process down.
//! * **Failure means "read the file".** Callers get a tri-state, and anything other than a
//!   confident yes must be treated as "cannot skip".

use std::{
    collections::HashMap,
    hash::{Hash, Hasher},
    os::raw::c_char,
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    sync::Mutex,
};
use wasmtime::{Config, Engine, Instance, Module, Store, TypedFunc};

/// Names the module must export. It must import nothing.
const EXPORT_METADATA_BUFFER: &str = "f8_metadata_buffer";
const EXPORT_METADATA_CAPACITY: &str = "f8_metadata_capacity";
const EXPORT_CAN_SKIP_EQUAL_I64: &str = "f8_can_skip_equal_i64";

/// The module's answer.
const GUEST_CAN_SKIP: u32 = 1;

/// Bounds how long a module may run. A predicate over metadata is a handful of loads and
/// compares; anything approaching this is a module that is misbehaving.
const FUEL_PER_CALL: u64 = 10_000_000;

/// Returned to C.
pub const RESULT_MUST_READ: i32 = 0;
pub const RESULT_CAN_SKIP: i32 = 1;
pub const RESULT_ERROR: i32 = -1;

pub struct F8Runtime {
    engine: Engine,
    /// Compiled modules, keyed by a hash of their bytes. Without this every probe would
    /// re-compile the module, which for a per-file filter means once per file.
    modules: Mutex<HashMap<u64, Module>>,
}

impl F8Runtime {
    fn new() -> Result<Self, wasmtime::Error> {
        let mut config = Config::new();
        config.consume_fuel(true);
        Ok(Self {
            engine: Engine::new(&config)?,
            modules: Mutex::new(HashMap::new()),
        })
    }

    fn module(&self, wasm: &[u8]) -> Result<Module, wasmtime::Error> {
        let mut hasher = std::collections::hash_map::DefaultHasher::new();
        wasm.hash(&mut hasher);
        let key = hasher.finish();

        // Compile outside the lock is not worth the complexity here: probes for one file are
        // serialised anyway, and the common case is a hit.
        let mut modules = self.modules.lock().expect("module cache poisoned");
        if let Some(module) = modules.get(&key) {
            return Ok(module.clone());
        }
        let module = Module::new(&self.engine, wasm)?;
        modules.insert(key, module.clone());
        Ok(module)
    }

    fn can_skip_equal_i64(
        &self,
        wasm: &[u8],
        metadata: &[u8],
        column_index: u32,
        value: i64,
    ) -> Result<bool, wasmtime::Error> {
        let module = self.module(wasm)?;
        let mut store = Store::new(&self.engine, ());
        store.set_fuel(FUEL_PER_CALL)?;

        // The module imports nothing, so there is nothing to link.
        let instance = Instance::new(&mut store, &module, &[])?;
        let metadata_buffer: TypedFunc<(), u32> =
            instance.get_typed_func(&mut store, EXPORT_METADATA_BUFFER)?;
        let metadata_capacity: TypedFunc<(), u32> =
            instance.get_typed_func(&mut store, EXPORT_METADATA_CAPACITY)?;
        let can_skip: TypedFunc<(u32, u32, i64), u32> =
            instance.get_typed_func(&mut store, EXPORT_CAN_SKIP_EQUAL_I64)?;

        let capacity = metadata_capacity.call(&mut store, ())? as usize;
        if metadata.len() > capacity {
            // Not an error: metadata this module cannot hold simply cannot be evaluated.
            return Ok(false);
        }
        let offset = metadata_buffer.call(&mut store, ())? as usize;
        let memory = instance
            .get_memory(&mut store, "memory")
            .ok_or_else(|| wasmtime::Error::msg("filter module does not export its memory"))?;
        memory.write(&mut store, offset, metadata)?;

        let answer = can_skip.call(&mut store, (column_index, metadata.len() as u32, value))?;
        Ok(answer == GUEST_CAN_SKIP)
    }
}

/// Runs `body`, turning both errors and panics into an error string for the caller.
fn guard<F>(error_out: *mut *mut c_char, body: F) -> i32
where
    F: FnOnce() -> Result<bool, wasmtime::Error>,
{
    if !error_out.is_null() {
        unsafe { *error_out = ptr::null_mut() };
    }
    let outcome = catch_unwind(AssertUnwindSafe(body));
    let message = match outcome {
        Ok(Ok(true)) => return RESULT_CAN_SKIP,
        Ok(Ok(false)) => return RESULT_MUST_READ,
        Ok(Err(err)) => format!("{err}"),
        Err(panic) => {
            let detail = panic
                .downcast_ref::<&str>()
                .map(|s| (*s).to_string())
                .or_else(|| panic.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "unknown panic".to_string());
            format!("wasm filter panicked: {detail}")
        }
    };
    if !error_out.is_null() {
        let bytes: Vec<u8> = message
            .into_bytes()
            .into_iter()
            .filter(|b| *b != 0)
            .chain(std::iter::once(0))
            .collect();
        unsafe { *error_out = Box::into_raw(bytes.into_boxed_slice()).cast::<c_char>() };
    }
    RESULT_ERROR
}

/// Creates a runtime, or returns null. Owned by the caller: pass it to
/// [`f8_runtime_drop`].
#[no_mangle]
pub extern "C" fn f8_runtime_create() -> *mut F8Runtime {
    match catch_unwind(F8Runtime::new) {
        Ok(Ok(runtime)) => Box::into_raw(Box::new(runtime)),
        _ => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn f8_runtime_drop(runtime: *mut F8Runtime) {
    if !runtime.is_null() {
        drop(Box::from_raw(runtime));
    }
}

/// Frees a message produced by [`f8_can_skip_equal_i64`].
#[no_mangle]
pub unsafe extern "C" fn f8_error_drop(error: *mut c_char) {
    if !error.is_null() {
        let len = std::ffi::CStr::from_ptr(error).to_bytes_with_nul().len();
        drop(Box::from_raw(std::slice::from_raw_parts_mut(error.cast::<u8>(), len)));
    }
}

/// Asks the module whether `value` can match no row of `column_index`.
///
/// Returns [`RESULT_CAN_SKIP`], [`RESULT_MUST_READ`], or [`RESULT_ERROR`] with a message in
/// `error_out` that the caller frees with [`f8_error_drop`]. Only `RESULT_CAN_SKIP`
/// permits skipping the file.
///
/// # Safety
///
/// `wasm` and `metadata` must point to at least their stated lengths, or be null when empty.
#[no_mangle]
pub unsafe extern "C" fn f8_can_skip_equal_i64(
    runtime: *mut F8Runtime,
    wasm: *const u8,
    wasm_len: usize,
    metadata: *const u8,
    metadata_len: usize,
    column_index: u32,
    value: i64,
    error_out: *mut *mut c_char,
) -> i32 {
    if runtime.is_null() || (wasm.is_null() && wasm_len != 0) || (metadata.is_null() && metadata_len != 0)
    {
        return guard(error_out, || {
            Err(wasmtime::Error::msg("null argument passed to f8_can_skip_equal_i64"))
        });
    }
    let runtime = &*runtime;
    let wasm = if wasm_len == 0 { &[][..] } else { std::slice::from_raw_parts(wasm, wasm_len) };
    let metadata =
        if metadata_len == 0 { &[][..] } else { std::slice::from_raw_parts(metadata, metadata_len) };

    guard(error_out, || runtime.can_skip_equal_i64(wasm, metadata, column_index, value))
}
