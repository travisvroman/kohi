/**
 * @file vulkan_renderpass.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the implementation of a Vulkan renderpass, which defines
 * attachments, potential clearing, and render area for the next drawing commands.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief The types of clearing to be done on a renderpass.
 * Can be combined together for multiple clearing functions.
 */
typedef enum renderpass_clear_flag {
    /** @brief No clearing shoudl be done. */
    RENDERPASS_CLEAR_NONE_FLAG = 0x0,
    /** @brief Clear the colour buffer. */
    RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG = 0x1,
    /** @brief Clear the depth buffer. */
    RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG = 0x2,
    /** @brief Clear the stencil buffer. */
    RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG = 0x4
} renderpass_clear_flag;

/**
 * @brief Creates a new renderpass.
 * 
 * @param context A pointer to the Vulkan context.
 * @param out_renderpass A pointer to hold the newly-created renderpass.
 * @param render_area A rectangle to represent the render area. x and y are position, 
 * z is width and w is height.
 * @param clear_colour The colour to be used when clearing the colour buffer (RGBA, 0.0-1.0 range).
 * @param depth The depth clear amount.
 * @param stencil The stencil clear value.
 * @param clear_flags The combined clear flags indicating what kind of clear should take place.
 * @param has_prev_pass Indicates if there is a previous renderpass.
 * @param has_next_pass Indicates if there is a next renderpass.
 */
void vulkan_renderpass_create(
    vulkan_context* context,
    vulkan_renderpass* out_renderpass,
    vec4 render_area,
    vec4 clear_colour,
    f32 depth,
    u32 stencil,
    u8 clear_flags,
    b8 has_prev_pass,
    b8 has_next_pass);

/**
 * @brief Destroys the given renderpass.
 * 
 * @param context A pointer to the Vulkan context.
 * @param renderpass A pointer to the renderpass to be destroyed.
 */
void vulkan_renderpass_destroy(vulkan_context* context, vulkan_renderpass* renderpass);

/**
 * @brief Begins the given renderpass.
 * 
 * @param command_buffer A pointer to the command buffer to be used.
 * @param renderpass A pointer to the renderpass to begin.
 * @param frame_buffer The framebuffer to be used for the being of the renderpass.
 */
void vulkan_renderpass_begin(
    vulkan_command_buffer* command_buffer,
    vulkan_renderpass* renderpass,
    VkFramebuffer frame_buffer);

/**
 * @brief Ends the given renderpass.
 * 
 * @param command_buffer A pointer to the command buffer to be used.
 * @param renderpass A pointer to the renderpass to end.
 */
void vulkan_renderpass_end(vulkan_command_buffer* command_buffer, vulkan_renderpass* renderpass);
