#pragma once

#include "assets/kasset_types.h"
#include "defines.h"

struct kasset_name;

/**
 * @brief Parses name info from the provided fully_qualified_name.
 *
 * @param fully_qualified_name The fully-qualified name of the asset (i.e. "Testbed.Texture.Rock01").
 * @param out_name A pointer to hold the parsed name. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 kasset_util_parse_name(const char* fully_qualified_name, struct kasset_name* out_name);

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
const char* kasset_type_to_string(kasset_type type);
