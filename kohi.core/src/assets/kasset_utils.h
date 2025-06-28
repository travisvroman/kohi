#pragma once

#include "assets/kasset_types.h"
#include "defines.h"

struct kasset_name;

/**
 * @brief Attempts to convert the provided type string to the appropriate enumeration value.
 *
 * @param type_str The type string to be examined.
 * @return The converted type if successful; otherwise KASSET_TYPE_UNKNOWN.
 */
KAPI kasset_type kasset_type_from_string(const char* type_str);

/**
 * @brief Converts the given asset type enumeration value to its string representation.
 * NOTE: Returns a copy of the string, which should be freed when no longer used.
 *
 * @param type The type to be converted.
 * @return The string representation of the type.
 */
KAPI const char* kasset_type_to_string(kasset_type type);

/**
 * @brief Indicates if the given asset type is a binary asset type.
 * 
 * @param type The asset type.
 * @return True if the given asset type is binary; otherwise false.
 */
KAPI b8 kasset_type_is_binary(kasset_type type);
