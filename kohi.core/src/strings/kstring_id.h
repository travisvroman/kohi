/**
 * @file kstring_id.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of kstring_ids.
 *
 * @details
 * kstring_ids are a lightweight system to manage strings within an application for
 * quick comparisons. Each kstring_id is hashed into a unique number stored as a key
 * in a lookup table for later use. This lookup table also stores a copy of the
 * original string. kstring_id are immutable, and thus cannot be changed once internalized
 * into the lookup table, even when reused.
 *
 * NOTE: kstring_ids are case-sensitive. For a case-insensitive variant, see kname.h
 *
 * @version 1.0
 * @date 2024-11-26
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief A kstring_id is a string hash made for quick comparisons versus traditional string comparisons.
 *
 * A kname's hash is generated from a case-sensitive version of a string. The original string
 * may be looked up and retrieved at any time using kstring_id_string_get().
 */
typedef u64 kstring_id;

/** @brief Represents an invalid kstring_id, which is essentially used to represent a null or empty string. */
#define INVALID_KSTRING_ID 0

/**
 * Creates a kstring_id for the given string. This creates a hash of the string
 * for quick comparisons. A copy of the original string is maintained within
 * an internal global lookup table, where the hash provided (i.e. kstring_id) is
 * the lookup key. NOTE: kstring_ids are case-sensitive!
 * NOTE: A hash of 0 is never allowed here.
 *
 * @param str The source string to use while creating the kstring_id.
 * @returns The hashed kstring_id.
 */
KAPI kstring_id kstring_id_create(const char* str);

/**
 * Attempts to get the original string associated with the given kname.
 * This will only work if the name was originally registered in the internal
 * global lookup table.
 *
 * @param name The kstring_id to lookup.
 * @returns A constant pointer to the string if found, otherwise 0/null. NOTE: Do *NOT* free this string!
 */
KAPI const char* kstring_id_string_get(kstring_id stringid);
