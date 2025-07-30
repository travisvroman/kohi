/**
 * @file standard_ui_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The standard UI system is responsible for managing standard ui elements throughout the engine.
 * This is an example of a retained-mode UI.
 * @version 1.0
 * @date 2023-09-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "core_resource_types.h"
#include <defines.h>
#include <identifiers/identifier.h>
#include <input_types.h>
#include <kresources/kresource_types.h>
#include <math/geometry.h>
#include <math/math_types.h>
#include <renderer/renderer_types.h>
#include <systems/xform_system.h>

struct frame_data;
struct standard_ui_state;
struct renderer_system_state;

/** @brief The standard UI system configuration. */
typedef struct standard_ui_system_config {
    u64 max_control_count;
} standard_ui_system_config;

typedef struct standard_ui_renderable {
    u32* group_id;
    u32* per_draw_id;
    ktexture atlas_override;
    geometry_render_data render_data;
    geometry_render_data* clip_mask_render_data;
} standard_ui_renderable;

typedef struct standard_ui_render_data {
    ktexture ui_atlas;
    // darray
    standard_ui_renderable* renderables;
} standard_ui_render_data;

typedef struct sui_mouse_event {
    mouse_buttons mouse_button;
    i16 x;
    i16 y;
} sui_mouse_event;

typedef enum sui_keyboard_event_type {
    SUI_KEYBOARD_EVENT_TYPE_PRESS,
    SUI_KEYBOARD_EVENT_TYPE_RELEASE,
} sui_keyboard_event_type;

typedef struct sui_keyboard_event {
    keys key;
    sui_keyboard_event_type type;
} sui_keyboard_event;

typedef struct sui_clip_mask {
    u32 reference_id;
    ktransform clip_xform;
    kgeometry clip_geometry;
    geometry_render_data render_data;
} sui_clip_mask;

typedef struct sui_control {
    identifier id;
    ktransform xform;
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

    void* user_data;
    u64 user_data_size;

    void (*destroy)(struct standard_ui_state* state, struct sui_control* self);
    b8 (*load)(struct standard_ui_state* state, struct sui_control* self);
    void (*unload)(struct standard_ui_state* state, struct sui_control* self);

    b8 (*update)(struct standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
    void (*render_prepare)(struct standard_ui_state* state, struct sui_control* self, const struct frame_data* p_frame_data);
    b8 (*render)(struct standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* reneder_data);

    /**
     * The click handler for a control.
     * @param self A pointer to the control.
     * @param event The mouse event.
     * @returns True if the event should be allowed to propagate to other controls; otherwise false.
     */
    void (*on_click)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*on_mouse_down)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*on_mouse_up)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*on_mouse_over)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*on_mouse_out)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*on_mouse_move)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

    void (*internal_click)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*internal_mouse_over)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*internal_mouse_out)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*internal_mouse_down)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*internal_mouse_up)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);
    void (*internal_mouse_move)(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event);

    void (*on_key)(struct standard_ui_state* state, struct sui_control* self, struct sui_keyboard_event event);

} sui_control;

typedef struct standard_ui_state {
    struct renderer_system_state* renderer;
    struct font_system_state* font_system;
    standard_ui_system_config config;
    // Array of pointers to controls, the system does not own these. The application does.
    u32 total_control_count;
    u32 active_control_count;
    sui_control** active_controls;
    u32 inactive_control_count;
    sui_control** inactive_controls;
    sui_control root;
    // texture_map ui_atlas;

    ktexture atlas_texture;

    u64 focused_id;

} standard_ui_state;

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
KAPI b8 standard_ui_system_initialize(u64* memory_requirement, standard_ui_state* state, standard_ui_system_config* config);

/**
 * @brief Shuts down the standard UI system.
 *
 * @param state The state block of memory.
 */
KAPI void standard_ui_system_shutdown(standard_ui_state* state);

KAPI b8 standard_ui_system_update(standard_ui_state* state, struct frame_data* p_frame_data);

KAPI void standard_ui_system_render_prepare_frame(standard_ui_state* state, const struct frame_data* p_frame_data);

KAPI b8 standard_ui_system_render(standard_ui_state* state, sui_control* root, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI b8 standard_ui_system_update_active(standard_ui_state* state, sui_control* control);

KAPI b8 standard_ui_system_register_control(standard_ui_state* state, sui_control* control);

KAPI b8 standard_ui_system_control_add_child(standard_ui_state* state, sui_control* parent, sui_control* child);

KAPI b8 standard_ui_system_control_remove_child(standard_ui_state* state, sui_control* parent, sui_control* child);

KAPI void standard_ui_system_focus_control(standard_ui_state* state, sui_control* control);

// ---------------------------
// Base control
// ---------------------------
KAPI b8 sui_base_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control);
KAPI void sui_base_control_destroy(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_base_control_load(standard_ui_state* state, struct sui_control* self);
KAPI void sui_base_control_unload(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_base_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_base_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

/**
 * @brief Sets the position on the given control.
 *
 * @param self A pointer to the control whose position will be set.
 * @param position The position to be set.
 */
KAPI void sui_control_position_set(standard_ui_state* state, struct sui_control* self, vec3 position);

/**
 * @brief Gets the position on the given control.
 *
 * @param u_text A pointer to the control whose position will be retrieved.
 * @param The position of the given control.
 */
KAPI vec3 sui_control_position_get(standard_ui_state* state, struct sui_control* self);
