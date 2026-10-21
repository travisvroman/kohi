#include "kbinary_reader.h"
#include "debug/kassert.h"
#include "memory/kmemory.h"

kbinary_reader kbinary_reader_create (const void *block, u64 size) {
	kbinary_reader out_reader = {
		.data_size = size,
		.data = block,
		.offset = 0};
	return out_reader;
}

void kbinary_reader_read (kbinary_reader *reader, void *dest, u64 size) {
	KASSERT(reader);
	KASSERT(dest);
	KASSERT(size);
	kcopy_memory(dest, (void *)((u64)reader->data + reader->offset), size);
	reader->offset += size;
}

void kbinary_reader_read_i64 (kbinary_reader *reader, i64 *dest) {
	kbinary_reader_read(reader, dest, sizeof(i64));
}
void kbinary_reader_read_i32 (kbinary_reader *reader, i32 *dest) {
	kbinary_reader_read(reader, dest, sizeof(i32));
}
void kbinary_reader_read_i16 (kbinary_reader *reader, i16 *dest) {
	kbinary_reader_read(reader, dest, sizeof(i16));
}
void kbinary_reader_read_i8 (kbinary_reader *reader, i8 *dest) {
	kbinary_reader_read(reader, dest, sizeof(i8));
}

void kbinary_reader_read_u64 (kbinary_reader *reader, u64 *dest) {
	kbinary_reader_read(reader, dest, sizeof(u64));
}
void kbinary_reader_read_u32 (kbinary_reader *reader, u32 *dest) {
	kbinary_reader_read(reader, dest, sizeof(u32));
}
void kbinary_reader_read_u16 (kbinary_reader *reader, u16 *dest) {
	kbinary_reader_read(reader, dest, sizeof(u16));
}
void kbinary_reader_read_u8 (kbinary_reader *reader, u8 *dest) {
	kbinary_reader_read(reader, dest, sizeof(u8));
}

void kbinary_reader_read_f32 (kbinary_reader *reader, f32 *dest) {
	kbinary_reader_read(reader, dest, sizeof(f32));
}
void kbinary_reader_read_f64 (kbinary_reader *reader, f64 *dest) {
	kbinary_reader_read(reader, dest, sizeof(f64));
}

void kbinary_reader_read_array (kbinary_reader *reader, void *dest, u64 element_size, u32 count) {
	kbinary_reader_read(reader, dest, element_size * count);
}
