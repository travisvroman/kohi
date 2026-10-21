#include "kbinary_writer.h"

#include "debug/kassert.h"
#include "memory/kmemory.h"

kbinary_writer kbinary_writer_create (const void *block, u64 size) {
	kbinary_writer writer = {
		.offset = 0,
		.data = block,
		.data_size = size};
	return writer;
}

void kbinary_writer_write (kbinary_writer *writer, void *source, u64 size) {
	KASSERT(writer);
	KASSERT(source);
	KASSERT(size);

	kcopy_memory((void *)((u8 *)writer->data + writer->offset), source, size);
	writer->offset += size;
}

void kbinary_writer_write_i64 (kbinary_writer *writer, i64 value) {
	kbinary_writer_write(writer, &value, sizeof(i64));
}
void kbinary_writer_write_i32 (kbinary_writer *writer, i32 value) {
	kbinary_writer_write(writer, &value, sizeof(i32));
}
void kbinary_writer_write_i16 (kbinary_writer *writer, i16 value) {
	kbinary_writer_write(writer, &value, sizeof(i16));
}
void kbinary_writer_write_i8 (kbinary_writer *writer, i8 value) {
	kbinary_writer_write(writer, &value, sizeof(i8));
}

void kbinary_writer_write_u64 (kbinary_writer *writer, u64 value) {
	kbinary_writer_write(writer, &value, sizeof(u64));
}
void kbinary_writer_write_u32 (kbinary_writer *writer, u32 value) {
	kbinary_writer_write(writer, &value, sizeof(u32));
}
void kbinary_writer_write_u16 (kbinary_writer *writer, u16 value) {
	kbinary_writer_write(writer, &value, sizeof(u16));
}
void kbinary_writer_write_u8 (kbinary_writer *writer, u8 value) {
	kbinary_writer_write(writer, &value, sizeof(u8));
}

void kbinary_writer_write_f32 (kbinary_writer *writer, f32 value) {
	kbinary_writer_write(writer, &value, sizeof(f32));
}
void kbinary_writer_write_f64 (kbinary_writer *writer, f64 value) {
	kbinary_writer_write(writer, &value, sizeof(f64));
}

void kbinary_writer_write_array (kbinary_writer *writer, void *value, u64 element_size, u32 count) {
	kbinary_writer_write(writer, value, element_size * count);
}
