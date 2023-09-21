#pragma once

#include "defines.h"

struct linear_allocator;

typedef struct frame_allocator_int {
    void* (*allocate)(u64 size);
    void (*free)(void* block, u64 size);
    void (*free_all)(void);
} frame_allocator_int;

/**
 * @brief Engine-level current frame-specific data.
 */
typedef struct frame_data {
    /** @brief The time in seconds since the last frame. */
    f32 delta_time;

    /** @brief The total amount of time in seconds the application has been running. */
    f64 total_time;

    /** @brief The number of meshes drawn in the last frame. */
    u32 drawn_mesh_count;

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
