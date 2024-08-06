#pragma once

#include "assets/kasset_types.h"
#include "core_render_types.h"
#include "math/math_types.h"

/** @brief Returns the string representation of the given texture repeat. */
KAPI const char* texture_repeat_to_string(texture_repeat repeat);

/** @brief Returns the string representation of the given texture filter. */
KAPI const char* texture_filter_mode_to_string(texture_filter filter);

/** @brief Returns the string representation of the given shader uniform type. */
KAPI const char* shader_uniform_type_to_string(shader_uniform_type type);

/** @brief Converts the given string into a texture repeat. Case-insensitive. */
KAPI texture_repeat string_to_texture_repeat(const char* str);

/** @brief Converts the given string into a texture filter. Case-insensitive. */
KAPI texture_filter string_to_texture_filter_mode(const char* str);

/** @brief Converts the given string into a shader uniform type. Case-insensitive. */
KAPI shader_uniform_type string_to_shader_uniform_type(const char* str);

/** @brief Returns the string representation of the given material type. */
KAPI const char* kmaterial_type_to_string(kmaterial_type type);

/** @brief Converts the given string into a material type. Case-insensitive. */
KAPI kmaterial_type string_to_kmaterial_type(const char* str);

KAPI const char* material_map_channel_to_string(kasset_material_map_channel channel);

KAPI kasset_material_map_channel string_to_material_map_channel(const char* str);
