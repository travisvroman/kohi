#pragma once

#include "core_render_types.h"
#include <core_resource_types.h>
#include <defines.h>
#include <input_types.h>
#include <math/geometry.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/nine_slice.h>
#include <renderer/renderer_types.h>
#include <systems/font_system.h>

struct kui_state;

// Encodes both the control type as well as an index into the array of that type.
typedef struct {
	u32 val;
} kui_control;

#define INVALID_KUI_CONTROL ((kui_control){U32_MAX})

typedef enum kui_renderable_type {
	KUI_RENDERABLE_TYPE_CONTROL,
	KUI_RENDERABLE_TYPE_STENCIL_CLIP_BEGIN,
	KUI_RENDERABLE_TYPE_STENCIL_CLIP_END,
	KUI_RENDERABLE_TYPE_SCISSOR_CLIP_BEGIN,
	KUI_RENDERABLE_TYPE_SCISSOR_CLIP_END
} kui_renderable_type;

typedef struct kui_renderable {
	kui_renderable_type type;
	// The per-control instance binding id for binding set 1.
	u32 binding_instance_id;
	ktexture atlas_override;
	geometry_render_data render_data;
	rect_2di clipping_area;
} kui_renderable;

typedef struct kui_render_data {
	ktexture colour_buffer;
	ktexture depth_stencil_buffer;
	mat4 view;
	mat4 projection;

	ktexture ui_atlas;
	u32 shader_set0_binding_instance_id;

	u32 renderable_count;
	kui_renderable *renderables;
} kui_render_data;

// Global UBO data for the KUI shader.
typedef struct kui_global_ubo {
	mat4 projection;
	mat4 view;
} kui_global_ubo;

// Immediate (i.e. every draw) data for the KUI shader.
typedef struct kui_immediate_data {
	mat4 model;
	vec4 diffuse_colour;
} kui_immediate_data;

typedef struct kui_mouse_event {
	mouse_buttons mouse_button;
	i16 x;
	i16 y;
	i16 local_x;
	i16 local_y;
	i16 delta_x;
	i16 delta_y;
	// Mouse wheel.
	i8 delta_z;
} kui_mouse_event;

typedef enum kui_keyboard_event_type {
	KUI_KEYBOARD_EVENT_TYPE_PRESS,
	KUI_KEYBOARD_EVENT_TYPE_RELEASE,
} kui_keyboard_event_type;

typedef struct kui_keyboard_event {
	keys key;
	kui_keyboard_event_type type;
} kui_keyboard_event;

typedef struct kui_checkbox_event {
	b8 checked;
} kui_checkbox_event;

typedef enum kui_clip_type {
	KUI_CLIP_TYPE_NONE,
	KUI_CLIP_TYPE_STENCIL,
	// Cheaper than stencil clipping, but limited to axis-aligned rectangle.
	// Applying rotation will break this.
	KUI_CLIP_TYPE_SCISSOR
} kui_clip_type;

typedef struct kui_clip_mask {
	kui_clip_type type;

	union {
		struct {
			u32 reference_id;
			ktransform transform;
			kgeometry geometry;
			geometry_render_data render_data;
		} stencil_data;
		struct {
			rect_2di clipping_area;
		} scissor_data;
	};
} kui_clipping_area;

typedef enum kui_control_flag_bits {
	KUI_CONTROL_FLAG_NONE = 0,
	KUI_CONTROL_FLAG_ACTIVE_BIT = 1 << 0,
	KUI_CONTROL_FLAG_VISIBLE_BIT = 1 << 1,
	KUI_CONTROL_FLAG_HOVERED_BIT = 1 << 2,
	KUI_CONTROL_FLAG_PRESSED_BIT = 1 << 3,
	KUI_CONTROL_FLAG_FOCUSABLE_BIT = 1 << 4,
	KUI_CONTROL_FLAG_IS_DRAGGING_BIT = 1 << 5,
	KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT = 1 << 6,
	KUI_CONTROL_FLAG_USER_DATA_FREE_ON_DESTROY_BIT = 1 << 7,
	// Control clips children
	KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT = 1 << 8,
	KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT = 1 << 9
} kui_control_flag_bits;

typedef u32 kui_control_flags;

/**
 * The mouse event handler callback for a control.
 * @returns True if the event should be allowed to propagate to other controls; otherwise false.
 */
typedef b8 (*PFN_mouse_event_callback)(struct kui_state *state, kui_control self, struct kui_mouse_event event);
typedef void (*PFN_keyboard_event_callback)(struct kui_state *state, kui_control self, struct kui_keyboard_event event);
typedef void (*PFN_checkbox_event_callback)(struct kui_state *state, kui_control self, kui_checkbox_event event);

typedef enum kui_control_type {
	KUI_CONTROL_TYPE_NONE, // indicates a "free" slot in the internal arrays
	KUI_CONTROL_TYPE_BASE,
	KUI_CONTROL_TYPE_PANEL,
	KUI_CONTROL_TYPE_LABEL,
	KUI_CONTROL_TYPE_BUTTON,
	KUI_CONTROL_TYPE_TEXTBOX,
	KUI_CONTROL_TYPE_TREE_ITEM,
	KUI_CONTROL_TYPE_SCROLLABLE,
	KUI_CONTROL_TYPE_IMAGE_BOX,
	KUI_CONTROL_TYPE_CHECKBOX,
	KUI_CONTROL_TYPE_FRAME,

	KUI_CONTROL_TYPE_MAX = 64
} kui_control_type;

typedef struct kui_base_control {
	kui_control_type type;
	// A copy of the handle for reverse lookups.
	kui_control handle;
	ktransform ktransform;
	char *name;

	kui_control_flags flags;

	// How deep in the hierarchy the control is.
	u32 depth;

	rect_2d bounds;

	kui_clipping_area clipping_area;

	kui_control parent;
	// darray
	kui_control *children;

	memory_tag user_data_memory_tag;
	void *user_data;
	u64 user_data_size;

	void (*destroy)(struct kui_state *state, kui_control *self);

	b8 (*update)(struct kui_state *state, kui_control self, struct frame_data *p_frame_data);
	b8 (*render)(struct kui_state *state, kui_control self, struct frame_data *p_frame_data, struct kui_render_data *reneder_data);

	void (*active_changed)(struct kui_state *state, kui_control self, b8 is_active);

	/**
	 * The click handler for a control.
	 * @param self A pointer to the control.
	 * @param event The mouse event.
	 * @returns True if the event should be allowed to propagate to other controls; otherwise false.
	 */
	PFN_mouse_event_callback on_click;
	PFN_mouse_event_callback on_mouse_down;
	PFN_mouse_event_callback on_mouse_up;
	PFN_mouse_event_callback on_mouse_over;
	PFN_mouse_event_callback on_mouse_out;
	PFN_mouse_event_callback on_mouse_move;
	PFN_mouse_event_callback on_mouse_drag_begin;
	PFN_mouse_event_callback on_mouse_drag;
	PFN_mouse_event_callback on_mouse_drag_end;
	PFN_mouse_event_callback on_mouse_wheel;

	void (*on_focus)(struct kui_state *state, kui_control self);
	void (*on_unfocus)(struct kui_state *state, kui_control self);

	PFN_mouse_event_callback internal_click;
	PFN_mouse_event_callback internal_mouse_over;
	PFN_mouse_event_callback internal_mouse_out;
	PFN_mouse_event_callback internal_mouse_down;
	PFN_mouse_event_callback internal_mouse_up;
	PFN_mouse_event_callback internal_mouse_move;
	PFN_mouse_event_callback internal_mouse_drag_begin;
	PFN_mouse_event_callback internal_mouse_drag;
	PFN_mouse_event_callback internal_mouse_drag_end;
	PFN_mouse_event_callback internal_mouse_wheel;

	PFN_keyboard_event_callback on_key;

} kui_base_control;

typedef struct kui_panel_control {
	kui_base_control base;
	vec4 colour;
	kgeometry g;
	u32 binding_instance_id;
	b8 is_dirty;
} kui_panel_control;

typedef enum kui_label_flag_bits {
	KUI_LABEL_FLAG_NONE = 0,
	KUI_LABEL_FLAG_WRAP_BIT = 1 << 0,
	KUI_LABEL_FLAG_TRUNCATE_BIT = 1 << 1,
	KUI_LABEL_FLAG_TRUNCATE_ELLIPSIS_BIT = 1 << 2
} kui_label_flag_bits;

typedef u32 kui_label_flags;

typedef struct kui_label_control {
	kui_base_control base;
	vec2i size;
	vec4 colour;
	f32 max_width;
	kui_label_flags flags;
	u32 binding_instance_id;

	font_type type;
	// Only used when set to use a bitmap font.
	khandle bitmap_font;
	// Only used when set to use a system font.
	system_font_variant system_font;

	u64 vertex_buffer_offset;
	u64 index_buffer_offset;
	u64 vertex_buffer_size;
	u64 index_buffer_size;
	char *text;
	u32 max_text_length;
	u32 quad_count;
	u32 max_quad_count;

	b8 is_dirty;
} kui_label_control;

typedef enum kui_button_type {
	// Just a regular button - no content like text or image.
	KUI_BUTTON_TYPE_BASIC,
	KUI_BUTTON_TYPE_TEXT,
	KUI_BUTTON_TYPE_UPARROW,
	KUI_BUTTON_TYPE_DOWNARROW
} kui_button_type;

typedef struct kui_button_control {
	kui_base_control base;
	kui_button_type button_type;

	vec4 colour;
	nine_slice nslice;
	u32 binding_instance_id;

	kui_control label;
} kui_button_control;

typedef enum kui_textbox_type {
	KUI_TEXTBOX_TYPE_STRING,
	KUI_TEXTBOX_TYPE_INT,
	KUI_TEXTBOX_TYPE_FLOAT
} kui_textbox_type;

typedef struct kui_textbox_event_listener {
	struct kui_state *state;
	kui_control control;
} kui_textbox_event_listener;

typedef struct kui_textbox_control {
	kui_base_control base;
	vec2i size;
	vec4 colour;
	kui_textbox_type type;
	nine_slice nslice;
	nine_slice focused_nslice;
	u32 binding_instance_id;
	kui_control content_label;
	kui_control cursor;
	kui_control highlight_box;
	range32 highlight_range;
	u32 cursor_position;
	f32 text_view_offset;

	// Cached copy of the internal label's line height (taken in turn from its font.)
	f32 label_line_height;

	struct kui_textbox_event_listener *listener;
} kui_textbox_control;

typedef struct kui_frame_control {
	kui_base_control base;
	vec2i size;
	vec4 colour;
	nine_slice nslice;
	u32 binding_instance_id;
} kui_frame_control;

typedef struct kui_tree_item_control {
	kui_base_control base;
	vec2i size;
	vec4 colour;
	u32 binding_instance_id;

	kui_control toggle_button;
	kui_control label;

	kui_control child_container;

	u64 context;

	PFN_mouse_event_callback on_expanded;
	PFN_mouse_event_callback on_collapsed;

} kui_tree_item_control;

struct kui_scrollable_control;

typedef struct kui_scrollbar {
	struct kui_scrollable_control *owner;

	f32 drag_button_offset_start;
	f32 drag_button_mouse_offset;

	nine_slice bg;
	ktransform bg_transform;
	u32 bg_binding_instance_id;
	// up or left
	kui_control dec_button;
	// down or right
	kui_control inc_button;
	kui_control thumb_button;
} kui_scrollbar;

typedef struct kui_scrollable_control {
	kui_base_control base;
	b8 is_dirty;
	b8 scroll_x;
	b8 scroll_y;

	// What actually holds all controls.
	kui_control content_wrapper;

	f32 scrollbar_width;

	// TODO: scrollbar x
	/* kui_scrollbar scrollbar_x; */
	kui_scrollbar scrollbar_y;

	vec2 offset;
	vec2 min_offset;

	// HACK: Use proper kui events so we don't have to do this
	struct kui_state *kui_state;
} kui_scrollable_control;

typedef struct kui_image_box_control {
	kui_base_control base;
	kgeometry geometry;
	ktexture texture;

	u32 binding_instance_id;
} kui_image_box_control;

typedef enum kui_checkbox_state {
	KUI_CHECKBOX_STATE_ENABLED_UNCHECKED,
	KUI_CHECKBOX_STATE_ENABLED_CHECKED,
	KUI_CHECKBOX_STATE_DISABLED_UNCHECKED,
	KUI_CHECKBOX_STATE_DISABLED_CHECKED,
} kui_checkbox_state;

typedef struct kui_checkbox_control {
	kui_base_control base;

	kui_control check_image;
	kui_control label;

	PFN_checkbox_event_callback on_checked_changed;

	kui_checkbox_state state;
} kui_checkbox_control;

// Atlas configuration

typedef struct kui_atlas_panel_control_config {
	extents_2d extents;
} kui_atlas_panel_control_config;

typedef struct kui_atlas_button_control_mode_config {
	extents_2d extents;
	vec2 corner_size;
	vec2 corner_px_size;
} kui_atlas_button_control_mode_config;

typedef struct kui_atlas_button_control_config {
	kui_atlas_button_control_mode_config normal;
	kui_atlas_button_control_mode_config hover;
	kui_atlas_button_control_mode_config pressed;
} kui_atlas_button_control_config;

typedef struct kui_atlas_textbox_control_mode_config {
	extents_2d extents;
	vec2 corner_size;
	vec2 corner_px_size;
} kui_atlas_textbox_control_mode_config;

typedef struct kui_atlas_textbox_control_config {
	kui_atlas_textbox_control_mode_config normal;
	kui_atlas_textbox_control_mode_config focused;
} kui_atlas_textbox_control_config;

typedef struct kui_atlas_scrollbar_bg_config {
	extents_2d extents;
	vec2 corner_size;
	vec2 corner_px_size;
} kui_atlas_scrollbar_bg_config;

typedef struct kui_atlas_checkbox_config {
	vec2i image_box_size;
	rect_2di enabled_unchecked_rect;
	rect_2di enabled_checked_rect;
	rect_2di disabled_unchecked_rect;
	rect_2di disabled_checked_rect;
} kui_atlas_checkbox_config;

typedef kui_atlas_textbox_control_mode_config kui_atlas_frame_control_config;

typedef struct kui_atlas_config {
	kname image_asset_name;
	kname image_asset_package_name;

	kui_atlas_panel_control_config panel;
	kui_atlas_button_control_config button;
	kui_atlas_button_control_config button_uparrow;
	kui_atlas_button_control_config button_downarrow;
	kui_atlas_textbox_control_config textbox;
	kui_atlas_scrollbar_bg_config scrollbar;
	kui_atlas_checkbox_config checkbox;
	kui_atlas_frame_control_config frame;
} kui_atlas_config;
