/**
 * @file png_writer.h
 * @brief Writes RGBA8 pixels to a PNG file. Needs no compression library: the image data is stored
 * uncompressed, so files are large but every PNG reader can open them.
 */
#pragma once

#include <defines.h>

/**
 * @brief Writes an image to a PNG file.
 * @param path The file to create or overwrite.
 * @param width The image width in pixels.
 * @param height The image height in pixels.
 * @param rgba Tightly-packed 8-bit RGBA pixels, row 0 at the top (width * height * 4 bytes).
 * @returns True on success; otherwise false.
 */
KAPI b8 png_write_rgba8 (const char *path, u32 width, u32 height, const u8 *rgba);
