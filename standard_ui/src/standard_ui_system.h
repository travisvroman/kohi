
/**
 * @file standard_ui_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The standard UI system is responsible for managing standard ui elements throughout the engine.
 * @version 1.0
 * @date 2023-09-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include <math/math_types.h>

#include "core/identifier.h"
#include "core/input.h"
#include "defines.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"

// FIXME: Need to maintain a list of extension types somewhere and pull from there.
#define K_SYSTEM_TYPE_STANDARD_UI_EXT 128

struct frame_data;

/** @brief The standard UI system configuration. */
typedef struct standard_ui_system_config {
    u64 max_control_count;
} standard_ui_system_config;

typedef struct standard_ui_renderable {
    u32* instance_id;
    u64* frame_number;
    texture_map* atlas_override;
    u8* draw_index;
    geometry_render_data render_data;
} standard_ui_renderable;

typedef struct standard_ui_render_data {
    texture_map* ui_atlas;
    // darray
    standard_ui_renderable* renderables;
} standard_ui_render_data;

typedef struct sui_mouse_event {
    buttons mouse_button;
    i16 x;
    i16 y;
} sui_mouse_event;

typedef struct sui_control {
    identifier id;
    transform xform;
    char* name;
    // TODO: Convert to flags.
    b8 is_active;
    b8 is_visible;
    b8 is_hovered;
    b8 is_pressed;
    rect_2d bounds;

    struct sui_control* parent;
    // darray
    struct sui_control** children;

    void* internal_data;
    u64 internal_data_size;

    void (*destroy)(struct sui_control* self);
    b8 (*load)(struct sui_control* self);
    void (*unload)(struct sui_control* self);

    b8 (*update)(struct sui_control* self, struct frame_data* p_frame_data);
    b8 (*render)(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* reneder_data);

    /**
     * The click handler for a control.
     * @param self A pointer to the control.
     * @param event The mouse event.
     * @returns True if the event should be allowed to propagate to other controls; otherwise false.
     */
    b8 (*on_click)(struct sui_control* self, struct sui_mouse_event event);
    b8 (*on_mouse_down)(struct sui_control* self, struct sui_mouse_event event);
    b8 (*on_mouse_up)(struct sui_control* self, struct sui_mouse_event event);

    b8 (*on_mouse_over)(struct sui_control* self, struct sui_mouse_event event);
    b8 (*on_mouse_out)(struct sui_control* self, struct sui_mouse_event event);
} sui_control;

/**
 * @brief Initializes the standard UI system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (standard_ui_system_config) for this system.
 * @return True on success; otherwise false.
 */
KAPI b8 standard_ui_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the standard UI system.
 *
 * @param state The state block of memory.
 */
KAPI void standard_ui_system_shutdown(void* state);

KAPI b8 standard_ui_system_update(void* state, struct frame_data* p_frame_data);

KAPI b8 standard_ui_system_render(void* state, sui_control* root, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI b8 standard_ui_system_update_active(void* state, sui_control* control);

KAPI b8 standard_ui_system_register_control(void* state, sui_control* control);

KAPI b8 standard_ui_system_control_add_child(void* state, sui_control* parent, sui_control* child);

KAPI b8 standard_ui_system_control_remove_child(void* state, sui_control* parent, sui_control* child);

// ---------------------------
// Base control
// ---------------------------
KAPI b8 sui_base_control_create(const char* name, struct sui_control* out_control);
KAPI void sui_base_control_destroy(struct sui_control* self);

KAPI b8 sui_base_control_load(struct sui_control* self);
KAPI void sui_base_control_unload(struct sui_control* self);

KAPI b8 sui_base_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_base_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

// ---------------------------
// Panel control
// ---------------------------

KAPI b8 sui_panel_control_create(const char* name, vec2 size, struct sui_control* out_control);
KAPI void sui_panel_control_destroy(struct sui_control* self);

KAPI b8 sui_panel_control_load(struct sui_control* self);
KAPI void sui_panel_control_unload(struct sui_control* self);

KAPI b8 sui_panel_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_panel_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI vec2 sui_panel_size(struct sui_control* self);
KAPI b8 sui_panel_control_resize(struct sui_control* self, vec2 new_size);
// ---------------------------
// Button control
// ---------------------------

KAPI b8 sui_button_control_create(const char* name, struct sui_control* out_control);
KAPI void sui_button_control_destroy(struct sui_control* self);
KAPI b8 sui_button_control_height_set(struct sui_control* self, i32 width);

KAPI b8 sui_button_control_load(struct sui_control* self);
KAPI void sui_button_control_unload(struct sui_control* self);

KAPI b8 sui_button_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_button_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI b8 sui_button_on_mouse_out(struct sui_control* self, struct sui_mouse_event event);
KAPI b8 sui_button_on_mouse_over(struct sui_control* self, struct sui_mouse_event event);
KAPI b8 sui_button_on_mouse_down(struct sui_control* self, struct sui_mouse_event event);
KAPI b8 sui_button_on_mouse_up(struct sui_control* self, struct sui_mouse_event event);

// ---------------------------
// Label control
// ---------------------------

KAPI b8 sui_label_control_create(const char* name, font_type type, const char* font_name, u16 font_size, const char* text, struct sui_control* out_control);
KAPI void sui_label_control_destroy(struct sui_control* self);
KAPI b8 sui_label_control_load(struct sui_control* self);
KAPI void sui_label_control_unload(struct sui_control* self);
KAPI b8 sui_label_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_label_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

/**
 * @brief Sets the position on the given label object.
 *
 * @param u_text A pointer to the label whose text will be set.
 * @param text The position to be set.
 */
KAPI void sui_label_position_set(struct sui_control* self, vec3 position);
/**
 * @brief Sets the text on the given label object.
 *
 * @param u_text A pointer to the label whose text will be set.
 * @param text The text to be set.
 */
KAPI void sui_label_text_set(struct sui_control* self, const char* text);

KAPI const char* sui_label_text_get(struct sui_control* self);
