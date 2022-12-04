/**
 * @file render_view_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system for managing views.
 * @version 1.0
 * @date 2022-05-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "renderer/renderer_types.inl"

/** @brief The configuration for the render view system. */
typedef struct render_view_system_config {
    /** @brief The maximum number of views that can be registered with the system. */
    u16 max_view_count;
} render_view_system_config;

/**
 * @brief Initializes the render view system. Call twice; once to obtain memory
 * requirement (where state=0) and a second time with allocated memory passed to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement in bytes.
 * @param state A block of memory to be used for the state.
 * @param config Configuration for the system.
 * @return True on success; otherwise false.
 */
b8 render_view_system_initialize(u64* memory_requirement, void* state, render_view_system_config config);

/**
 * @brief Shuts the render view system down.
 *
 * @param state The block of state memory.
 */
void render_view_system_shutdown(void* state);

/**
 * @brief Creates a new view using the provided config. The new
 * view may then be obtained via a call to render_view_system_get.
 *
 * @param config A constant pointer to the view configuration.
 * @return True on success; otherwise false.
 */
KAPI b8 render_view_system_create(const render_view_config* config);

/**
 * @brief Called when the owner of this view (i.e. the window) is resized.
 *
 * @param width The new width in pixels.
 * @param width The new height in pixels.
 */
KAPI void render_view_system_on_window_resize(u32 width, u32 height);

/**
 * @brief Obtains a pointer to a view with the given name.
 *
 * @param name The name of the view.
 * @return A pointer to a view if found; otherwise 0.
 */
KAPI render_view* render_view_system_get(const char* name);

/**
 * @brief Builds a render view packet using the provided view and meshes.
 *
 * @param view A pointer to the view to use.
 * @param frame_allocator An allocator used this frame to build a packet.
 * @param data Freeform data used to build the packet.
 * @param out_packet A pointer to hold the generated packet.
 * @return True on success; otherwise false.
 */
KAPI b8 render_view_system_build_packet(const render_view* view, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet);

/**
 * @brief Uses the given view and packet to render the contents therein.
 *
 * @param view A pointer to the view to use.
 * @param packet A pointer to the packet whose data is to be rendered.
 * @param frame_number The current renderer frame number, typically used for data synchronization.
 * @param render_target_index The current render target index for renderers that use multiple render targets at once (i.e. Vulkan).
 * @return True on success; otherwise false.
 */
KAPI b8 render_view_system_on_render(const render_view* view, const render_view_packet* packet, u64 frame_number, u64 render_target_index);

KAPI void render_view_system_regenerate_render_targets(render_view* view);
