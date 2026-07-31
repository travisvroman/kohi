#pragma once

#include <defines.h>

typedef struct kbinary_writer {
	u64 offset;
	u64 data_size;
	const void *data;
} kbinary_writer;

kbinary_writer kbinary_writer_create (const void *block, u64 size);

void kbinary_writer_write (kbinary_writer *writer, void *source, u64 size);
void kbinary_writer_write_i64 (kbinary_writer *writer, i64 value);
void kbinary_writer_write_i32 (kbinary_writer *writer, i32 value);
void kbinary_writer_write_i16 (kbinary_writer *writer, i16 value);
void kbinary_writer_write_i8 (kbinary_writer *writer, i8 value);
void kbinary_writer_write_u64 (kbinary_writer *writer, u64 value);
void kbinary_writer_write_u32 (kbinary_writer *writer, u32 value);
void kbinary_writer_write_u16 (kbinary_writer *writer, u16 value);
void kbinary_writer_write_u8 (kbinary_writer *writer, u8 value);
void kbinary_writer_write_f32 (kbinary_writer *writer, f32 value);
void kbinary_writer_write_f64 (kbinary_writer *writer, f64 value);

void kbinary_writer_write_array (kbinary_writer *writer, void *value, u64 element_size, u32 count);

#define kbinary_writer_write_type(writer, type, value) kbinary_writer_write(writer, value, sizeof(type))
#define kbinary_writer_write_type_array(writer, type, count, value) kbinary_writer_write(writer, value, sizeof(type) * count)
