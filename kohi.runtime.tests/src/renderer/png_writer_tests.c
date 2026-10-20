#include "png_writer_tests.h"

#include <defines.h>
#include <renderer/png_writer.h>

#include <stdio.h>
#include <string.h>

#include "../expect.h"
#include "../test_manager.h"

#define TEST_FILE "png_writer_test.png"

// Layout of the file for a 3x2 image: signature, IHDR, one IDAT with a single stored block, IEND.
#define SIGNATURE_SIZE 8
#define IHDR_CHUNK_SIZE (12 + 13)
#define IDAT_DATA_OFFSET (SIGNATURE_SIZE + IHDR_CHUNK_SIZE + 8)
#define ZLIB_AND_BLOCK_HEADER_SIZE (2 + 5)

static u8 png_writer_writes_expected_structure (void) {
	u8 pixels[3 * 2 * 4];
	for (u32 i = 0; i < sizeof(pixels); ++i) {
		pixels[i] = (u8)(i * 7 + 1);
	}
	expect_to_be_true(png_write_rgba8(TEST_FILE, 3, 2, pixels));

	u8 file[256];
	FILE *handle = fopen(TEST_FILE, "rb");
	expect_to_be_true(handle != 0);
	u64 size = fread(file, 1, sizeof(file), handle);
	fclose(handle);
	remove(TEST_FILE);

	// Two rows of a filter byte and 12 pixel bytes make 26 bytes, stored in one deflate block.
	const u32 raw_size = 2 * (1 + 3 * 4);
	const u32 idat_size = ZLIB_AND_BLOCK_HEADER_SIZE + raw_size + 4;
	expect_should_be(SIGNATURE_SIZE + IHDR_CHUNK_SIZE + (12 + idat_size) + 12, size);

	static const u8 signature[SIGNATURE_SIZE] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	expect_should_be(0, memcmp(file, signature, SIGNATURE_SIZE));

	// IHDR: width 3, height 2, 8-bit RGBA.
	const u8 *ihdr = file + SIGNATURE_SIZE + 8;
	expect_should_be(3, ihdr[3]);
	expect_should_be(2, ihdr[7]);
	expect_should_be(8, ihdr[8]);
	expect_should_be(6, ihdr[9]);

	// The stored data is each row's pixels behind a zero filter byte.
	const u8 *raw = file + IDAT_DATA_OFFSET + ZLIB_AND_BLOCK_HEADER_SIZE;
	expect_should_be(0, raw[0]);
	expect_should_be(0, memcmp(raw + 1, pixels, 12));
	expect_should_be(0, raw[13]);
	expect_should_be(0, memcmp(raw + 14, pixels + 12, 12));

	// IEND closes the file; its CRC is always the same.
	static const u8 iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
	expect_should_be(0, memcmp(file + size - 12, iend, 12));
	return true;
}

static u8 png_writer_rejects_empty_images (void) {
	u8 pixel[4] = {0};
	expect_should_be(false, png_write_rgba8(TEST_FILE, 0, 1, pixel));
	expect_should_be(false, png_write_rgba8(TEST_FILE, 1, 1, 0));
	return true;
}

void png_writer_register_tests (void) {
	test_manager_register_test(png_writer_writes_expected_structure, "png_writer: writes a PNG with the expected chunks and pixel data.");
	test_manager_register_test(png_writer_rejects_empty_images, "png_writer: rejects empty images.");
}
