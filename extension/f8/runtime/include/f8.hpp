//===----------------------------------------------------------------------===//
//                         DuckDB
//
// f8.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "gen/f8_ffi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace f8 {

//! What a filter module answered about a file.
enum class SkipVerdict {
	//! The file may contain matching rows and has to be read.
	MUST_READ,
	//! No row can match, so the file can be skipped entirely.
	CAN_SKIP
};

//! Thrown when a module could not be run at all - it is malformed, missing its exports, or
//! ran out of fuel. Callers should treat this as MUST_READ unless they want to surface it.
class F8Error : public std::runtime_error {
public:
	explicit F8Error(const std::string &message) : std::runtime_error(message) {
	}
};

//! Runs wasm filter modules that data files carry alongside their data.
//!
//! Compiled modules are cached inside the runtime, keyed by their bytes, so probing many
//! files that share a module compiles it once. Safe to share; the runtime locks internally.
class Runtime {
public:
	Runtime() : runtime(f8_runtime_create(), f8_runtime_drop) {
		if (!runtime) {
			throw F8Error("could not create the wasm filter runtime");
		}
	}

	//! Asks whether `value` can match no row of `column_index`, given the metadata the file
	//! carries. Throws F8Error if the module could not be run.
	SkipVerdict CanSkipEqual(const uint8_t *wasm, size_t wasm_len, const uint8_t *metadata, size_t metadata_len,
	                         uint32_t column_index, int64_t value) const {
		char *error = nullptr;
		auto result =
		    f8_can_skip_equal_i64(runtime.get(), wasm, wasm_len, metadata, metadata_len, column_index, value, &error);
		if (result == F8_ERROR) {
			// Copy before freeing, so the message survives as an exception.
			std::string message = error ? error : "unknown wasm filter error";
			f8_error_drop(error);
			throw F8Error(message);
		}
		return result == F8_CAN_SKIP ? SkipVerdict::CAN_SKIP : SkipVerdict::MUST_READ;
	}

private:
	std::unique_ptr<F8Runtime, void (*)(F8Runtime *)> runtime;
};

} // namespace f8
