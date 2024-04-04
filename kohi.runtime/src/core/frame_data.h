#pragma once

#include "defines.h"
#include "kmemory.h"

struct linear_allocator;

/**
 * @brief Engine-level current frame-specific data.
 */
typedef struct frame_data {
    /** @brief The number of meshes drawn in the last frame. */
    u32 drawn_mesh_count;

    /** @brief The number of meshes drawn in the shadow pass in the last frame. */
    u32 drawn_shadow_mesh_count;

    /** @brief An allocator designed and used for per-frame allocations. */
    frame_allocator_int allocator;

    /** @brief The current renderer frame number, typically used for data synchronization. */
    u64 renderer_frame_number;

    /** @brief The draw index for this frame. Used to track queue submissions for this frame (renderer_begin()/end())/ */
    u8 draw_index;

    /** @brief The current render target index for renderers that use multiple render targets
     *  at once (i.e. Vulkan). For renderers that don't this will likely always be 0.
     */
    u64 render_target_index;

    /** @brief Application level frame specific data. Optional, up to the app to know how to use this if needed. */
    void* application_frame_data;
} frame_data;
