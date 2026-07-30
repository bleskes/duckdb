/* C interface of the f8 runtime (extension/f8/runtime/rust).
 *
 * Hand-maintained to match src/lib.rs - it is small enough that generating it would add a
 * cbindgen build dependency for no benefit. Keep the two in sync.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque; create with f8_runtime_create, release with f8_runtime_drop. */
typedef struct F8Runtime F8Runtime;

/* Results of the can-skip calls. Only F8_CAN_SKIP permits skipping. */
#define F8_MUST_READ 0
#define F8_CAN_SKIP  1
#define F8_ERROR     (-1)

/* What a term of a conjunction compares with. NONE means "there is no term on this side". Only equality
 * so far; a module answers "no information" for a type it does not know, so this list can grow without
 * breaking modules already sitting in files. */
#define F8_SKIP_TYPE_NONE  0
#define F8_SKIP_TYPE_EQUAL 1

/* Returns NULL if the wasm engine could not be created. */
F8Runtime *f8_runtime_create(void);

void f8_runtime_drop(F8Runtime *runtime);

/* Frees a message returned through error_out. */
void f8_error_drop(char *error);

/* Asks the filter module whether value can match no row of column_index. The direct line for the
 * common case, so it carries no skip type for the module to dispatch on.
 *
 * On F8_ERROR, *error_out holds a NUL-terminated message owned by the caller.
 * Never panics or aborts: a malformed module reports an error.
 */
int32_t f8_can_skip_equal_i64(F8Runtime *runtime, const uint8_t *wasm, size_t wasm_len, const uint8_t *metadata,
                              size_t metadata_len, uint32_t column_index, int64_t value, char **error_out);

/* Asks whether two terms that must both hold can match no row together. One call rather than two, so a
 * module may answer from metadata that only means something jointly.
 *
 * Same tri-state and error ownership as f8_can_skip_equal_i64.
 */
int32_t f8_can_skip_conj_i64(F8Runtime *runtime, const uint8_t *wasm, size_t wasm_len, const uint8_t *metadata,
                             size_t metadata_len, uint32_t lhs_skip_type, uint32_t lhs_column_index, int64_t lhs_value,
                             uint32_t rhs_skip_type, uint32_t rhs_column_index, int64_t rhs_value, char **error_out);

#ifdef __cplusplus
}
#endif
