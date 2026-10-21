#pragma once

#include "core_render_types.h"
#include "defines.h"
#include "memory/kmemory.h"

struct linear_allocator;

struct application_frame_data;
struct kforward_renderer_render_data;
struct kui_render_data;

typedef struct frame_metrics_render_event {
	char event_name[128];
	colour4 colour;
	f32 duration_ms;
	i8 depth;
} frame_metrics_render_event;

typedef struct frame_metrics_data {
	u32 render_event_count;
	// Dynamically allocated, but every frame using the frame allocator.
	frame_metrics_render_event *render_events;
} frame_metrics_data;

/**
 * @brief Engine-level current frame-specific data.
 */
typedef struct frame_data {

	/** @brief The number of meshes drawn in the last frame. */
	u32 drawn_mesh_count;

	/** @brief The number of meshes drawn in the shadow pass in the last frame. */
	u32 drawn_shadow_mesh_count;

	u32 drawn_hft_block_count;
	u32 drawn_hft_chunk_count;

	/** @brief An allocator designed and used for per-frame allocations. */
	frame_allocator_int allocator;

	/** @brief Application level frame specific data. Optional, up to the app to know how to use this if needed. */
	struct application_frame_data *app_frame_data;

	struct kui_render_data *kui_render_data;
	struct kforward_renderer_render_data *render_data;

	frame_metrics_data metrics;
} frame_data;
