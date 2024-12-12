#pragma once

#include <assets/kasset_types.h>

#include "kresource_types.h"

KAPI kresource_texture_format image_format_to_texture_format(kasset_image_format format);

KAPI kasset_image_format texture_format_to_image_format(kresource_texture_format format);

KAPI u8 channel_count_from_texture_format(kresource_texture_format format);

/**
 * @brief Gets the string representation of the given resource type.
 * 
 * @param type The resource type.
 * @return The string representation of the given resource type.
 */
KAPI const char* kresource_type_to_string(kresource_type type);
