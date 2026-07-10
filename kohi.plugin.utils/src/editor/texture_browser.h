#pragma once

#include <core_render_types.h>
#include <defines.h>
#include <kui_types.h>
#include <strings/kname.h>
#include <systems/texture_system.h>

struct kui_state;

typedef enum texture_browser_flag_bits {
	TEXTURE_BROWSER_FLAG_NONE = 0,
	TEXTURE_BROWSER_FLAG_OPEN_BIT = 1 << 0,
	TEXTURE_BROWSER_FLAG_SELECTING_BIT = 1 << 1
} texture_browser_flag_bits;

typedef u32 texture_browser_flags;

typedef void (*PFN_tex_browser_selected_callback)(ktexture texture, void *context);
typedef void (*PFN_tex_browser_cancelled_callback)(void *context);

typedef struct texture_browser {
	struct kui_state *ui;
	// The root editor control.
	kui_control editor_root;
	// Size of font used for most UI controls.
	u16 font_size;
	// Name of the font used for most UI controls.
	kname font_name;
	// Size of font used for texbox UI controls.
	u16 textbox_font_size;
	// Name of the font used for texbox UI controls.
	kname textbox_font_name;
	// The name of the game package.
	kname game_package_name;

	kui_control bg_panel;
	vec2 window_size;
	kui_control title;
	vec2i min_size;
	f32 right_col_x;
	kui_control search_label;
	kui_control search_textbox;
	kui_control search_game_pack_only_checkbox;
	kname search_package_name;
	kui_control scrollable_control;
	kui_control content_container;
	u32 tex_count;
	char *search_text;
	kui_control *image_boxes;
	kui_control *labels;
	kui_control selected_frame;
	f32 imagebox_size;
	f32 imagebox_padding;
	vec2 tex_tile_size;
	// The currently-selected texture in the texture browser.
	ktexture selected_texture;
	texture_browser_flags flags;

	void *selection_context;
	PFN_tex_browser_selected_callback selected_callback;
	PFN_tex_browser_cancelled_callback cancelled_callback;

	kui_control inspector_preview_imagebox;
	kui_control inspector_label;
	kui_control confirm_btn;
	kui_control cancel_btn;
	kui_control import_btn;
} texture_browser;

typedef struct texture_browser_create_info {
	// A pointer to the ui system state.
	struct kui_state *ui;
	// The root editor control.
	kui_control editor_root;
	// The name of the game package.
	kname game_package_name;
	// Size of font used for most UI controls.
	u16 font_size;
	// Name of the font used for most UI controls.
	kname font_name;
	// Size of font used for texbox UI controls.
	u16 textbox_font_size;
	// Name of the font used for texbox UI controls.
	kname textbox_font_name;
} texture_browser_create_info;

typedef struct texure_browser_element_data {
	texture_browser *tb;
	kname texture_name;
	ktexture texture;
	ktexture_properties properties;
} texture_browser_element_data;

void texture_browser_create (texture_browser *tb, texture_browser_create_info create_info);
void texture_browser_destroy (texture_browser *tb);

b8 texture_browser_is_open (const texture_browser *tb);

void texture_browser_open_for_browsing (texture_browser *tb);
void texture_browser_open_for_selection (texture_browser *tb, void *context, PFN_tex_browser_selected_callback selected_callback, PFN_tex_browser_cancelled_callback cancelled_callback);
void texture_browser_close (texture_browser *tb);
void texture_browser_refresh (texture_browser *tb);
