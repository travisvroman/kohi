#pragma once

#include "core_render_types.h"

/**
 * @brief Indicates if the given shader uniform type is a sampler.
 *
 * @param The type to examine.
 * @returns True if a sampler; otherwise false.
 */
KAPI b8 uniform_type_is_sampler(shader_uniform_type type);

/**
 * @brief Indicates if the given shader uniform type is a texture.
 *
 * @param The type to examine.
 * @returns True if a texture; otherwise false.
 */
KAPI b8 uniform_type_is_texture(shader_uniform_type type);

/** @brief Returns the string representation of the given texture repeat. */
KAPI const char* texture_repeat_to_string(texture_repeat repeat);

/** @brief Converts the given string into a texture repeat. Case-insensitive. */
KAPI texture_repeat string_to_texture_repeat(const char* str);

/** @brief Returns the string representation of the given texture filter. */
KAPI const char* texture_filter_mode_to_string(texture_filter filter);

/** @brief Converts the given string into a texture filter. Case-insensitive. */
KAPI texture_filter string_to_texture_filter_mode(const char* str);

KAPI const char* texture_channel_to_string(texture_channel channel);

KAPI texture_channel string_to_texture_channel(const char* str);

/** @brief Returns the string representation of the given shader uniform type. */
KAPI const char* shader_uniform_type_to_string(shader_uniform_type type);

/** @brief Converts the given string into a shader uniform type. Case-insensitive. */
KAPI shader_uniform_type string_to_shader_uniform_type(const char* str);

/** @brief Returns the string representation of the given shader attribute type. */
KAPI const char* shader_attribute_type_to_string(shader_attribute_type type);

/** @brief Converts the given string into a shader attribute type. Case-insensitive. */
KAPI shader_attribute_type string_to_shader_attribute_type(const char* str);

/** @brief Returns the string representation of the given shader stage. */
KAPI const char* shader_stage_to_string(shader_stage stage);

/** @brief Converts the given string into a shader stage. Case-insensitive. */
KAPI shader_stage string_to_shader_stage(const char* str);

/** @brief Returns the string representation of the given shader update frequency. */
KAPI const char* shader_update_frequency_to_string(shader_update_frequency frequency);

/** @brief Converts the given string into a shader update frequency. Case-insensitive. */
KAPI shader_update_frequency string_to_shader_update_frequency(const char* str);

/** @brief Returns the string representation of the given cull mode. */
KAPI const char* face_cull_mode_to_string(face_cull_mode mode);

/** @brief Converts the given string to a face cull mode. */
KAPI face_cull_mode string_to_face_cull_mode(const char* str);

/** @brief Returns the string representation of the given primitive topology type. */
KAPI const char* topology_type_to_string(primitive_topology_type_bits type);

/** @brief Converts the given string to a primitive topology type. */
KAPI primitive_topology_type_bits string_to_topology_type(const char* str);

/** @brief Returns the size in bytes of the attribute type. */
KAPI u16 size_from_shader_attribute_type(shader_attribute_type type);

/** @brief Returns the size in bytes of the uniform type. */
KAPI u16 size_from_shader_uniform_type(shader_uniform_type type);

/** @brief Returns the string representation of the given material type. */
KAPI const char* kmaterial_type_to_string(kmaterial_type type);

/** @brief Converts the given string into a material type. Case-insensitive. */
KAPI kmaterial_type string_to_kmaterial_type(const char* str);

/** @brief Returns the string representation of the given material model. */
KAPI const char* kmaterial_model_to_string(kmaterial_model model);

/** @brief Converts the given string into a material model. Case-insensitive. */
KAPI kmaterial_model string_to_kmaterial_model(const char* str);
