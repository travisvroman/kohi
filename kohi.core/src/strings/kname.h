
/**
 * @file kname.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of knames.
 *
 * @details
 * KNames are a lightweight system to manage strings within an application for
 * quick comparisons. Each KName is hashed into a unique number stored as a key
 * in a lookup table for later use. This lookup table also stores a copy of the
 * original string. KNames are immutable, and thus cannot be changed once internalized
 * into the lookup table, even when reused.
 *
 * KNames are case-insensitive .
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

typedef u64 kname;

/**
 * Creates a kname for the given string. This creates a hash of the string
 * for quick comparisons. A copy of the original string is maintained within
 * an internal global lookup table, where the hash provided (i.e. kname) is
 * the lookup key.
 * NOTE: A hash of 0 is never allowed here.
 */
KAPI kname kname_create(const char* str);

/**
 * Attempts to get the original string associated with the given kname.
 * This will only work if the name was originally registered in the internal
 * global lookup table.
 *
 * @param name The kname to lookup.
 * @returns A constant pointer to the string if found, otherwise 0/null.
 */
KAPI const char* kname_string_get(kname name);
