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
// Both exports are required, but this module only answers through f8_conj_i64: it records which *pairs*
// occur together, and a single value tells that nothing. Bounds answer a lone equality, and ../min_max is
// the module that reads them - one idea per example, both reading the same blob.
//
// A conjunction is one call rather than two so a module can answer from metadata that only means something
// jointly: a = 2 AND b = 1002 is ruled out when both values occur in the file but never in the same row.
// No per-column statistic can do that, parquet's own included.
//
// column_index is the column's 0-based position in the file's own column order. Values arrive in
// registers, so only the metadata crosses into linear memory.
//
// Every unexpected input answers MUST_READ: a wrong CAN_SKIP silently drops rows, a wrong MUST_READ only
// costs a scan. Nothing here can trap - no division, no out-of-range shifts, and every read is
// bounds-checked against metadata_len.
//
// Metadata layout ("F8S1")
// ------------------------
// Bounds first, then matrices. The bounds are a sparse map from column index, so a fifty column file
// with one bounded column carries one entry rather than fifty:
//
//   0..4    magic "F8S1"
//   4..8    entry count, little endian
//   8..     per entry, 24 bytes: column index u32, reserved u32, min i64, max i64
//
// A column with no entry has no bounds and is never skipped, which is also how an unsupported type is
// represented. This module skips past them; it reads only the matrices that follow.
//
// A matrix covers a pair of columns each spanning at most 256 values, and records which combinations
// occur together in some row:
//
//   0..4    magic "F8M1"
//   4..8    x column index u32
//   8..12   y column index u32
//   12..20  x_min i64
//   20..28  y_min i64
//   28..30  width  u16, x_max - x_min + 1
//   30..32  height u16, y_max - y_min + 1
//   32..    ceil(width * height / 8) bytes; bit (dy * width + dx) is set when that pair occurs
//
// Matrices come last so a module that knows nothing about them still works: the entry count fixes where
// the bounds end, and the rest is simply not read.
//
// Nothing outside this file and its metadata writer knows this layout - swap both and the engine is
// unaffected.

typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned char u8;

#define MUST_READ 0u
#define CAN_SKIP  1u

// What a term compares with. NONE means "no term on this side". Only equality so far; any other type is
// answered "no information", so the host can add to this list without breaking modules already in files.
#define SKIP_TYPE_NONE  0u
#define SKIP_TYPE_EQUAL 1u

// "F8S1" and "F8M1" read as little endian u32.
#define MAGIC             0x31533846u
#define MATRIX_MAGIC      0x314D3846u
#define HEADER_LEN        8u
#define ENTRY_LEN         24u
#define MATRIX_HEADER_LEN 32u

// The widest span a dimension may have. 256 x 256 bits is 8 KiB per matrix, hence the buffer size.
#define MATRIX_MAX_SPAN 256u

#define METADATA_CAPACITY 65536u
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

static u32 read_u16(u32 offset) {
	return (u32)METADATA[offset] | ((u32)METADATA[offset + 1] << 8);
}

static i64 read_i64(u32 offset) {
	u64 value = 0;
	for (u32 i = 0; i < 8; i++) {
		value |= (u64)METADATA[offset + i] << (i * 8);
	}
	return (i64)value;
}

// Where the bounds end and the matrices begin, or 0 if the metadata is not usable at all.
static u32 matrix_section_offset(u32 metadata_len) {
	if (metadata_len < HEADER_LEN || read_u32(0) != MAGIC) {
		return 0;
	}
	u32 entry_count = read_u32(4);
	if (entry_count > (metadata_len - HEADER_LEN) / ENTRY_LEN) {
		return 0;
	}
	return HEADER_LEN + entry_count * ENTRY_LEN;
}

// Looks the pair up in the matrix whose header starts at `offset`; `x` and `y` are in that matrix's own
// column order. CAN_SKIP only when the matrix positively says the pair does not occur.
static u32 matrix_lookup(u32 offset, u32 width, u32 height, i64 x_min, i64 y_min, i64 x, i64 y) {
	// Unsigned subtraction on purpose: a value below the minimum wraps to something huge, which the
	// range check below catches. Signed subtraction could overflow, and overflow is a trap.
	u64 dx = (u64)x - (u64)x_min;
	u64 dy = (u64)y - (u64)y_min;
	if (dx >= (u64)width || dy >= (u64)height) {
		// Outside the matrix entirely, so that value does not occur in the file at all.
		return CAN_SKIP;
	}
	u32 bit = (u32)dy * width + (u32)dx;
	u32 byte = METADATA[offset + MATRIX_HEADER_LEN + bit / 8u];
	return ((byte >> (bit % 8u)) & 1u) ? MUST_READ : CAN_SKIP;
}

// Asks every matrix about this pair of columns, in either order. Anything unparseable stops the walk
// rather than guessing where the next matrix starts.
static u32 matrices_can_skip(u32 metadata_len, u32 lhs_column, i64 lhs_value, u32 rhs_column, i64 rhs_value) {
	u32 offset = matrix_section_offset(metadata_len);
	if (offset == 0) {
		return MUST_READ;
	}
	while (offset + MATRIX_HEADER_LEN <= metadata_len && read_u32(offset) == MATRIX_MAGIC) {
		u32 x_column = read_u32(offset + 4);
		u32 y_column = read_u32(offset + 8);
		i64 x_min = read_i64(offset + 12);
		i64 y_min = read_i64(offset + 20);
		u32 width = read_u16(offset + 28);
		u32 height = read_u16(offset + 30);

		// A zero dimension would make every lookup answer "outside the matrix", i.e. skip everything.
		if (width == 0 || height == 0 || width > MATRIX_MAX_SPAN || height > MATRIX_MAX_SPAN) {
			return MUST_READ;
		}
		u32 bits_len = (width * height + 7u) / 8u;
		if (bits_len > metadata_len - offset - MATRIX_HEADER_LEN) {
			// Truncated: the bits we would read are not there.
			return MUST_READ;
		}

		if (x_column == lhs_column && y_column == rhs_column) {
			if (matrix_lookup(offset, width, height, x_min, y_min, lhs_value, rhs_value) == CAN_SKIP) {
				return CAN_SKIP;
			}
		} else if (x_column == rhs_column && y_column == lhs_column) {
			// The same matrix read with the query's two terms the other way round.
			if (matrix_lookup(offset, width, height, x_min, y_min, rhs_value, lhs_value) == CAN_SKIP) {
				return CAN_SKIP;
			}
		}
		offset += MATRIX_HEADER_LEN + bits_len;
	}
	return MUST_READ;
}

EXPORT("f8_can_skip_equal_i64")
u32 f8_can_skip_equal_i64(u32 column_index, u32 metadata_len, i64 value) {
	(void)column_index;
	(void)metadata_len;
	(void)value;
	return MUST_READ;
}

// Both terms must hold, so a combination the matrix does not have rules the file out.
EXPORT("f8_conj_i64")
u32 f8_conj_i64(u32 metadata_len, u32 lhs_type, u32 lhs_column_index, i64 lhs_value, u32 rhs_type, u32 rhs_column_index,
                i64 rhs_value) {
	if (metadata_len > METADATA_CAPACITY) {
		return MUST_READ;
	}
	// A matrix covers a pair, so both sides have to be real equality terms for it to say anything.
	if (lhs_type != SKIP_TYPE_EQUAL || rhs_type != SKIP_TYPE_EQUAL) {
		return MUST_READ;
	}
	return matrices_can_skip(metadata_len, lhs_column_index, lhs_value, rhs_column_index, rhs_value);
}
