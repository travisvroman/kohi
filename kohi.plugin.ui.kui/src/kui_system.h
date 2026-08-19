/**
 * @file kui_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The Kohi UI system (kui) is responsible for managing standard ui elements throughout the engine.
 * This is an example of a retained-mode UI.
 * @version 2.0
 * @date 2026-01-16
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <defines.h>
#include <identifiers/identifier.h>
#include <input_types.h>
#include <math/geometry.h>
#include <math/math_types.h>
#include <renderer/renderer_types.h>
#include <systems/ktransform_system.h>

#include "kui_types.h"
#include "memory/kmemory.h"

struct frame_data;
struct renderer_system_state;

/** @brief The Kohi UI system configuration. */
typedef struct kui_system_config {
	u32 dummy;
} kui_system_config;

typedef struct kui_state {
	struct renderer_system_state *renderer;
	struct font_system_state *font_system;
	kui_system_config config;

	b8 running;

	kshader shader;
	u32 shader_set0_binding_instance_id;
	// Array of pointers to controls, the system does not own these. The application does.
	u32 total_control_count;
	kui_control *active_controls;
	kui_control *inactive_controls;
	kui_control root;
	// texture_map ui_atlas;

	colour4 focused_base_colour;
	colour4 unfocused_base_colour;

	ktexture atlas_texture;
	uvec2 atlas_texture_size;
	kui_atlas_config atlas;

	krenderbuffer vertex_buffer;
	krenderbuffer index_buffer;

	kui_control focused;

	kui_base_control *base_controls;
	kui_panel_control *panel_controls;
	kui_label_control *label_controls;
	kui_button_control *button_controls;
	kui_textbox_control *textbox_controls;
	kui_tree_item_control *tree_item_controls;
	kui_scrollable_control *scrollable_controls;
	kui_image_box_control *image_box_controls;
	kui_checkbox_control *checkbox_controls;
	kui_frame_control *frame_controls;
} kui_state;

/**
 * @brief Initializes the standard UI system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (kui_system_config) for this system.
 * @return True on success; otherwise false.
 */
KAPI b8 kui_system_initialize (u64 *memory_requirement, kui_state *state, kui_system_config *config);

/**
 * @brief Shuts down the standard UI system.
 *
 * @param state The state block of memory.
 */
KAPI void kui_system_shutdown (kui_state *state);

KAPI b8 kui_system_update (kui_state *state, struct frame_data *p_frame_data);

// FIXME: combine the SUI renderer into here.
KAPI b8 kui_system_render (kui_state *state, kui_control root, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI kui_base_control *kui_system_get_base (kui_state *state, kui_control control);

KAPI b8 toggle_active (kui_state *state, kui_control control);

KAPI b8 kui_system_control_add_child (kui_state *state, kui_control parent, kui_control child);

KAPI b8 kui_system_control_remove_child (kui_state *state, kui_control parent, kui_control child);

// Pass KNULL to unfocus without focusing something new.
KAPI void kui_system_focus_control (kui_state *state, kui_control control);
KAPI b8 kui_system_is_control_focused (const kui_state *state, const kui_control control);

// ---------------------------
// Base control
// ---------------------------
KAPI kui_control kui_base_control_create (kui_state *state, const char *name, kui_control_type type);
KAPI void kui_base_control_destroy (kui_state *state, kui_control *self);

KAPI void kui_control_destroy_all_children (kui_state *state, kui_control control);

KAPI b8 kui_base_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_base_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, struct kui_render_data *render_data);

/**
 * @brief Checks control and its ancestors to see if it is active. More reliable than
 * checking just the control's is_active property.
 *
 * @return True if active; otherwise false.
 */
KAPI b8 kui_control_is_active (kui_state *state, kui_control self);

/**
 * @brief Checks control and its ancestors to see if it is visible. More reliable than
 * checking just the control's is_visible property.
 *
 * @return True if visible; otherwise false.
 */
KAPI b8 kui_control_is_visible (kui_state *state, kui_control self);

KAPI void kui_control_set_is_visible (kui_state *state, kui_control self, b8 is_visible);
KAPI void kui_control_set_is_active (kui_state *state, kui_control self, b8 is_active);

KAPI b8 kui_control_get_flag (kui_state *state, kui_control self, kui_control_flag_bits flag);
KAPI void kui_control_set_flag (kui_state *state, kui_control self, kui_control_flag_bits flag, b8 enabled);

KAPI void kui_control_set_user_data (kui_state *state, kui_control self, u32 data_size, void *data, b8 free_on_destroy, memory_tag tag);
KAPI void *kui_control_get_user_data (kui_state *state, kui_control self);

KAPI void kui_control_set_on_click (kui_state *state, kui_control self, PFN_mouse_event_callback on_click_callback);
KAPI void kui_control_set_on_key (kui_state *state, kui_control self, PFN_keyboard_event_callback on_key_callback);

/**
 * @brief Sets the position on the given control.
 *
 * @param self A pointer to the control whose position will be set.
 * @param position The position to be set.
 */
KAPI void kui_control_position_set (kui_state *state, kui_control self, vec3 position);

/**
 * @brief Gets the position on the given control.
 *
 * @param u_text A pointer to the control whose position will be retrieved.
 * @param The position of the given control.
 */
KAPI vec3 kui_control_position_get (kui_state *state, kui_control self);
