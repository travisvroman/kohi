#pragma once

#include <defines.h>

typedef struct kbinary_reader {
	u64 offset;
	u64 data_size;
	const void *data;
} kbinary_reader;

kbinary_reader kbinary_reader_create (const void *block, u64 size);

void kbinary_reader_read (kbinary_reader *reader, void *dest, u64 size);
void kbinary_reader_read_i64 (kbinary_reader *reader, i64 *dest);
void kbinary_reader_read_i32 (kbinary_reader *reader, i32 *dest);
void kbinary_reader_read_i16 (kbinary_reader *reader, i16 *dest);
void kbinary_reader_read_i8 (kbinary_reader *reader, i8 *dest);
void kbinary_reader_read_u64 (kbinary_reader *reader, u64 *dest);
void kbinary_reader_read_u32 (kbinary_reader *reader, u32 *dest);
void kbinary_reader_read_u16 (kbinary_reader *reader, u16 *dest);
void kbinary_reader_read_u8 (kbinary_reader *reader, u8 *dest);
void kbinary_reader_read_f32 (kbinary_reader *reader, f32 *dest);
void kbinary_reader_read_f64 (kbinary_reader *reader, f64 *dest);

void kbinary_reader_read_array (kbinary_reader *reader, void *dest, u64 element_size, u32 count);

#define kbinary_reader_read_type(reader, type, dest) kbinary_reader_read(reader, dest, sizeof(type))
#define kbinary_reader_read_type_array(reader, type, count, dest) kbinary_reader_read(reader, dest, sizeof(type) * count)
