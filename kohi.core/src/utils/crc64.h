#pragma once

#include "defines.h"

/**
 * Compute crc64, update given crc value with new data.
 *
 * @param crc The current crc value. Can pass 0 for a new one.
 * @param data A constant pointer to a buffer of length bytes.
 * @param length Number of bytes in the data buffer.
 */
KAPI u64 crc64(u64 crc, const u8* data, u64 length);
