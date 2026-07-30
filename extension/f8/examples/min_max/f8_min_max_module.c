// An f8 filter module: given a predicate and the metadata a parquet file carries in its key-value
// metadata, it answers whether the whole file can be skipped.
//
// It decodes nothing - parquet does that. It only reads the metadata.
//
// Build with tools/build_f8_module. Freestanding C: no libc, no allocator, no imports, so the host
// can instantiate it without supplying anything.
//
// ABI
// ---
//   f8_metadata_buffer()   -> u32   address of the buffer the host writes the metadata into
//   f8_metadata_capacity() -> u32   its size, so the host can bounds-check before writing
//   f8_can_skip_equal_i64(column_index, metadata_len, value) -> u32
//                                 1 = no row of that column can equal value, so skip the file
//                                 0 = must read
//   f8_conj_i64(metadata_len, lhs_type, lhs_column_index, lhs_value,
//                             rhs_type, rhs_column_index, rhs_value) -> u32
//                                 the same answer for two terms that must both hold
//
// column_index is the column's 0-based position in the file's own column order. Values arrive in
// registers, so only the metadata crosses into linear memory.
//
// The simplest module that implements the whole ABI: per-column bounds and nothing else, so a conjunction
// is just each term in turn. See ../min_maxtrix for one that answers from metadata about a pair jointly.
//
// Every unexpected input answers MUST_READ. Answering CAN_SKIP wrongly silently drops rows;
// answering MUST_READ wrongly only costs a scan. Nothing here can trap: no division, no
// out-of-range shifts, and every read is bounds-checked against metadata_len.
//
// Metadata layout ("F8S1")
// ----------------------
// A sparse map from column index to bounds, so a fifty column file with one bounded column carries
// one entry rather than fifty:
//
//   0..4    magic "F8S1"
//   4..8    entry count, little endian
//   8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
//
// A column with no entry has no bounds and is never skipped, which is also how a column of an
// unsupported type is represented. Nothing outside this file and its metadata writer knows this
// layout - swap both and the engine is unaffected.

typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned char u8;

#define MUST_READ 0u
#define CAN_SKIP  1u

// What a term of a conjunction compares with. NONE is how the host says there is no term on this side.
// Only equality so far; a term of any other type is answered "no information", which lets the host grow
// this list without breaking modules already sitting in files.
#define SKIP_TYPE_NONE  0u
#define SKIP_TYPE_EQUAL 1u

// "F8S1" read as a little endian u32.
#define MAGIC      0x31533846u
#define HEADER_LEN 8u
#define ENTRY_LEN  24u

#define METADATA_CAPACITY 4096u
static u8 METADATA[METADATA_CAPACITY];

#define EXPORT(name) __attribute__((export_name(name)))

EXPORT("f8_metadata_buffer")
u32 f8_metadata_buffer(void) {
	return (u32)(u64)(void *)METADATA;
}

EXPORT("f8_metadata_capacity")
u32 f8_metadata_capacity(void) {
	return METADATA_CAPACITY;
}

// Reads little endian regardless of host byte order, and without unaligned loads.
static u32 read_u32(u32 offset) {
	return (u32)METADATA[offset] | ((u32)METADATA[offset + 1] << 8) | ((u32)METADATA[offset + 2] << 16) |
	       ((u32)METADATA[offset + 3] << 24);
}

static i64 read_i64(u32 offset) {
	u64 value = 0;
	for (u32 i = 0; i < 8; i++) {
		value |= (u64)METADATA[offset + i] << (i * 8);
	}
	return (i64)value;
}

// Finds the bounds recorded for a column. Returns 0 when the metadata says nothing about it, in which
// case *min and *max are untouched.
static int get_bounds_for_columns(u32 metadata_len, u32 column_index, i64 *min, i64 *max) {
	if (metadata_len < HEADER_LEN || read_u32(0) != MAGIC) {
		return 0;
	}
	u32 entry_count = read_u32(4);
	// Reject a count that does not fit the bytes we were given, rather than reading past them.
	if (entry_count > (metadata_len - HEADER_LEN) / ENTRY_LEN) {
		return 0;
	}
	for (u32 entry = 0; entry < entry_count; entry++) {
		u32 offset = HEADER_LEN + entry * ENTRY_LEN;
		if (read_u32(offset) != column_index) {
			continue;
		}
		i64 low = read_i64(offset + 8);
		i64 high = read_i64(offset + 16);
		// An inverted range would make everything look skippable, so treat it as no information.
		if (low > high) {
			return 0;
		}
		*min = low;
		*max = high;
		return 1;
	}
	return 0;
}

// One term of a predicate: can this comparison match no row at all?
static u32 term_can_skip(u32 metadata_len, u32 skip_type, u32 column_index, i64 value) {
	if (skip_type != SKIP_TYPE_EQUAL) {
		return MUST_READ;
	}
	i64 min = 0, max = 0;
	if (!get_bounds_for_columns(metadata_len, column_index, &min, &max)) {
		return MUST_READ;
	}
	return (value < min || value > max) ? CAN_SKIP : MUST_READ;
}

// The direct line for a lone equality: no skip type to look at, no second term to walk.
EXPORT("f8_can_skip_equal_i64")
u32 f8_can_skip_equal_i64(u32 column_index, u32 metadata_len, i64 value) {
	if (metadata_len > METADATA_CAPACITY) {
		return MUST_READ;
	}
	return term_can_skip(metadata_len, SKIP_TYPE_EQUAL, column_index, value);
}

// Both terms must hold, so either one matching no row rules the file out - and a term this module cannot
// use (NONE, or a type from a newer host) does not stop the other from proving it.
EXPORT("f8_conj_i64")
u32 f8_conj_i64(u32 metadata_len, u32 lhs_type, u32 lhs_column_index, i64 lhs_value, u32 rhs_type, u32 rhs_column_index,
                i64 rhs_value) {
	if (metadata_len > METADATA_CAPACITY) {
		return MUST_READ;
	}
	if (term_can_skip(metadata_len, lhs_type, lhs_column_index, lhs_value) == CAN_SKIP) {
		return CAN_SKIP;
	}
	return term_can_skip(metadata_len, rhs_type, rhs_column_index, rhs_value);
}
