#include <containers/darray.h>
#include <controls/checkbox_control.h>
#include <controls/image_box_control.h>
#include <controls/kui_button.h>
#include <controls/kui_frame.h>
#include <controls/kui_label.h>
#include <controls/kui_panel.h>
#include <controls/kui_scrollable.h>
#include <controls/kui_textbox.h>
#include <core/engine.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <importers/kasset_importer_image.h>
#include <kui_system.h>
#include <kui_types.h>
#include <logger.h>
#include <math/kmath.h>
#include <platform/kpackage.h>
#include <platform/platform.h>
#include <platform/vfs.h>
#include <strings/kstring.h>
#include <systems/asset_system.h>
#include <systems/texture_system.h>
#include <utils/render_type_utils.h>

#include "assets/kasset_types.h"
#include "strings/kname.h"
#include "texture_browser.h"

static void texture_browser_open_internal (texture_browser *tb);
static b8 texture_browser_confirm_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 texture_browser_cancel_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 texture_browser_import_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static void texture_browser_search_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void texture_browser_search_game_pak_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static b8 texture_browser_imagebox_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);

void texture_browser_create (texture_browser *tb, texture_browser_create_info create_info) {
	tb->ui = create_info.ui;
	tb->editor_root = create_info.editor_root;
	tb->game_package_name = create_info.game_package_name;
	tb->search_package_name = tb->game_package_name;
	tb->font_name = create_info.font_name;
	tb->font_size = create_info.font_size;
	tb->textbox_font_name = create_info.textbox_font_name;
	tb->textbox_font_size = create_info.textbox_font_size;

	// Texture listing
	tb->imagebox_size = 128;
	tb->imagebox_padding = 5.0f;

	// Texture browser background/window
	tb->window_size = vec2_create(1500, 900);
	tb->right_col_x = 1100.0f;
	tb->bg_panel = kui_panel_control_create(tb->ui, "tex_browser_bg_panel", tb->window_size, (vec4){0, 0, 0, 0.8f});
	KASSERT(kui_system_control_add_child(tb->ui, create_info.editor_root, tb->bg_panel));
	kui_control_position_set(tb->ui, tb->bg_panel, (vec3){300, 50, 0});
	kui_control_set_is_active(tb->ui, tb->bg_panel, false);
	kui_control_set_is_visible(tb->ui, tb->bg_panel, false);

	// Window Label
	tb->title = kui_label_control_create(tb->ui, "tex_browser_title", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "Browse Textures");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->title));
	kui_control_position_set(tb->ui, tb->title, (vec3){10, -5.0f, 0});

	// Search Label
	tb->search_label = kui_label_control_create(tb->ui, "tex_browser_search_label", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "Search");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->search_label));
	kui_control_position_set(tb->ui, tb->search_label, (vec3){10, 45.0f, 0});

	// Search textbox.
	tb->search_textbox = kui_textbox_control_create(tb->ui, "tex_browser_search_textbox", FONT_TYPE_SYSTEM, tb->textbox_font_name, tb->textbox_font_size, "", KUI_TEXTBOX_TYPE_STRING);
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->search_textbox));
	kui_control_position_set(tb->ui, tb->search_textbox, (vec3){100, 50, 0});
	KASSERT(kui_textbox_control_width_set(tb->ui, tb->search_textbox, 800.0f));
	kui_control_set_user_data(tb->ui, tb->search_textbox, sizeof(*tb), tb, false, MEMORY_TAG_EDITOR);
	kui_control_set_on_key(tb->ui, tb->search_textbox, texture_browser_search_textbox_on_key);

	tb->search_game_pack_only_checkbox = kui_checkbox_control_create(tb->ui, "tex_browser_search_game_pack_only_checkbox", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "Game Pak. Only");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->search_game_pack_only_checkbox));
	kui_control_position_set(tb->ui, tb->search_game_pack_only_checkbox, (vec3){910, 50, 0});
	kui_control_set_user_data(tb->ui, tb->search_game_pack_only_checkbox, sizeof(*tb), tb, false, MEMORY_TAG_EDITOR);
	kui_checkbox_set_checked(tb->ui, tb->search_game_pack_only_checkbox, true);
	kui_checkbox_set_on_checked(tb->ui, tb->search_game_pack_only_checkbox, texture_browser_search_game_pak_checkbox_check_changed);

	// Scrollable content control
	f32 inspector_width = tb->window_size.x - tb->right_col_x;
	f32 scrollable_width = tb->window_size.x - inspector_width;
	tb->scrollable_control = kui_scrollable_control_create(tb->ui, "tex_browser_scrollable_control", (vec2){scrollable_width, 100}, false, true);
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->scrollable_control));
	kui_control_position_set(tb->ui, tb->scrollable_control, (vec3){10, 100, 0});
	kui_scrollable_control_resize(tb->ui, tb->scrollable_control, (vec2){scrollable_width, tb->window_size.y - 150});

	tb->content_container = kui_scrollable_control_get_content_container(tb->ui, tb->scrollable_control);

	tb->tex_tile_size = vec2_create(
		tb->imagebox_size + tb->imagebox_padding,
		tb->imagebox_size + tb->imagebox_padding + tb->font_size + tb->imagebox_padding);

	// Preview imagebox
	tb->inspector_preview_imagebox = kui_image_box_control_create(tb->ui, "tex_inspector_preview_imagebox", (vec2i){380, 380});
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->inspector_preview_imagebox));
	kui_control_position_set(tb->ui, tb->inspector_preview_imagebox, (vec3){tb->right_col_x + 10, 100, 0});
	kui_image_box_control_texture_set_by_name(tb->ui, tb->inspector_preview_imagebox, kname_create(DEFAULT_TEXTURE_NAME), INVALID_KNAME);

	const char *tex_data_str = string_format(
		"%k\n%s\n%ux%u\n%s",
		kname_create("No selection"),
		"texture type", // TODO:
		0,
		0,
		"format:");

	// texture browser tex data
	tb->inspector_label = kui_label_control_create(tb->ui, "tex_inspector_label", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, tex_data_str);
	string_free(tex_data_str);

	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->inspector_label));
	kui_control_position_set(tb->ui, tb->inspector_label, (vec3){tb->right_col_x + 10, 500, 0});

	// Confirm button.
	tb->confirm_btn = kui_button_control_create_with_text(tb->ui, "tex_browser_confirm_btn", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "OK");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->confirm_btn));
	kui_control_set_user_data(tb->ui, tb->confirm_btn, sizeof(*tb), tb, false, MEMORY_TAG_EDITOR);
	kui_button_control_width_set(tb->ui, tb->confirm_btn, 190);
	kui_control_position_set(tb->ui, tb->confirm_btn, (vec3){tb->right_col_x + 5, tb->window_size.y - 45, 0});
	kui_control_set_on_click(tb->ui, tb->confirm_btn, texture_browser_confirm_button_clicked);

	// Cancel button.
	tb->cancel_btn = kui_button_control_create_with_text(tb->ui, "tex_browser_cancel_btn", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "Cancel");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->cancel_btn));
	kui_control_set_user_data(tb->ui, tb->cancel_btn, sizeof(*tb), tb, false, MEMORY_TAG_EDITOR);
	kui_button_control_width_set(tb->ui, tb->cancel_btn, 190);
	kui_control_position_set(tb->ui, tb->cancel_btn, (vec3){tb->right_col_x + 5 + 190 + 5, tb->window_size.y - 45, 0});
	kui_control_set_on_click(tb->ui, tb->cancel_btn, texture_browser_cancel_button_clicked);

	// Import button.
	tb->import_btn = kui_button_control_create_with_text(tb->ui, "import_btn", FONT_TYPE_SYSTEM, tb->font_name, tb->font_size, "Import");
	KASSERT(kui_system_control_add_child(tb->ui, tb->bg_panel, tb->import_btn));
	kui_control_set_user_data(tb->ui, tb->import_btn, sizeof(*tb), tb, false, MEMORY_TAG_EDITOR);
	kui_button_control_width_set(tb->ui, tb->import_btn, 190);
	kui_control_position_set(tb->ui, tb->import_btn, (vec3){5, tb->window_size.y - 45, 0});
	kui_control_set_on_click(tb->ui, tb->import_btn, texture_browser_import_button_clicked);
}

void texture_browser_destroy (texture_browser *tb) {
	if (tb->image_boxes) {
		KFREE_TYPE_CARRAY(tb->image_boxes, kui_control, tb->tex_count);
	}
	if (tb->labels) {
		KFREE_TYPE_CARRAY(tb->labels, kui_control, tb->tex_count);
	}
}

b8 texture_browser_is_open (const texture_browser *tb) {
	return FLAG_GET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT);
}

void texture_browser_open_for_browsing (texture_browser *tb) {
	if (FLAG_GET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT)) {
		KWARN("The texture browser is already open");
		return;
	}

	tb->flags = TEXTURE_BROWSER_FLAG_NONE;
	FLAG_SET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT, true);

	texture_browser_open_internal(tb);
}

void texture_browser_open_for_selection (texture_browser *tb, void *context, PFN_tex_browser_selected_callback selected_callback, PFN_tex_browser_cancelled_callback cancelled_callback) {
	if (FLAG_GET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT)) {
		KWARN("The texture browser is already open");
		return;
	}

	tb->flags = TEXTURE_BROWSER_FLAG_NONE;
	FLAG_SET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT, true);
	FLAG_SET(tb->flags, TEXTURE_BROWSER_FLAG_SELECTING_BIT, true);

	tb->selection_context = context;
	tb->selected_callback = selected_callback;
	tb->cancelled_callback = cancelled_callback;

	texture_browser_open_internal(tb);
}
void texture_browser_close (texture_browser *tb) {
	// Activate/set visible the tex browser bg panel
	kui_control_set_is_active(tb->ui, tb->bg_panel, false);
	kui_control_set_is_visible(tb->ui, tb->bg_panel, false);
	FLAG_SET(tb->flags, TEXTURE_BROWSER_FLAG_OPEN_BIT, false);

	tb->selection_context = KNULL;
	tb->selected_callback = KNULL;
	tb->cancelled_callback = KNULL;
}

void texture_browser_refresh (texture_browser *tb) {
	f32 inspector_width = tb->window_size.x - tb->right_col_x;
	f32 scrollable_width = tb->window_size.x - inspector_width - 5.0f;

	// Clear the old controls first.
	if (tb->tex_count) {
		kui_control_destroy_all_children(tb->ui, tb->content_container);
		/* for (u32 i = 0; i < tb->tex_browser_tex_count; ++i) {
			kui_image_box_control_destroy(tb->ui, &tb->tex_browser_image_boxes[i]);
			kui_label_control_destroy(tb->ui, &tb->tex_browser_labels[i]);
		} */
		KFREE_TYPE_CARRAY(tb->image_boxes, kui_control, tb->tex_count);
		tb->image_boxes = KNULL;
		KFREE_TYPE_CARRAY(tb->labels, kui_control, tb->tex_count);
		tb->labels = KNULL;
	}

	// Query for a list of textures.
	kname *texture_names = asset_system_names_by_type(engine_systems_get()->asset_state, KASSET_TYPE_IMAGE, tb->search_package_name, &tb->tex_count);

	if (texture_names && tb->search_text && string_length(tb->search_text)) {
		kname *search_names = darray_create(kname);
		for (u32 i = 0; i < tb->tex_count; ++i) {
			if (string_index_of_stri(kname_string_get(texture_names[i]), tb->search_text) != -1) {
				darray_push(search_names, texture_names[i]);
			}
		}
		KFREE_TYPE_CARRAY(texture_names, kname, tb->tex_count);
		texture_names = KNULL;
		tb->tex_count = darray_length(search_names);
		if (tb->tex_count) {
			KDUPLICATE_TYPE_CARRAY(texture_names, search_names, kname, tb->tex_count);
			darray_destroy(search_names);
		}
	}

	u32 x = 0;
	u32 y = 0;

	if (tb->tex_count) {
		tb->image_boxes = KALLOC_TYPE_CARRAY(kui_control, tb->tex_count);
		tb->labels = KALLOC_TYPE_CARRAY(kui_control, tb->tex_count);

		texture_browser_element_data *first_element_data = KNULL;
		for (u32 i = 0; i < tb->tex_count; ++i) {

			texture_browser_element_data *element_data = KALLOC_TYPE(texture_browser_element_data, MEMORY_TAG_EDITOR);
			element_data->tb = tb;
			element_data->texture_name = texture_names[i];
			if (i == 0) {
				first_element_data = element_data;
			}

			// Image box
			{
				char *name = string_format("__texture_browser_image_box_%u__", i);
				tb->image_boxes[i] = kui_image_box_control_create(tb->ui, name, (vec2i){tb->imagebox_size, tb->imagebox_size});
				string_free(name);
				KASSERT(kui_system_control_add_child(tb->ui, tb->content_container, tb->image_boxes[i]));
				if (!kui_image_box_control_texture_set_by_name(tb->ui, tb->image_boxes[i], texture_names[i], INVALID_KNAME)) {
					KERROR("Image not loaded, ya dingus!");
				}

				element_data->texture = texture_get_by_name(texture_names[i]);
				if (!texture_properties_get(element_data->texture, &element_data->properties)) {
					KERROR("Unable to get properties for texture '%k'", texture_names[i]);
					KFREE_TYPE(element_data, texture_browser_element_data, MEMORY_TAG_EDITOR);
					continue;
				}
				kui_control_set_user_data(tb->ui, tb->image_boxes[i], sizeof(texture_browser_element_data), element_data, true, MEMORY_TAG_EDITOR);
				// onclick select and show metadata
				kui_control_set_on_click(tb->ui, tb->image_boxes[i], texture_browser_imagebox_clicked);

				// TODO: doubleclick selects and returns if in "selection mode"
			}
			// Label
			{
				char *name = string_format("__texture_browser_image_label_%u__", i);
				tb->labels[i] = kui_label_control_create_ex(
					tb->ui,
					name,
					FONT_TYPE_SYSTEM,
					tb->font_name,
					tb->font_size,
					kname_string_get(texture_names[i]),
					tb->imagebox_size,
					KUI_LABEL_FLAG_TRUNCATE_ELLIPSIS_BIT);
				string_free(name);
				KASSERT(kui_system_control_add_child(tb->ui, tb->content_container, tb->labels[i]));
				kui_control_set_user_data(tb->ui, tb->labels[i], sizeof(texture_browser_element_data), element_data, false, MEMORY_TAG_EDITOR);
			}

			// positioning
			b8 has_space = (tb->tex_tile_size.x * (x + 1)) <= scrollable_width;
			if (!has_space) {
				x = 0;
				y++;
			}

			vec3 image_pos = (vec3){tb->tex_tile_size.x * x, tb->tex_tile_size.y * y, 0};
			vec3 label_pos = vec3_add(image_pos, vec3_create(0, tb->imagebox_size, 0));
			kui_control_position_set(tb->ui, tb->image_boxes[i], image_pos);
			kui_control_position_set(tb->ui, tb->labels[i], label_pos);

			++x;
		}

		// Selection frame.
		tb->selected_frame = kui_frame_control_create(tb->ui, "tex_browser_selected_frame");
		KASSERT(kui_system_control_add_child(tb->ui, tb->content_container, tb->selected_frame));
		kui_frame_control_size_set(tb->ui, tb->selected_frame, tb->imagebox_size, tb->imagebox_size);
		// HACK: hardcoded pos to first selection - need to calculate based on selected index.
		kui_control_position_set(tb->ui, tb->selected_frame, (vec3){0, 0, 0});

		tb->selected_texture = first_element_data->texture;
		kui_image_box_control_texture_set_by_name(tb->ui, tb->inspector_preview_imagebox, first_element_data->texture_name, INVALID_KNAME);

		const char *tex_data_str = string_format(
			"%k\n%s\n%ux%u\nformat:%s",
			first_element_data->texture_name,
			"texture type", // TODO:
			first_element_data->properties.width,
			first_element_data->properties.height,
			string_from_kpixel_format(first_element_data->properties.format));

		// texture browser tex data
		kui_label_text_set(tb->ui, tb->inspector_label, tex_data_str);
		string_free(tex_data_str);
	} else {

		// No textures available, thus no selection can be made.
		tb->selected_texture = INVALID_KTEXTURE;
		kui_image_box_control_texture_set_by_name(tb->ui, tb->inspector_preview_imagebox, kname_create(DEFAULT_TEXTURE_NAME), INVALID_KNAME);

		const char *tex_data_str = string_format(
			"%k\n%s\n%ux%u\n%s",
			kname_create("No selection"),
			"texture type", // TODO:
			0,
			0,
			"format:");

		// texture browser tex data
		kui_label_text_set(tb->ui, tb->inspector_label, tex_data_str);
		string_free(tex_data_str);
	}

	f32 scrollable_height = tb->tex_tile_size.y * (y + 1);

	kui_scrollable_set_content_size(tb->ui, tb->scrollable_control, scrollable_width, scrollable_height);
	// Scroll to the top left
	/* kui_scrollable_control_scroll_y_set(tb->ui, tb->tex_browser_scrollable_control, 0); */
	/* kui_scrollable_control_scroll_x_set(tb->ui, tb->tex_browser_scrollable_control, 0); */

	if (texture_names) {
		KFREE_TYPE_CARRAY(texture_names, kname, tb->tex_count);
	}
}

static void texture_browser_open_internal (texture_browser *tb) {
	// Activate/set visible the tex browser bg panel
	kui_control_set_is_active(tb->ui, tb->bg_panel, true);
	kui_control_set_is_visible(tb->ui, tb->bg_panel, true);

	texture_browser_refresh(tb);

	if (FLAG_GET(tb->flags, TEXTURE_BROWSER_FLAG_SELECTING_BIT)) {
		kui_label_text_set(tb->ui, tb->title, "Select a Texture");
		// TODO: show additional selection controls (i.e. ok/cancel buttons, maybe a label at the top of the window?)

	} else {
		kui_label_text_set(tb->ui, tb->title, "Browse Textures");
		// TODO: ensure selection-only controls are not shown.
	}
}

static b8 texture_browser_confirm_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	texture_browser *tb = base->user_data;

	if (FLAG_GET(tb->flags, TEXTURE_BROWSER_FLAG_SELECTING_BIT)) {
		if (tb->selected_texture == INVALID_KTEXTURE) {
			// TODO: Either some sort of error dialog OR to disable the select button until a selection is made.
			KERROR("No texture is selected, cannot proceed.");
			return false;
		}
		// Issue the callback along with the selected texture.
		if (tb->selected_callback) {
			tb->selected_callback(tb->selected_texture, tb->selection_context);
		}
	}

	// Just close the window.
	texture_browser_close(tb);
	return false;
}

static b8 texture_browser_cancel_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	texture_browser *tb = base->user_data;

	if (tb->cancelled_callback) {
		tb->cancelled_callback(tb->selection_context);
	}

	// Just close the window.
	texture_browser_close(tb);
	return false;
}

static b8 texture_browser_import_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
// TODO: Should saving be allowed here? In general the editor wouldn't be available in non-debug builds, but...
#if KOHI_DEBUG
	kui_base_control *base = kui_system_get_base(state, self);
	texture_browser *tb = base->user_data;

	// Open file dialog
	platform_open_file_dialog_options options = {
		.allow_multiselect = false, // TODO: import multiple textures at once.
		.title = "Select Image Source Assets",
		.filter = "Image Files (*.bmp;*.jpg;*.gif;*.png)|*.bmp;*.jpg;*.gif;*.png|All files (*.*)|*.*",
	};
	platform_open_file_dialog_result result = platform_open_file_dialog_open(options);
	if (result.success) {
		u8 success_count = 0;
		kpackage *package = vfs_package_get(engine_systems_get()->vfs_system_state, tb->game_package_name);
		const char *package_dir = string_directory_from_path(package->manifest_file_path);
		KTRACE("Open file dialog success: %s", bool_to_string(result.success));
		for (u8 i = 0; i < result.file_count; ++i) {
			KTRACE("File selected (%u): '%s'", i, result.file_paths[i]);

			// Copy the file from wherever it is to the current game package assets/images/source dir.
			const char *asset_file_name = string_filename_from_path(result.file_paths[i]);
			char *source_path = string_format("%s/assets/images/source/%s", package_dir, asset_file_name);
			char *local_source_path = string_format("./assets/images/source/%s", asset_file_name);
			if (platform_copy_file(result.file_paths[i], source_path, true) != PLATFORM_ERROR_SUCCESS) {
				KERROR("Failed to copy file. See logs for details.");
			} else {

				// Run the import process on that image to go from image->kbi.
				const char *asset_name = string_filename_no_extension_from_path(result.file_paths[i]);
				char *target_path = string_format("%s/assets/images/%s.kbi", package_dir, asset_name);
				char *local_target_path = string_format("./assets/images/%s.kbi", asset_name);
				if (!kasset_image_import(source_path, target_path, true, KPIXEL_FORMAT_RGBA8)) {
					KERROR("Failed to import image asset. See logs for details.");
				} else {
					// Add asset to manifest.
					asset_manifest_asset new_asset = {
						.type = KASSET_TYPE_IMAGE,
						.path = target_path,
						.local_path = local_target_path,
						.source_path = source_path,
						.local_source_path = local_source_path,
						.name = kname_create(asset_name)};
					if (!kpackage_add_asset(package, &new_asset)) {
						KERROR("Failed to add asset '%k' to package '%k'. See logs for details.", new_asset.name, tb->game_package_name);
					} else {
						success_count++;
					}
				}

				string_free(asset_name);
				string_free(target_path);
				string_free(local_target_path);
			}
			string_free(asset_file_name);
			string_free(source_path);
			string_free(local_source_path);
		}
		string_free(package_dir);

		if (success_count > 0) {
			// Save manifest.
			if (!kpackage_save(package)) {
				KERROR("Failed to save package '%k'. See logs for details.", tb->game_package_name);
			} else {
				// Refresh texture browser.
				texture_browser_refresh(tb);
			}
		}
	}

	// cleanup result
	if (result.file_count) {
		for (u8 i = 0; i < result.file_count; ++i) {
			string_free(result.file_paths[i]);
		}
		KFREE_TYPE_CARRAY(result.file_paths, const char *, result.file_count);
	}

	texture_browser_refresh(tb);

#endif
	return false;
}

static void texture_browser_search_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		texture_browser *tb = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			// Clear current search text.
			string_free(tb->search_text);
			tb->search_text = KNULL;

			// Only search on enter press.
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				KTRACE("Searching for textures based on '%s'...", entry_control_text);
				tb->search_text = string_duplicate(entry_control_text);
			}
			texture_browser_refresh(tb);
		}
	}
}

static void texture_browser_search_game_pak_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	texture_browser *tb = kui_control_get_user_data(state, self);

	tb->search_package_name = event.checked ? tb->game_package_name : INVALID_KNAME;
	texture_browser_refresh(tb);
}

static b8 texture_browser_imagebox_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	texture_browser_element_data *element_data = kui_control_get_user_data(state, self);

	kui_base_control *base = kui_system_get_base(state, self);
	vec3 pos = ktransform_position_get(base->ktransform);

	kui_base_control *selection_frame = kui_system_get_base(state, element_data->tb->selected_frame);
	ktransform_position_set(selection_frame->ktransform, pos);

	element_data->tb->selected_texture = element_data->texture;

	KTRACE("Texture selected '%k' (width=%u, height=%u).", element_data->texture_name, element_data->properties.width, element_data->properties.height);

	kui_image_box_control_texture_set_by_name(state, element_data->tb->inspector_preview_imagebox, element_data->texture_name, INVALID_KNAME);

	const char *tex_data_str = string_format(
		"%k\n%s\n%ux%u\nformat:%s",
		element_data->texture_name,
		"texture type", // TODO:
		element_data->properties.width,
		element_data->properties.height,
		string_from_kpixel_format(element_data->properties.format));
	kui_label_text_set(state, element_data->tb->inspector_label, tex_data_str);
	string_free(tex_data_str);

	return false;
}
