/**
 * @file kname.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of knames.
 *
 * @details
 * knames are a lightweight system to manage strings within an application for
 * quick comparisons. Each kname is hashed into a unique number stored as a key
 * in a lookup table for later use. This lookup table also stores a copy of the
 * original string. knames are immutable, and thus cannot be changed once internalized
 * into the lookup table, even when reused.
 *
 * NOTE: knames are case-insensitive. For a case-sensitive variant, see kstring_id.h.
 * NOTE: case-insensitivity applies to regular ascii and western european high-ascii characters only.
 *
 * @version 1.0
 * @date 2024-08-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

/** @brief Represents an invalid kname, which is essentially used to represent "no name". */
#define INVALID_KNAME 0

/**
 * @brief A kname is a string hash made for quick comparisons versus traditional string comparisons.
 *
 * A kname's hash is generated from a lowercased version of a string. The original string (with original
 * casing) may be looked up and retrieved at any time using kname_string_get().
 */
typedef u64 kname;

/**
 * Creates a kname for the given string. This creates a hash of the string
 * for quick comparisons. A copy of the original string is maintained within
 * an internal global lookup table, where the hash provided (i.e. kname) is
 * the lookup key. NOTE: knames are case-insensitive!
 *
 * NOTE: A hash of 0 is never allowed here.
 *
 * @param str The source string to use while creating the kname.
 * @returns The hashed kname.
 */
KAPI kname kname_create(const char* str);

/**
 * Attempts to get the original string associated with the given kname.
 * This will only work if the name was originally registered in the internal
 * global lookup table.
 *
 * @param name The kname to lookup.
 * @returns A constant pointer to the string if found, otherwise 0/null. NOTE: Do *NOT* free this string!
 */
KAPI const char* kname_string_get(kname name);
