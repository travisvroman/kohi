#pragma once

#include "defines.h"
#include "memory/kmemory.h"

struct linear_allocator;

struct application_frame_data;
struct kforward_renderer_render_data;
struct kui_render_data;

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

	/** @brief Application level frame specific data. Optional, up to the app to know how to use this if needed. */
	struct application_frame_data *app_frame_data;

	struct kui_render_data *kui_render_data;
	struct kforward_renderer_render_data *render_data;

} frame_data;
