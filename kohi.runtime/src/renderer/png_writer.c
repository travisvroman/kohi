#include "png_writer.h"

#include <logger.h>

#include <stdio.h>

// Deflate stores at most this many bytes per uncompressed block.
#define DEFLATE_STORED_BLOCK_MAX 65535

static u32 crc32_update (u32 crc, const u8 *data, u64 size) {
	for (u64 i = 0; i < size; ++i) {
		crc ^= data[i];
		for (u32 bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return crc;
}

static void write_u32_be (FILE *file, u32 value) {
	u8 bytes[4] = {(u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
	fwrite(bytes, 1, 4, file);
}

static void chunk_write (FILE *file, const char type[4], const u8 *data, u32 size) {
	write_u32_be(file, size);
	fwrite(type, 1, 4, file);
	if (size) {
		fwrite(data, 1, size, file);
	}
	u32 crc = crc32_update(0xFFFFFFFFu, (const u8 *)type, 4);
	crc = crc32_update(crc, data, size);
	write_u32_be(file, crc ^ 0xFFFFFFFFu);
}

b8 png_write_rgba8 (const char *path, u32 width, u32 height, const u8 *rgba) {
	if (!path || !rgba || !width || !height) {
		return false;
	}

	// Each row is a filter byte (0 = none) followed by its pixels.
	u64 row_size = 1 + (u64)width * 4;
	u64 raw_size = row_size * height;
	if (raw_size > 0xFFFFFFFFu - 1024) {
		KERROR("png_write_rgba8: a %ux%u image is too large.", width, height);
		return false;
	}

	FILE *file = fopen(path, "wb");
	if (!file) {
		KERROR("png_write_rgba8: cannot open '%s' for writing.", path);
		return false;
	}

	static const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	fwrite(signature, 1, 8, file);

	u8 header[13] = {
		(u8)(width >> 24), (u8)(width >> 16), (u8)(width >> 8), (u8)width,
		(u8)(height >> 24), (u8)(height >> 16), (u8)(height >> 8), (u8)height,
		8, // bit depth
		6, // colour type: RGBA
		0, 0, 0 // deflate, adaptive filtering, no interlace
	};
	chunk_write(file, "IHDR", header, sizeof(header));

	// The IDAT chunk is a zlib stream of stored deflate blocks. Its size is known up front,
	// so the chunk can be streamed instead of being built in memory.
	u64 block_count = (raw_size + DEFLATE_STORED_BLOCK_MAX - 1) / DEFLATE_STORED_BLOCK_MAX;
	u32 idat_size = (u32)(2 + raw_size + block_count * 5 + 4);
	write_u32_be(file, idat_size);
	fwrite("IDAT", 1, 4, file);
	u32 crc = crc32_update(0xFFFFFFFFu, (const u8 *)"IDAT", 4);

	const u8 zlib_header[2] = {0x78, 0x01};
	fwrite(zlib_header, 1, 2, file);
	crc = crc32_update(crc, zlib_header, 2);

	u32 adler_a = 1, adler_b = 0;
	u64 raw_offset = 0;
	u8 block[DEFLATE_STORED_BLOCK_MAX];
	while (raw_offset < raw_size) {
		u32 block_size = (u32)(raw_size - raw_offset < DEFLATE_STORED_BLOCK_MAX ? raw_size - raw_offset : DEFLATE_STORED_BLOCK_MAX);

		// Gather the block's bytes from the filtered rows.
		for (u32 i = 0; i < block_size; ++i) {
			u64 position = raw_offset + i;
			u64 column = position % row_size;
			block[i] = column == 0 ? 0 : rgba[(position / row_size) * width * 4 + (column - 1)];
		}

		b8 is_final = raw_offset + block_size == raw_size;
		u8 block_header[5] = {is_final ? 1 : 0, (u8)block_size, (u8)(block_size >> 8), (u8)~block_size, (u8)(~block_size >> 8)};
		fwrite(block_header, 1, 5, file);
		fwrite(block, 1, block_size, file);
		crc = crc32_update(crc, block_header, 5);
		crc = crc32_update(crc, block, block_size);

		for (u32 i = 0; i < block_size; ++i) {
			adler_a = (adler_a + block[i]) % 65521;
			adler_b = (adler_b + adler_a) % 65521;
		}
		raw_offset += block_size;
	}

	u32 adler = (adler_b << 16) | adler_a;
	u8 adler_bytes[4] = {(u8)(adler >> 24), (u8)(adler >> 16), (u8)(adler >> 8), (u8)adler};
	fwrite(adler_bytes, 1, 4, file);
	crc = crc32_update(crc, adler_bytes, 4);
	write_u32_be(file, crc ^ 0xFFFFFFFFu);

	chunk_write(file, "IEND", 0, 0);

	b8 ok = ferror(file) == 0;
	ok = fclose(file) == 0 && ok;
	if (!ok) {
		KERROR("png_write_rgba8: failed writing '%s'.", path);
	}
	return ok;
}
