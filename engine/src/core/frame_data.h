#pragma once

#include "defines.h"

struct linear_allocator;

/**
 * @brief Engine-level current frame-specific data.
 */
typedef struct frame_data {
    /** @brief The time in seconds since the last frame. */
    f32 delta_time;

    /** @brief The total amount of time in seconds the application has been running. */
    f64 total_time;

    /** @brief A pointer to the engine's frame allocator. */
    struct linear_allocator* frame_allocator;

    /** @brief Application level frame specific data. Optional, up to the app to know how to use this if needed. */
    void* application_frame_data;
} frame_data;
