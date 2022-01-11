/**
 * @file vulkan_ui_shader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains the implementation of the Vulkan UI shader, which is used for
 * drawing most UI objects.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer/vulkan/vulkan_types.inl"
#include "renderer/renderer_types.inl"

/**
 * @brief Creates a new Vulkan UI shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param out_shader A pointer to hold the newly created shader.
 * @return True on success; otherwise false.
 */
b8 vulkan_ui_shader_create(vulkan_context* context, vulkan_ui_shader* out_shader);

/**
 * @brief Destroys the given Vulkan UI shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be destroyed.
 */
void vulkan_ui_shader_destroy(vulkan_context* context, struct vulkan_ui_shader* shader);

/**
 * @brief "Uses" the shader, binding its internal pipeline.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be used.
 */
void vulkan_ui_shader_use(vulkan_context* context, struct vulkan_ui_shader* shader);

/**
 * @brief Updates the global state of the given shader, setting items such as view and projection matrices.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be updated.
 * @param delta_time The amount of time in seconds since the last frame.
 */
void vulkan_ui_shader_update_global_state(vulkan_context* context, struct vulkan_ui_shader* shader, f32 delta_time);

/**
 * @brief Sets the model matrix on the given shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to set the matrix on.
 * @param model A copy of the model matrix to be set.
 */
void vulkan_ui_shader_set_model(vulkan_context* context, struct vulkan_ui_shader* shader, mat4 model);

/**
 * @brief Applies various properties of the given material to the provided shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to which the material should be applied.
 * @param material A pointer to the material to apply.
 */
void vulkan_ui_shader_apply_material(vulkan_context* context, struct vulkan_ui_shader* shader, material* material);

/**
 * @brief Acquires internal resources, such as descriptors, for the given material from the provided shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader whose resources should be acquired from.
 * @param material A pointer to the material to acquire resources for.
 * @return True on success; otherwise false.
 */
b8 vulkan_ui_shader_acquire_resources(vulkan_context* context, struct vulkan_ui_shader* shader, material* material);

/**
 * @brief Releases internal resources for the given material from the provided shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader whose resources should be released from.
 * @param material A pointer to the material to release resources for.
 */
void vulkan_ui_shader_release_resources(vulkan_context* context, struct vulkan_ui_shader* shader, material* material);
