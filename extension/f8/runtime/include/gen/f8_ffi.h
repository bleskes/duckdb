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

/* Results of f8_can_skip_equal_i64. Only F8_CAN_SKIP permits skipping. */
#define F8_MUST_READ 0
#define F8_CAN_SKIP  1
#define F8_ERROR     (-1)

/* Returns NULL if the wasm engine could not be created. */
F8Runtime *f8_runtime_create(void);

void f8_runtime_drop(F8Runtime *runtime);

/* Frees a message returned through error_out. */
void f8_error_drop(char *error);

/* Asks the filter module whether value can match no row of column_index.
 *
 * On F8_ERROR, *error_out holds a NUL-terminated message owned by the caller.
 * Never panics or aborts: a malformed module reports an error.
 */
int32_t f8_can_skip_equal_i64(F8Runtime *runtime, const uint8_t *wasm, size_t wasm_len, const uint8_t *metadata,
                              size_t metadata_len, uint32_t column_index, int64_t value, char **error_out);

#ifdef __cplusplus
}
#endif
