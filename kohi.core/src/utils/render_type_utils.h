#pragma once

#include "core_render_types.h"

/** @brief Returns the string representation of the given texture repeat. */
KAPI const char* texture_repeat_to_string(texture_repeat repeat);

/** @brief Returns the string representation of the given texture filter. */
KAPI const char* texture_filter_mode_to_string(texture_filter filter);

/** @brief Returns the string representation of the given shader uniform type. */
KAPI const char* shader_uniform_type_to_string(shader_uniform_type type);

/** @brief Returns the string representation of the given shader attribute type. */
KAPI const char* shader_attribute_type_to_string(shader_attribute_type type);

/** @brief Returns the string representation of the given shader stage. */
KAPI const char* shader_stage_to_string(shader_stage stage);

/** @brief Returns the string representation of the given shader update frequency. */
KAPI const char* shader_update_frequency_to_string(shader_update_frequency frequency);

/** @brief Converts the given string into a texture repeat. Case-insensitive. */
KAPI texture_repeat string_to_texture_repeat(const char* str);

/** @brief Converts the given string into a texture filter. Case-insensitive. */
KAPI texture_filter string_to_texture_filter_mode(const char* str);

/** @brief Converts the given string into a shader uniform type. Case-insensitive. */
KAPI shader_uniform_type string_to_shader_uniform_type(const char* str);

/** @brief Converts the given string into a shader attribute type. Case-insensitive. */
KAPI shader_attribute_type string_to_shader_attribute_type(const char* str);

/** @brief Converts the given string into a shader stage. Case-insensitive. */
KAPI shader_stage string_to_shader_stage(const char* str);

/** @brief Converts the given string into a shader update frequency. Case-insensitive. */
KAPI shader_update_frequency string_to_shader_update_frequency(const char* str);
