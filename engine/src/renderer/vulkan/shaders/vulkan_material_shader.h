/**
 * @file vulkan_material_shader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the implementation of the Vulkan material shader,
 * used for rendering objects in the game world.
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
 * @brief Creates a new Vulkan material shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param out_shader A pointer to hold the newly created shader.
 * @return True on success; otherwise false.
 */
b8 vulkan_material_shader_create(vulkan_context* context, vulkan_material_shader* out_shader);

/**
 * @brief Destroys the provided Vulkan material shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be destroyed.
 */
void vulkan_material_shader_destroy(vulkan_context* context, struct vulkan_material_shader* shader);

/**
 * @brief "Uses" the shader, binding its internal pipeline.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be used.
 */
void vulkan_material_shader_use(vulkan_context* context, struct vulkan_material_shader* shader);

/**
 * @brief Updates global properties of the shader, such as view and projection matrices.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to be updated.
 * @param delta_time The time in seconds that has passed since the last frame.
 */
void vulkan_material_shader_update_global_state(vulkan_context* context, struct vulkan_material_shader* shader, f32 delta_time);

/**
 * @brief Sets the model matrix on the shader.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader on which to set the matrix.
 * @param model A copy of the model matrix to be set.
 */
void vulkan_material_shader_set_model(vulkan_context* context, struct vulkan_material_shader* shader, mat4 model);

/**
 * @brief Applies various properties of the given material.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader on which to set the material properties.
 * @param material A pointer to the material to be used.
 */
void vulkan_material_shader_apply_material(vulkan_context* context, struct vulkan_material_shader* shader, material* material);

/**
 * @brief Acquires internal resources for the given material, such as descriptors.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to acquire resources from.
 * @param material A pointer to the material whose resources need to be acquired.
 * @return True on success; otherwise false.
 */
b8 vulkan_material_shader_acquire_resources(vulkan_context* context, struct vulkan_material_shader* shader, material* material);

/**
 * @brief Releases internal resources for the given material.
 * 
 * @param context A pointer to the Vulkan context.
 * @param shader A pointer to the shader to release resources from.
 * @param material A pointer to the material whose resources need to be released.
 */
void vulkan_material_shader_release_resources(vulkan_context* context, struct vulkan_material_shader* shader, material* material);
