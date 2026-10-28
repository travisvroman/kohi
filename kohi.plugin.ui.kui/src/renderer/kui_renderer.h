/**
 * @file kui_renderer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Types and functions for the renderer for the Kohi UI (KUI).
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <renderer/renderer_types.h>

#include "kui_types.h"

struct frame_data;

typedef struct kui_pass_data {
	kshader kui_shader;
} kui_pass_data;

/**
 * @brief Represents the state of the Standard UI renderer.
 */
typedef struct kui_renderer {

	struct renderer_system_state *renderer_state;
	struct texture_system_state *texture_system;

	krenderbuffer standard_vertex_buffer;
	krenderbuffer extended_vertex_buffer;
	krenderbuffer index_buffer;

	kui_pass_data kui_pass;

} kui_renderer;

KAPI b8 kui_renderer_create (kui_renderer *out_renderer);

KAPI void kui_renderer_destroy (kui_renderer *renderer);

KAPI b8 kui_renderer_render_frame (kui_renderer *renderer, struct frame_data *p_frame_data, kui_render_data *render_data);
