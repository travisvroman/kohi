#include "editor.h"

// Core
#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <input_types.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/geometry_2d.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <strings/kstring_id.h>
#include <utils/ksort.h>

// Runtime
#include <audio/audio_frontend.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/keymap.h>
#include <plugins/plugin_types.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <runtime_defines.h>
#include <systems/asset_system.h>
#include <systems/font_system.h>
#include <systems/kcamera_system.h>
#include <systems/kmatrix_system.h>
#include <systems/kshader_system.h>
#include <systems/ktimeline_system.h>
#include <systems/plugin_system.h>
#include <systems/texture_system.h>
#include <world/heightfield_terrain.h>
#include <world/kscene.h>
#include <world/world_types.h>
#include <world/world_utils.h>

// KUI plugin
#include <controls/checkbox_control.h>
#include <controls/image_box_control.h>
#include <controls/kui_button.h>
#include <controls/kui_label.h>
#include <controls/kui_panel.h>
#include <controls/kui_scrollable.h>
#include <controls/kui_textbox.h>
#include <controls/kui_tree_item.h>
#include <kui_plugin_main.h>
#include <kui_system.h>
#include <kui_types.h>

// Utils plugin
#include "editor/editor_gizmo.h"
#include "editor/texture_browser.h"
#include "utils_plugin_defines.h"

typedef struct editor_gizmo_immediate_data {
	// Offsets into the matrix SSBO per type.
	u32 mat_ssbo_view_offset;
	u32 mat_ssbo_proj_offset;
	u32 mat_ssbo_tran_offset;
	u32 mat_ssbo_genc_offset;
	// Indices into the matrix SSBO, offset by above types.
	u32 view_index;
	u32 projection_index;
	u32 model_index;
	u32 padding;
} editor_gizmo_immediate_data;

typedef struct hf_terrain_material_imagebox_context {
	editor_state *editor;
	u8 material_index;
	hf_terrain_material_map map;
} hf_terrain_material_imagebox_context;

typedef struct hf_terrain_chunk_material_context {
	editor_state *editor;
	u8 material_slot;
} hf_terrain_chunk_material_context;

static f32 get_engine_delta_time (void);
static f32 get_engine_total_time (void);
static b8 editor_has_focused_control (editor_state *editor);

// General editor movement/interaction
static void save_scene (const struct kscene *scene, kname package_name, kname asset_name);
static void zoom_extents (struct editor_state *state);
static b8 editor_on_mouse_move (u16 code, void *sender, void *listener_inst, event_context context);
static b8 editor_on_drag (u16 code, void *sender, void *listener_inst, event_context context);
static b8 editor_on_button (u16 code, void *sender, void *listener_inst, event_context context);

static void editor_command_execute (console_command_context context);

static void editor_register_events (struct editor_state *state);
static void editor_unregister_events (struct editor_state *state);
static void editor_register_commands (struct editor_state *state);
static void editor_unregister_commands (struct editor_state *state);

// "Main menu" and/or mode buttons.
static b8 save_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 mode_scene_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 mode_entity_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 mode_tree_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 mode_hf_terrain_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 texture_browser_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);

// Editor options
static void show_bvh_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static void show_grid_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static void show_debug_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static void scene_visibility_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);

// Scene editor controls
static void scene_name_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void scene_fog_colour_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

// Entity editor controls
static void entity_name_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_position_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_position_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_position_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_w_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_scale_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_scale_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void entity_scale_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

// Tree view controls
static void tree_clear (editor_state *state);
static void tree_refresh (editor_state *state);
static b8 tree_item_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 tree_item_expanded (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 tree_item_collapsed (struct kui_state *state, kui_control self, struct kui_mouse_event event);

// HF Terrain editor controls
static b8 hft_save_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 hft_material_imagebox_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static void hft_paint_brush_diameter_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void hft_paint_brush_strength_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void hft_paint_material_index_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

static void hft_elevation_diameter_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);
static void hft_elevation_amount_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

static void hf_terrain_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static void hf_terrain_erase_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);
static void hf_terrain_set_height_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event);

static void hft_chunk_material_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

b8 editor_initialize (u64 *memory_requirement, struct editor_state *state, kname game_package_name) {
	*memory_requirement = sizeof(editor_state);
	if (!state) {
		return true;
	}

	state->game_package_name = game_package_name;

	state->matrix_system = engine_systems_get()->matrix_system;
	state->default_identity_matrix_id = kmatrix_system_add(state->matrix_system, KMATRIX_TYPE_TRANSFORM, mat4_identity());

	// Setup gizmo.
	if (!editor_gizmo_create(&state->gizmo)) {
		KERROR("Failed to create editor gizmo!");
		return false;
	}
	if (!editor_gizmo_initialize(&state->gizmo)) {
		KERROR("Failed to initialize editor gizmo!");
		return false;
	}
	if (!editor_gizmo_load(&state->gizmo)) {
		KERROR("Failed to load editor gizmo!");
		return false;
	}

	state->renderer = engine_systems_get()->renderer_system;

	state->standard_vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	state->index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// Editor gizmo pass state
	{
		state->editor_gizmo_pass.gizmo_shader = kshader_system_get(kname_create(SHADER_NAME_PLUGIN_UTILS_EDITOR_GIZMO), kname_create(PACKAGE_NAME_PLUGIN_UTILS));
		KASSERT_DEBUG(state->editor_gizmo_pass.gizmo_shader != KSHADER_INVALID);

		state->editor_gizmo_pass.set0_instance_id = kshader_acquire_binding_set_instance(state->editor_gizmo_pass.gizmo_shader, 0);
	}

	state->colour_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_COLOUR_3D), kname_create(PACKAGE_NAME_RUNTIME));
	KASSERT_DEBUG(state->colour_shader != KSHADER_INVALID);

	state->debug_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_DEBUG_3D), kname_create(PACKAGE_NAME_RUNTIME));
	KASSERT_DEBUG(state->debug_shader != KSHADER_INVALID);

	// Allocate some space in the vertex buffer for the debug points to be rendered.
	state->debug_points_vertex_buffer_offset = 0;
	renderer_renderbuffer_allocate(state->renderer, state->standard_vertex_buffer, sizeof(colour_vertex_3d) * 256, &state->debug_points_vertex_buffer_offset);

	// HF Terrain editor: selected chunk debug shape.
	f32 chunk_dim = HF_CHUNK_QUAD_COUNT * HF_QUAD_SCALE;
	state->hft_selected_chunk_debug_box = geometry_generate_line_box3d_typed(vec3_create(chunk_dim, 1.0f, chunk_dim), INVALID_KNAME, KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY, vec3_zero());

	// Editor camera. Use the same view properties of the world camera, but different starting position/rotation.
	vec3 editor_cam_pos = (vec3){-2.571f, 4.75f, 0.929f};
	vec3 editor_cam_rot_euler_radians = (vec3){deg_to_rad(-29.0f), deg_to_rad(253.0f), deg_to_rad(0.0f)};
	rect_2di world_vp_rect = {0, 0, 1280 - 40, 720 - 40};
	state->editor_camera = kcamera_create(KCAMERA_TYPE_3D, world_vp_rect, editor_cam_pos, editor_cam_rot_euler_radians, deg_to_rad(45.0f), 0.1f, 1000.0f);

	state->editor_camera_forward_move_speed = 5.0f * 5.0f;
	state->editor_camera_backward_move_speed = 2.5f * 5.0f;

	state->selection_list = darray_create(kentity);

	kruntime_plugin *kui_plugin = plugin_system_get(engine_systems_get()->plugin_system, "kohi.plugin.ui.kui");
	kui_state *kui_state = ((kui_plugin_state *)kui_plugin->plugin_state)->state;
	state->kui_state = kui_state;

	// UI elements. Create/load them all up here.
	state->font_name = kname_create("Noto Sans CJK JP, Bold");
	state->font_size = 32;
	state->textbox_font_name = kname_create("Noto Sans Mono CJK JP");
	state->textbox_font_size = 30;

	// Main root control for everything else to belong to.
	{
		state->editor_root = kui_base_control_create(kui_state, "editor_root", KUI_CONTROL_TYPE_BASE);
		KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, state->editor_root));

		kui_control_set_is_visible(kui_state, state->editor_root, false);
	}

	// Main window
	{
		// Main background panel.
		state->main_bg_panel = kui_panel_control_create(kui_state, "main_bg_panel", (vec2){250.0f, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->main_bg_panel));
		kui_control_position_set(kui_state, state->main_bg_panel, (vec3){10, 10, 0});

		// Save button.
		{
			state->save_button = kui_button_control_create_with_text(kui_state, "save_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Save");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->save_button));
			kui_button_control_width_set(kui_state, state->save_button, 240);
			kui_control_position_set(kui_state, state->save_button, (vec3){5, 50, 0});
			kui_control_set_on_click(kui_state, state->save_button, save_button_clicked);
		}

		// Scene mode button.
		{
			state->mode_scene_button = kui_button_control_create_with_text(kui_state, "mode_scene_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_scene_button));
			kui_button_control_width_set(kui_state, state->mode_scene_button, 115);
			kui_control_position_set(kui_state, state->mode_scene_button, (vec3){5, 100, 0});
			kui_control_set_user_data(kui_state, state->mode_scene_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_scene_button, mode_scene_button_clicked);
		}

		// Entity mode button.
		{
			state->mode_entity_button = kui_button_control_create_with_text(kui_state, "mode_entity_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_entity_button));
			kui_button_control_width_set(kui_state, state->mode_entity_button, 115);
			kui_control_position_set(kui_state, state->mode_entity_button, (vec3){125, 100, 0});
			kui_control_set_user_data(kui_state, state->mode_entity_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_entity_button, mode_entity_button_clicked);
		}

		// Tree mode button.
		{
			state->mode_tree_button = kui_button_control_create_with_text(kui_state, "mode_tree_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_tree_button));
			kui_button_control_width_set(kui_state, state->mode_tree_button, 115);
			kui_control_position_set(kui_state, state->mode_tree_button, (vec3){5, 150, 0});
			kui_control_set_user_data(kui_state, state->mode_tree_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_tree_button, mode_tree_button_clicked);
		}

		// HF Terrain mode button.
		{
			state->mode_hf_terrain_button = kui_button_control_create_with_text(kui_state, "mode_hf_terrain_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "HF Terr.");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_hf_terrain_button));
			kui_button_control_width_set(kui_state, state->mode_hf_terrain_button, 115);
			kui_control_position_set(kui_state, state->mode_hf_terrain_button, (vec3){125, 150, 0});
			kui_control_set_user_data(kui_state, state->mode_hf_terrain_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_hf_terrain_button, mode_hf_terrain_button_clicked);
		}

		// Texture browser button.
		{
			state->texture_browser_button = kui_button_control_create_with_text(kui_state, "texture_browser_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tex B");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->texture_browser_button));
			kui_button_control_width_set(kui_state, state->texture_browser_button, 115);
			kui_control_position_set(kui_state, state->texture_browser_button, (vec3){5, 200, 0});
			kui_control_set_user_data(kui_state, state->texture_browser_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->texture_browser_button, texture_browser_button_clicked);
		}

		// Toggle options label
		state->view_label = kui_label_control_create(kui_state, "view_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Visibility:");
		KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_label));
		kui_control_position_set(kui_state, state->view_label, (vec3){5, 270.0f, 0});

		// Debug toggles
		{
			// Toggle visiblity of general debug data. If this is disabled, so are the other debug options.
			{
				state->view_debug_checkbox = kui_checkbox_control_create(kui_state, "view_debug_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Debug");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_debug_checkbox));
				kui_control_position_set(kui_state, state->view_debug_checkbox, (vec3){5, 300, 0});
				kui_control_set_user_data(kui_state, state->view_debug_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_debug_checkbox, show_debug_checkbox_check_changed);
			}

			// Toggle visiblity of BVH debug data.
			{
				state->view_bvh_checkbox = kui_checkbox_control_create(kui_state, "view_bvh_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "BVH");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_bvh_checkbox));
				kui_control_position_set(kui_state, state->view_bvh_checkbox, (vec3){35, 330, 0});
				kui_control_set_user_data(kui_state, state->view_bvh_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_bvh_checkbox, show_bvh_checkbox_check_changed);
			}

			// Toggle visiblity of debug grid.
			{
				state->view_grid_checkbox = kui_checkbox_control_create(kui_state, "view_grid_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Grid");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_grid_checkbox));
				kui_control_position_set(kui_state, state->view_grid_checkbox, (vec3){35, 360, 0});
				kui_control_set_user_data(kui_state, state->view_grid_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_grid_checkbox, show_grid_checkbox_check_changed);
			}
		}

		// General scene visiblity
		{
			// Toggle visiblity of the skybox.
			{
				state->view_skybox_checkbox = kui_checkbox_control_create(kui_state, "view_skybox_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Skybox");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_skybox_checkbox));
				kui_control_position_set(kui_state, state->view_skybox_checkbox, (vec3){5, 390, 0});
				kui_control_set_user_data(kui_state, state->view_skybox_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_skybox_checkbox, scene_visibility_checkbox_check_changed);
			}

			// Toggle visiblity of fog.
			{
				state->view_fog_checkbox = kui_checkbox_control_create(kui_state, "view_fog_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Fog");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_fog_checkbox));
				kui_control_position_set(kui_state, state->view_fog_checkbox, (vec3){5, 420, 0});
				kui_control_set_user_data(kui_state, state->view_fog_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_fog_checkbox, scene_visibility_checkbox_check_changed);
			}
		}
	}

	// Scene inspector window panel.
	{
		state->scene_inspector_width = 650.0f;
		state->scene_inspector_right_col_x = 130.0f;
		state->scene_inspector_bg_panel = kui_panel_control_create(kui_state, "scene_inspector_bg_panel", (vec2){state->scene_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->scene_inspector_bg_panel));
		kui_control_position_set(kui_state, state->scene_inspector_bg_panel, (vec3){1280 - (state->scene_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->scene_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->scene_inspector_bg_panel, false);

		// Window Label
		state->scene_inspector_title = kui_label_control_create(kui_state, "scene_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene");
		KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_inspector_title));
		kui_control_position_set(kui_state, state->scene_inspector_title, (vec3){10, -5.0f, 0});

		// scene name
		{
			// Name label.
			state->scene_name_label = kui_label_control_create(kui_state, "scene_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name");
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_name_label));
			kui_control_position_set(kui_state, state->scene_name_label, (vec3){10, 50 + -5.0f, 0});

			// Name textbox.
			state->scene_name_textbox = kui_textbox_control_create(kui_state, "scene_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_STRING);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_name_textbox));
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_name_textbox, 380));
			kui_control_position_set(kui_state, state->scene_name_textbox, (vec3){state->scene_inspector_right_col_x, 50, 0});
			kui_control_set_user_data(kui_state, state->scene_name_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_name_textbox, scene_name_textbox_on_key);
		}

		// Fog colour
		{
			// Fog colour label
			state->scene_fog_colour_label = kui_label_control_create(kui_state, "scene_fog_colour_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Fog colour");
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_label));
			kui_control_position_set(kui_state, state->scene_fog_colour_label, (vec3){10, 100 + -5.0f, 0});

			// Fog colour R textbox.
			state->scene_fog_colour_r_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_r_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_r_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_r_textbox, (vec3){state->scene_inspector_right_col_x, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_r_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_r_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_r_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_r_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour g textbox.
			state->scene_fog_colour_g_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_g_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_g_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_g_textbox, (vec3){state->scene_inspector_right_col_x + 130, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_g_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_g_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_g_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_g_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour b textbox.
			state->scene_fog_colour_b_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_b_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_b_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_b_textbox, (vec3){state->scene_inspector_right_col_x + 260, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_b_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_b_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_b_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_b_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour a textbox.
			state->scene_fog_colour_a_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_a_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_a_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_a_textbox, (vec3){state->scene_inspector_right_col_x + 390, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_a_textbox, 120));
			/* kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_a_textbox, EDITOR_AXIS_COLOUR_A); */
			kui_control_set_user_data(kui_state, state->scene_fog_colour_a_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_a_textbox, scene_fog_colour_textbox_on_key);
		}

		// TODO: more controls
	}

	// Entity inspector window panel.
	{
		state->entity_inspector_width = 650.0f;
		state->entity_inspector_right_col_x = 130.0f;
		state->entity_inspector_bg_panel = kui_panel_control_create(kui_state, "entity_inspector_bg_panel", (vec2){state->entity_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->entity_inspector_bg_panel));
		kui_control_position_set(kui_state, state->entity_inspector_bg_panel, (vec3){1280 - (state->entity_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->entity_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->entity_inspector_bg_panel, false);

		// Window Label
		state->entity_inspector_title = kui_label_control_create(kui_state, "entity_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity (no selection)");
		KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_inspector_title));
		kui_control_position_set(kui_state, state->entity_inspector_title, (vec3){10, -5.0f, 0});

		// Entity name
		{
			// Name label.
			state->entity_name_label = kui_label_control_create(kui_state, "entity_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name:");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_name_label));
			kui_control_position_set(kui_state, state->entity_name_label, (vec3){10, 50 + -5.0f, 0});

			// Name textbox.
			state->entity_name_textbox = kui_textbox_control_create(kui_state, "entity_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_STRING);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_name_textbox));
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_name_textbox, 380));
			kui_control_position_set(kui_state, state->entity_name_textbox, (vec3){state->entity_inspector_right_col_x, 50, 0});
			kui_control_set_user_data(kui_state, state->entity_name_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_name_textbox, entity_name_textbox_on_key);
		}

		// Entity position
		{
			// Position label
			state->entity_position_label = kui_label_control_create(kui_state, "entity_position_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Position");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_label));
			kui_control_position_set(kui_state, state->entity_position_label, (vec3){10, 100 + -5.0f, 0});

			// Position x textbox.
			state->entity_position_x_textbox = kui_textbox_control_create(kui_state, "entity_position_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_x_textbox));
			kui_control_position_set(kui_state, state->entity_position_x_textbox, (vec3){state->entity_inspector_right_col_x, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_x_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_x_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->entity_position_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_x_textbox, entity_position_x_textbox_on_key);

			// Position y textbox.
			state->entity_position_y_textbox = kui_textbox_control_create(kui_state, "entity_position_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_y_textbox));
			kui_control_position_set(kui_state, state->entity_position_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_y_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_y_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->entity_position_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_y_textbox, entity_position_y_textbox_on_key);

			// Position z textbox.
			state->entity_position_z_textbox = kui_textbox_control_create(kui_state, "entity_position_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_z_textbox));
			kui_control_position_set(kui_state, state->entity_position_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_z_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_z_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->entity_position_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_z_textbox, entity_position_z_textbox_on_key);
		}

		// Entity rotation
		{
			// Position label
			state->entity_orientation_label = kui_label_control_create(kui_state, "entity_orientation_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Orientation");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_label));
			kui_control_position_set(kui_state, state->entity_orientation_label, (vec3){10, 150 + -5.0f, 0});

			// Orientatiohttps://music.youtube.com/playlist?list=OLAK5uy_lW21dMR_nuKQOOxBTKzKpvzJCjP3hqtzw&si=pgXjcRP9HzglQh4Cn x textbox.
			state->entity_orientation_x_textbox = kui_textbox_control_create(kui_state, "entity_orientation_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_x_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_x_textbox, (vec3){state->entity_inspector_right_col_x, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_x_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_x_textbox, entity_orientation_x_textbox_on_key);

			// Orientation y textbox.
			state->entity_orientation_y_textbox = kui_textbox_control_create(kui_state, "entity_orientation_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_y_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_y_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_y_textbox, entity_orientation_y_textbox_on_key);

			// Orientation z textbox.
			state->entity_orientation_z_textbox = kui_textbox_control_create(kui_state, "entity_orientation_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_z_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_z_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_z_textbox, entity_orientation_z_textbox_on_key);

			// Orientation z textbox.
			state->entity_orientation_w_textbox = kui_textbox_control_create(kui_state, "entity_orientation_w_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_w_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_w_textbox, (vec3){state->entity_inspector_right_col_x + 390, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_w_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_w_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_w_textbox, entity_orientation_w_textbox_on_key);
		}

		// Entity scale
		{
			// Scale label
			state->entity_scale_label = kui_label_control_create(kui_state, "entity_scale_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scale");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_label));
			kui_control_position_set(kui_state, state->entity_scale_label, (vec3){10, 200 + -5.0f, 0});

			// Scale x textbox.
			state->entity_scale_x_textbox = kui_textbox_control_create(kui_state, "entity_scale_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_x_textbox));
			kui_control_position_set(kui_state, state->entity_scale_x_textbox, (vec3){state->entity_inspector_right_col_x, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_x_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_x_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->entity_scale_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_x_textbox, entity_scale_x_textbox_on_key);

			// Scale y textbox.
			state->entity_scale_y_textbox = kui_textbox_control_create(kui_state, "entity_scale_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_y_textbox));
			kui_control_position_set(kui_state, state->entity_scale_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_y_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_y_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->entity_scale_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_y_textbox, entity_scale_y_textbox_on_key);

			// Scale z textbox.
			state->entity_scale_z_textbox = kui_textbox_control_create(kui_state, "entity_scale_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_z_textbox));
			kui_control_position_set(kui_state, state->entity_scale_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_z_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_z_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->entity_scale_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_z_textbox, entity_scale_z_textbox_on_key);
		}
	}

	// Tree window panel.
	{
		state->tree_inspector_width = 500.0f;
		state->tree_inspector_right_col_x = 150.0f;
		state->tree_inspector_bg_panel = kui_panel_control_create(kui_state, "tree_inspector_bg_panel", (vec2){state->tree_inspector_width, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->tree_inspector_bg_panel));
		kui_control_position_set(kui_state, state->tree_inspector_bg_panel, (vec3){1280 - (state->tree_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->tree_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->tree_inspector_bg_panel, false);

		// Window Label
		state->tree_inspector_title = kui_label_control_create(kui_state, "tree_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree");
		KASSERT(kui_system_control_add_child(kui_state, state->tree_inspector_bg_panel, state->tree_inspector_title));
		kui_control_position_set(kui_state, state->tree_inspector_title, (vec3){10, -5.0f, 0});

		// Base tree control.
		state->tree_scrollable_control = kui_scrollable_control_create(kui_state, "tree_base_control", (vec2){state->tree_inspector_width, 200}, true, true);
		KASSERT(kui_system_control_add_child(kui_state, state->tree_inspector_bg_panel, state->tree_scrollable_control));
		kui_control_position_set(kui_state, state->tree_scrollable_control, (vec3){0, 50, 0});

		state->tree_content_container = kui_scrollable_control_get_content_container(state->kui_state, state->tree_scrollable_control);

		// TODO: more controls
	}

	// HF Terrain window panel.
	{
		state->hf_terrain_window_width = 540.0f;
		state->hf_terrain_right_col_x = 130.0f;
		state->hf_terrain_bg_panel = kui_panel_control_create(kui_state, "hf_terrain_bg_panel", (vec2){state->hf_terrain_window_width, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->hf_terrain_bg_panel));
		kui_control_position_set(kui_state, state->hf_terrain_bg_panel, (vec3){1280 - (state->hf_terrain_window_width + 10)});
		kui_control_set_is_active(kui_state, state->hf_terrain_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->hf_terrain_bg_panel, false);

		// Window Label
		state->hf_terrain_title = kui_label_control_create(kui_state, "hf_terrain_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Heightfield Terrain Editor");
		KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hf_terrain_title));
		kui_control_position_set(kui_state, state->hf_terrain_title, (vec3){10, -5.0f, 0});

		// Terrain save button HACK: This should have its own space alongside creation tools (create button, x/z dimensions, etc.)
		{
			state->hf_terrain_save_button = kui_button_control_create_with_text(kui_state, "hf_terrain_save_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Save HF Terr.");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hf_terrain_save_button));
			kui_button_control_width_set(kui_state, state->hf_terrain_save_button, 150);
			kui_control_position_set(kui_state, state->hf_terrain_save_button, (vec3){300, 0, 0});
			kui_control_set_user_data(kui_state, state->hf_terrain_save_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->hf_terrain_save_button, hft_save_button_clicked);
		}

		// General sub-mode - active by default.
		{
			state->hft_mode_general_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_general_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Gen.");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_general_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_general_checkbox, (vec3){5, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_general_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_general_checkbox, hf_terrain_checkbox_check_changed);
			kui_checkbox_set_checked(kui_state, state->hft_mode_general_checkbox, true);

			// Content pane
			state->hft_mode_general_content = kui_base_control_create(kui_state, "hft_mode_general_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_general_content));
			kui_control_position_set(kui_state, state->hft_mode_general_content, (vec3){5, 90, 0});

			// Scrollable content control
			state->hft_general_scrollable_control = kui_scrollable_control_create(kui_state, "hft_general_scrollable_control", (vec2){state->hf_terrain_window_width, 200}, true, true);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_general_content, state->hft_general_scrollable_control));
			kui_control_position_set(kui_state, state->hft_general_scrollable_control, (vec3){0, 50, 0});

			state->hft_general_content_container = kui_scrollable_control_get_content_container(state->kui_state, state->hft_general_scrollable_control);

			// Terrain material listing
			f32 imagebox_size = 64;
			f32 imagebox_padding = 5.0f;

			for (u8 i = 0; i < HF_TERRAIN_MAX_MATERIALS; ++i) {
				// Label
				{
					char *name = string_format("hft_general_material_name_%u", i);
					char *text = string_format("Material %u", i);
					state->hft_general_material_names[i] = kui_label_control_create(state->kui_state, name, FONT_TYPE_SYSTEM, state->font_name, state->font_size, text);
					string_free(name);
					string_free(text);
					KASSERT(kui_system_control_add_child(kui_state, state->hft_general_content_container, state->hft_general_material_names[i]));
					kui_control_position_set(kui_state, state->hft_general_material_names[i], (vec3){5, i * (imagebox_size + imagebox_padding), 0});
				}
				// Albedo
				{
					char *name = string_format("hft_general_material_albedo_image_box_%u", i);
					state->hft_general_material_albedo_image_boxes[i] = kui_image_box_control_create(state->kui_state, name, (vec2i){imagebox_size, imagebox_size});
					string_free(name);
					hf_terrain_material_imagebox_context *context = KALLOC_TYPE(hf_terrain_material_imagebox_context, MEMORY_TAG_EDITOR);
					context->editor = state;
					context->material_index = i;
					context->map = HF_TERRAIN_MATERIAL_MAP_ALBEDO;
					KASSERT(kui_system_control_add_child(kui_state, state->hft_general_content_container, state->hft_general_material_albedo_image_boxes[i]));
					kui_control_position_set(kui_state, state->hft_general_material_albedo_image_boxes[i], (vec3){200, i * (imagebox_size + imagebox_padding), 0});
					kui_control_set_user_data(kui_state, state->hft_general_material_albedo_image_boxes[i], sizeof(hf_terrain_material_imagebox_context), context, true, MEMORY_TAG_EDITOR);
					kui_control_set_on_click(kui_state, state->hft_general_material_albedo_image_boxes[i], hft_material_imagebox_clicked);
				}
				// Normal
				{
					char *name = string_format("hft_general_material_normal_image_box_%u", i);
					state->hft_general_material_normal_image_boxes[i] = kui_image_box_control_create(state->kui_state, name, (vec2i){imagebox_size, imagebox_size});
					string_free(name);
					hf_terrain_material_imagebox_context *context = KALLOC_TYPE(hf_terrain_material_imagebox_context, MEMORY_TAG_EDITOR);
					context->editor = state;
					context->material_index = i;
					context->map = HF_TERRAIN_MATERIAL_MAP_NORMAL;
					KASSERT(kui_system_control_add_child(kui_state, state->hft_general_content_container, state->hft_general_material_normal_image_boxes[i]));
					kui_control_position_set(kui_state, state->hft_general_material_normal_image_boxes[i], (vec3){269, i * (imagebox_size + imagebox_padding), 0});
					kui_control_set_user_data(kui_state, state->hft_general_material_normal_image_boxes[i], sizeof(hf_terrain_material_imagebox_context), context, true, MEMORY_TAG_EDITOR);
					kui_control_set_on_click(kui_state, state->hft_general_material_normal_image_boxes[i], hft_material_imagebox_clicked);
				}
				// MRA
				{
					char *name = string_format("hft_general_material_mra_image_box_%u", i);
					state->hft_general_material_mra_image_boxes[i] = kui_image_box_control_create(state->kui_state, name, (vec2i){imagebox_size, imagebox_size});
					string_free(name);
					hf_terrain_material_imagebox_context *context = KALLOC_TYPE(hf_terrain_material_imagebox_context, MEMORY_TAG_EDITOR);
					context->editor = state;
					context->material_index = i;
					context->map = HF_TERRAIN_MATERIAL_MAP_MRA;
					KASSERT(kui_system_control_add_child(kui_state, state->hft_general_content_container, state->hft_general_material_mra_image_boxes[i]));
					kui_control_position_set(kui_state, state->hft_general_material_mra_image_boxes[i], (vec3){338, i * (imagebox_size + imagebox_padding), 0});
					kui_control_set_user_data(kui_state, state->hft_general_material_mra_image_boxes[i], sizeof(hf_terrain_material_imagebox_context), context, true, MEMORY_TAG_EDITOR);
					kui_control_set_on_click(kui_state, state->hft_general_material_mra_image_boxes[i], hft_material_imagebox_clicked);
				}
			}

			kui_scrollable_set_content_size(state->kui_state, state->hft_general_scrollable_control, state->hf_terrain_window_width, (imagebox_size + imagebox_padding) * HF_TERRAIN_MAX_MATERIALS);
		}

		// Paint sub-mode
		{

			// Some reasonable defaults.
			state->hft_paint_brush_diameter = 19;
			state->hft_paint_brush_strength = 5;
			state->hft_paint_material_index = 1;

			state->hft_mode_paint_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_paint_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Paint");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_paint_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_paint_checkbox, (vec3){90, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_paint_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_paint_checkbox, hf_terrain_checkbox_check_changed);

			// Content pane
			state->hft_mode_paint_content = kui_base_control_create(kui_state, "hft_mode_paint_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_paint_content));
			kui_control_position_set(kui_state, state->hft_mode_paint_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_paint_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_paint_content, false);

			state->hft_paint_brush_diameter_label = kui_label_control_create(kui_state, "hft_paint_brush_diameter_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Diameter");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_diameter_label));
			kui_control_position_set(kui_state, state->hft_paint_brush_diameter_label, (vec3){5, 0, 0});

			state->hft_paint_brush_strength_label = kui_label_control_create(kui_state, "hft_paint_brush_strength_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Strength");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_strength_label));
			kui_control_position_set(kui_state, state->hft_paint_brush_strength_label, (vec3){5, 50, 0});

			state->hft_paint_material_index_label = kui_label_control_create(kui_state, "hft_paint_brush_material_index_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Mat. Index");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_material_index_label));
			kui_control_position_set(kui_state, state->hft_paint_material_index_label, (vec3){5, 100, 0});

			// Paint brush diameter textbox.
			state->hft_paint_brush_diameter_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_diameter_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_diameter_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_diameter_textbox, (vec3){state->hf_terrain_right_col_x, 0, 0});
			kui_textbox_i64_set(kui_state, state->hft_paint_brush_diameter_textbox, state->hft_paint_brush_diameter);
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_diameter_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_diameter_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_diameter_textbox, hft_paint_brush_diameter_textbox_on_key);

			// Strength
			state->hft_paint_brush_strength_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_strength_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_strength_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_strength_textbox, (vec3){state->hf_terrain_right_col_x, 50, 0});
			kui_textbox_i64_set(kui_state, state->hft_paint_brush_strength_textbox, state->hft_paint_brush_strength);
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_strength_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_strength_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_strength_textbox, hft_paint_brush_strength_textbox_on_key);

			// Erase checkbox.
			state->hft_paint_brush_erase_checkbox = kui_checkbox_control_create(kui_state, "hft_paint_brush_erase_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Erase");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_erase_checkbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_erase_checkbox, (vec3){state->hf_terrain_right_col_x + 130, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_paint_brush_erase_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_paint_brush_erase_checkbox, hf_terrain_erase_checkbox_check_changed);

			// Material index.
			state->hft_paint_brush_material_index_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_material_index_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_material_index_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_material_index_textbox, (vec3){state->hf_terrain_right_col_x, 100, 0});
			kui_textbox_i64_set(kui_state, state->hft_paint_brush_material_index_textbox, state->hft_paint_material_index);
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_material_index_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_material_index_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_material_index_textbox, hft_paint_material_index_textbox_on_key);
		}

		// Elevation sub-mode
		{
			// Reasonable tool defaults
			state->hft_elevation_amount = 0.01f;
			state->hft_elevation_diameter = 5.0f;
			state->hft_elevation_set_height = false;

			state->hft_mode_elevation_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_elevation_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Elev.");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_elevation_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_elevation_checkbox, (vec3){190, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_elevation_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_elevation_checkbox, hf_terrain_checkbox_check_changed);

			// Content pane
			state->hft_mode_elevation_content = kui_base_control_create(kui_state, "hft_mode_elevation_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_elevation_content));
			kui_control_position_set(kui_state, state->hft_mode_elevation_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_elevation_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_elevation_content, false);

			state->hft_elevation_diameter_label = kui_label_control_create(kui_state, "hft_elevation_diameter_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Diameter");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_elevation_content, state->hft_elevation_diameter_label));
			kui_control_position_set(kui_state, state->hft_elevation_diameter_label, (vec3){5, 0, 0});

			state->hft_elevation_amount_label = kui_label_control_create(kui_state, "hft_elevation_strength_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Amount");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_elevation_content, state->hft_elevation_amount_label));
			kui_control_position_set(kui_state, state->hft_elevation_amount_label, (vec3){5, 50, 0});

			// elevation brush diameter textbox.
			state->hft_elevation_diameter_textbox = kui_textbox_control_create(kui_state, "hft_elevation_diameter_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_elevation_content, state->hft_elevation_diameter_textbox));
			kui_control_position_set(kui_state, state->hft_elevation_diameter_textbox, (vec3){state->hf_terrain_right_col_x, 0, 0});
			kui_textbox_i64_set(kui_state, state->hft_elevation_diameter_textbox, state->hft_elevation_diameter);
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_elevation_diameter_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_elevation_diameter_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_elevation_diameter_textbox, hft_elevation_diameter_textbox_on_key);

			// Amount
			state->hft_elevation_amount_textbox = kui_textbox_control_create(kui_state, "hft_elevation_strength_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_elevation_content, state->hft_elevation_amount_textbox));
			kui_control_position_set(kui_state, state->hft_elevation_amount_textbox, (vec3){state->hf_terrain_right_col_x, 50, 0});
			kui_textbox_f32_set(kui_state, state->hft_elevation_amount_textbox, state->hft_elevation_amount);
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_elevation_amount_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_elevation_amount_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_elevation_amount_textbox, hft_elevation_amount_textbox_on_key);

			// Set height checkbox.
			state->hft_elevation_set_height_checkbox = kui_checkbox_control_create(kui_state, "hft_elevation_erase_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Set Height");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_elevation_content, state->hft_elevation_set_height_checkbox));
			kui_control_position_set(kui_state, state->hft_elevation_set_height_checkbox, (vec3){state->hf_terrain_right_col_x + 130, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_elevation_set_height_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_elevation_set_height_checkbox, hf_terrain_set_height_checkbox_check_changed);
		}

		// Chunk sub-mode
		{
			state->hft_mode_chunk_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_chunk_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Chunk");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_chunk_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_chunk_checkbox, (vec3){280, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_chunk_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_chunk_checkbox, hf_terrain_checkbox_check_changed);

			state->hft_mode_chunk_content = kui_base_control_create(kui_state, "hft_mode_chunk_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_chunk_content));
			kui_control_position_set(kui_state, state->hft_mode_chunk_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_chunk_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_chunk_content, false);

			// TODO: Add controls for material mapping
			for (u8 i = 0; i < 5; ++i) {
				f32 ypos = i * 50.0f;

				// Material label
				char *name = string_format("hft_chunk_material_labels[%u]", i);
				char *text = string_format("Slot %u", i);
				state->hft_chunk_material_labels[i] = kui_label_control_create(kui_state, name, FONT_TYPE_SYSTEM, state->font_name, state->font_size, text);
				string_free(name);
				string_free(text);
				KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_chunk_content, state->hft_chunk_material_labels[i]));
				kui_control_position_set(kui_state, state->hft_chunk_material_labels[i], (vec3){5, ypos, 0});

				// Material index
				name = string_format("hft_chunk_material_textboxes[%u]", i);
				state->hft_chunk_material_textboxes[i] = kui_textbox_control_create(kui_state, name, FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_INT);
				string_free(name);
				KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_chunk_content, state->hft_chunk_material_textboxes[i]));
				kui_control_position_set(kui_state, state->hft_chunk_material_textboxes[i], (vec3){state->hf_terrain_right_col_x, ypos, 0});
				KASSERT(kui_textbox_control_width_set(kui_state, state->hft_chunk_material_textboxes[i], 40));

				hf_terrain_chunk_material_context *ctrl_context = KALLOC_TYPE(hf_terrain_chunk_material_context, MEMORY_TAG_EDITOR);
				ctrl_context->editor = state;
				ctrl_context->material_slot = i;
				kui_control_set_user_data(kui_state, state->hft_chunk_material_textboxes[i], sizeof(*ctrl_context), ctrl_context, true, MEMORY_TAG_EDITOR);
				kui_control_set_on_key(kui_state, state->hft_chunk_material_textboxes[i], hft_chunk_material_textbox_on_key);
			}
		}

		// Remove sub-mode
		{
			state->hft_mode_remove_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_remove_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Remove");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_remove_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_remove_checkbox, (vec3){400, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_remove_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_remove_checkbox, hf_terrain_checkbox_check_changed);

			state->hft_mode_remove_content = kui_base_control_create(kui_state, "hft_mode_remove_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_remove_content));
			kui_control_position_set(kui_state, state->hft_mode_remove_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_remove_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_remove_content, false);
		}
	}

	// Texture browser
	{
		texture_browser_create_info create_info = {
			.ui = kui_state,
			.editor_root = state->editor_root,
			.game_package_name = game_package_name,
			.font_size = state->font_size,
			.font_name = state->font_name,
			.textbox_font_size = state->textbox_font_size,
			.textbox_font_name = state->textbox_font_name};
		texture_browser_create(&state->tex_browser, create_info);
	}

	// Listing of editor base controls.
	for (u16 i = 0; i < EDITOR_MODE_MAX; ++i) {
		state->base_mode_controls[i] = INVALID_KUI_CONTROL;
	}
	state->base_mode_controls[EDITOR_MODE_SCENE] = state->scene_inspector_bg_panel;
	state->base_mode_controls[EDITOR_MODE_ENTITY] = state->entity_inspector_bg_panel;
	state->base_mode_controls[EDITOR_MODE_TREE] = state->tree_inspector_bg_panel;
	state->base_mode_controls[EDITOR_MODE_HF_TERRAIN] = state->hf_terrain_bg_panel;

	state->is_running = true;

	return true;
}
void editor_shutdown (struct editor_state *state) {

	editor_gizmo_destroy(&state->gizmo);

	editor_destroy_keymaps(state);

	// TODO: dirty check. If dirty, return false here. May need some sort of callback to
	// allow a "this is saved, now we can close" function.

	KTRACE("Shutting down editor.");

	darray_destroy(state->selection_list);
	state->selection_list = KNULL;

	tree_clear(state);

	texture_browser_destroy(&state->tex_browser);

	if (state->edit_scene) {
		kscene_destroy(state->edit_scene);
		state->edit_scene = KNULL;
	}
}

b8 editor_open (struct editor_state *state, kname scene_name, kname scene_package_name) {
	kasset_text *scene_asset = asset_system_request_text_from_package_sync(
		engine_systems_get()->asset_state,
		kname_string_get(scene_package_name),
		kname_string_get(scene_name));
	if (!scene_asset) {
		KERROR("%s - Failed to request scene asset. See logs for details.", __FUNCTION__);
		return false;
	}

	KINFO("Opening editor scene...");

	// Creates scene and triggers load.
	state->edit_scene = kscene_create(scene_name, scene_asset->content, 0, 0, true);
	state->scene_asset_name = scene_name;
	state->scene_package_name = scene_package_name;

	asset_system_release_text(engine_systems_get()->asset_state, scene_asset);
	if (!state->edit_scene) {
		KERROR("%s - Failed to create and load scene. See logs for details.", __FUNCTION__);
		return false;
	}

	// Reset checkboxes
	b8 debug_enabled = kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_ENABLED_BIT);
	kui_checkbox_set_checked(state->kui_state, state->view_debug_checkbox, debug_enabled);
	kui_control_set_is_active(state->kui_state, state->view_bvh_checkbox, debug_enabled);
	kui_control_set_is_active(state->kui_state, state->view_grid_checkbox, debug_enabled);
	kui_checkbox_set_checked(state->kui_state, state->view_bvh_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT));
	kui_checkbox_set_checked(state->kui_state, state->view_grid_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT));

	// General scene visiblity
	kui_checkbox_set_checked(state->kui_state, state->view_skybox_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_RENDER_SKYBOX_BIT));
	kui_checkbox_set_checked(state->kui_state, state->view_fog_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_RENDER_FOG_BIT));

	const char *scene_name_str = kscene_get_name(state->edit_scene);
	kui_textbox_text_set(state->kui_state, state->scene_name_textbox, scene_name_str ? scene_name_str : "");
	colour4 fog_colour = kscene_get_fog_colour(state->edit_scene);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_r_textbox, fog_colour.r);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_g_textbox, fog_colour.g);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_b_textbox, fog_colour.b);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_a_textbox, fog_colour.a);

	// Setup terrain material controls.
	u8 material_count = 0;
	hf_terrain_material_data *materials = kscene_get_hf_terrain_materials(state->edit_scene, &material_count);
	for (u8 i = 0; i < HF_TERRAIN_MAX_MATERIALS; ++i) {
		// Material name.
		kstring_id name_strid = i < material_count ? materials[i].name : INVALID_KSTRING_ID;
		kui_label_text_set(state->kui_state, state->hft_general_material_names[i], (name_strid == INVALID_KSTRING_ID) ? "<empty>" : kstring_id_string_get(name_strid));

		{
			kname asset_name = i < material_count ? materials[i].albedo_asset_name : kname_create(DEFAULT_TEXTURE_NAME);
			kname package_name = i < material_count ? materials[i].albedo_asset_package_name : INVALID_KNAME;
			kui_image_box_control_texture_set_by_name(state->kui_state, state->hft_general_material_albedo_image_boxes[i], asset_name, package_name);
		}
		{
			kname asset_name = i < material_count ? materials[i].normal_asset_name : kname_create(DEFAULT_TEXTURE_NAME);
			kname package_name = i < material_count ? materials[i].normal_asset_package_name : INVALID_KNAME;
			kui_image_box_control_texture_set_by_name(state->kui_state, state->hft_general_material_normal_image_boxes[i], asset_name, package_name);
		}
		{
			kname asset_name = i < material_count ? materials[i].mra_asset_name : kname_create(DEFAULT_TEXTURE_NAME);
			kname package_name = i < material_count ? materials[i].mra_asset_package_name : INVALID_KNAME;
			kui_image_box_control_texture_set_by_name(state->kui_state, state->hft_general_material_mra_image_boxes[i], asset_name, package_name);
		}
	}
	kfree(materials);

	// If opened successfully, change keymaps.
	if (!input_keymap_pop()) {
		KERROR("No keymap was popped during world->editor");
	}
	input_keymap_push(&state->editor_keymap);

	state->is_running = true;

	// Events and console commands for the editor should only be available when it is running.
	editor_register_events(state);
	editor_register_commands(state);

	// Enable UI elements.
	kui_control_set_is_visible(state->kui_state, state->editor_root, true);

	// Set the default mode.
	editor_set_mode(state, EDITOR_MODE_SCENE);

	return true;
}

b8 editor_close (struct editor_state *state) {
	// TODO: dirty check. If dirty, return false here. May need some sort of callback to
	// allow a "this is saved, now we can close" function.

	renderer_wait_for_idle();
	KTRACE("Destroying editor scene...");
	// Unload the current zone's scene from the world.
	kscene_destroy(state->edit_scene);
	state->edit_scene = KNULL;

	state->scene_asset_name = INVALID_KNAME;
	state->scene_package_name = INVALID_KNAME;

	KTRACE("Editor scene destroyed.");

	// Events and console commands for the editor should only be available when it is running.
	editor_unregister_events(state);
	editor_unregister_commands(state);

	state->is_running = false;

	// Disable UI elements.
	kui_control_set_is_visible(state->kui_state, state->editor_root, false);

	return true;
}

static kui_control get_inspector_base_for_mode (struct editor_state *state, u16 mode) {
	return state->base_mode_controls[mode];
}

void editor_set_mode (struct editor_state *state, u16 mode) {
	// Disable current window
	kui_control window = get_inspector_base_for_mode(state, state->mode);
	if (window.val != INVALID_KUI_CONTROL.val) {
		kui_control_set_is_visible(state->kui_state, window, false);
		kui_control_set_is_active(state->kui_state, window, false);
	}

	// Set mode an enable the new.
	state->mode = mode;
	window = get_inspector_base_for_mode(state, state->mode);
	if (window.val != INVALID_KUI_CONTROL.val) {
		kui_control_set_is_visible(state->kui_state, window, true);
		kui_control_set_is_active(state->kui_state, window, true);
	}
}

void editor_clear_selected_entities (struct editor_state *state) {
	darray_clear(state->selection_list);
	state->gizmo.selected_transform = KTRANSFORM_INVALID;
	KTRACE("Selection cleared.");

	// No selection, turn stuff off.
	kui_label_text_set(state->kui_state, state->entity_inspector_title, "Entity (no selection)");
	kui_textbox_text_set(state->kui_state, state->entity_name_textbox, "");

	// Update inspector position controls.
	kui_textbox_text_set(state->kui_state, state->entity_position_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_position_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_position_z_textbox, "");

	// Update inspector orientation controls.
	kui_textbox_text_set(state->kui_state, state->entity_orientation_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_z_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_w_textbox, "");

	// Update inspector scale controls.
	kui_textbox_text_set(state->kui_state, state->entity_scale_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_scale_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_scale_z_textbox, "");
}

void editor_select_entities (struct editor_state *state, u32 count, kentity *entities) {
	editor_clear_selected_entities(state);

	editor_add_to_selected_entities(state, count, entities);
}

void editor_add_to_selected_entities (struct editor_state *state, u32 count, kentity *entities) {

	for (u32 s = 0; s < count; ++s) {
		kentity entity = entities[s];
		kname name = kscene_get_entity_name(state->edit_scene, entity);
		KINFO("Selection [%u]: '%s'", s, kname_string_get(name));
		if (!editor_selection_contains(state, entity)) {
			darray_push(state->selection_list, &entity);
		}
	}

	// Set the gizmo to the selection.
	// HACK: force single-select for now.
	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
	// TODO: Set the gizmo to an average position of all selected entity transforms,
	// and apply the modifications to transforms individually, but together.

	// Update inspector controls.
	const char *type_str = kentity_type_to_string(kentity_unpack_type(state->selection_list[0]));
	char *title_str = string_format("Entity (%s)", type_str);
	kui_label_text_set(state->kui_state, state->entity_inspector_title, title_str);
	string_free(title_str);

	kname name = kscene_get_entity_name(state->edit_scene, state->selection_list[0]);
	const char *name_str = kname_string_get(name);
	kui_textbox_text_set(state->kui_state, state->entity_name_textbox, name_str ? name_str : "");

	// Update inspector position controls.
	{
		vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
		kui_textbox_f32_set(state->kui_state, state->entity_position_x_textbox, position.x);
		kui_textbox_f32_set(state->kui_state, state->entity_position_y_textbox, position.y);
		kui_textbox_f32_set(state->kui_state, state->entity_position_z_textbox, position.z);
	}
	// Update inspector orientation controls.
	{
		quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
		kui_textbox_f32_set(state->kui_state, state->entity_orientation_x_textbox, rotation.x);
		kui_textbox_f32_set(state->kui_state, state->entity_orientation_y_textbox, rotation.y);
		kui_textbox_f32_set(state->kui_state, state->entity_orientation_z_textbox, rotation.z);
		kui_textbox_f32_set(state->kui_state, state->entity_orientation_w_textbox, rotation.w);
	}
	// Update inspector scale controls.
	{
		vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
		kui_textbox_f32_set(state->kui_state, state->entity_scale_x_textbox, scale.x);
		kui_textbox_f32_set(state->kui_state, state->entity_scale_y_textbox, scale.y);
		kui_textbox_f32_set(state->kui_state, state->entity_scale_z_textbox, scale.z);
	}
}

void editor_select_parent (struct editor_state *state) {
	u32 count = darray_length(state->selection_list);
	if (count != 1) {
		KWARN("%s - cannot select parent unless exactly one entity is selected.", __FUNCTION__);
		return;
	}

	kentity parent = kscene_get_entity_parent(state->edit_scene, state->selection_list[0]);
	if (parent == KENTITY_INVALID) {
		KINFO("Selected object has no parent.");
		return;
	}

	state->selection_list[0] = parent;

	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
}

b8 editor_selection_contains (struct editor_state *state, kentity entity) {
	u32 selection_count = darray_length(state->selection_list);
	for (u32 s = 0; s < selection_count; ++s) {
		if (state->selection_list[s] == entity) {
			return true;
		}
	}

	return false;
}

void editor_update (struct editor_state *state, frame_data *p_frame_data) {
	editor_gizmo_update(&state->gizmo, state->editor_camera);

	// Update the listener orientation. In editor mode, the sound follows the camera.
	vec3 cam_pos = kcamera_get_position(state->editor_camera);
	vec3 cam_forward = kcamera_forward(state->editor_camera);
	vec3 cam_up = kcamera_up(state->editor_camera);
	kaudio_system_listener_orientation_set(engine_systems_get()->audio_system, cam_pos, cam_forward, cam_up);

	if (!kscene_update(state->edit_scene, p_frame_data)) {
		KWARN("Failed to update editor scene.");
	}

	if (state->trigger_tree_refresh) {
		tree_refresh(state);
		state->trigger_tree_refresh = false;
	}
}

void editor_frame_prepare (struct editor_state *state, frame_data *p_frame_data, kcamera current_camera, b8 draw_gizmo, keditor_gizmo_pass_render_data *gizmo_pass_render_data) {
	// Setup data required for the editor gizmo pass

	editor_gizmo_render_frame_prepare(&state->gizmo, p_frame_data);
	b8 has_selection = state->selection_list && darray_length(state->selection_list);

	gizmo_pass_render_data->do_pass = state->mode == EDITOR_MODE_ENTITY && has_selection && draw_gizmo;
	if (gizmo_pass_render_data->do_pass) {

		gizmo_pass_render_data->projection_id = state->gizmo.render_projection_id;
		gizmo_pass_render_data->view_id = kcamera_get_view_id(state->editor_camera);
		gizmo_pass_render_data->visible = has_selection;
		gizmo_pass_render_data->gizmo_transform_id = state->gizmo.render_model_id;

		kgeometry g = state->gizmo.mode_data[state->gizmo.mode].geo;
		kdebug_geometry_render_data *geo_rd = &gizmo_pass_render_data->geometry;
		geo_rd->geo.index_count = g.index_count;
		geo_rd->geo.index_offset = g.index_buffer_offset;
		geo_rd->geo.vertex_count = g.vertex_count;
		geo_rd->geo.vertex_offset = g.vertex_buffer_offset;
		geo_rd->geo.transform = KTRANSFORM_INVALID; // NOTE: transform isn't directly used here. app->state->gizmo.ktransform_handle;

		// Inverted winding not supported for debug geometries.
		geo_rd->geo.flags = FLAG_SET(geo_rd->geo.flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT, false);
	}
}

static void set_render_state_defaults (rect_2di vp_rect) {
	/* renderer_begin_debug_label("frame defaults", KCOLOUR4_BLACK); */

	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_set_stencil_test_enabled(false);
	renderer_set_stencil_compare_mask(0);
	renderer_set_depth_bias_enabled(false);
	renderer_set_depth_bias(0.0f, 0.0f, 0.0f);

	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
	// Default winding is counter clockwise
	renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

	rect_2di viewport_rect = {vp_rect.x, vp_rect.y + vp_rect.height, vp_rect.width, -(f32)vp_rect.height};
	renderer_viewport_set(viewport_rect);

	rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
	renderer_scissor_set(scissor_rect);

	/* renderer_end_debug_label(); */
}

b8 editor_render (struct editor_state *state, frame_data *p_frame_data, kcamera current_camera, ktexture colour_buffer_target, b8 draw_gizmo, keditor_gizmo_pass_render_data *render_data) {

	rect_2di vp_rect = {0};
	if (!texture_dimensions_get(colour_buffer_target, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
		return false;
	}

	renderer_begin_debug_label("Editor", (colour4){0.6f, 1.0f, 0.6f, 1.0f});

	// Editor begin render
	renderer_begin_rendering(state->renderer, p_frame_data, vp_rect, 1, &colour_buffer_target, INVALID_KTEXTURE, 0);
	set_render_state_defaults(vp_rect);

#if KOHI_DEBUG
	// NOTE: Editor gizmo only included in non-release builds
	if (render_data->do_pass) {
		if (render_data->visible) {
			renderer_begin_debug_label("Editor gizmo", (colour4){0.5f, 1.0f, 0.5f, 1.0f});

			// Disable depth test/write so the gizmo is always on top.
			renderer_set_depth_test_enabled(false);
			renderer_set_depth_write_enabled(false);
			renderer_set_stencil_test_enabled(false);

			kshader_system_use_with_topology(state->editor_gizmo_pass.gizmo_shader, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT, 0);
			renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

			// Global binding set
			kshader_apply_binding_set(state->editor_gizmo_pass.gizmo_shader, 0, state->editor_gizmo_pass.set0_instance_id);

			kdebug_geometry_render_data *g = &render_data->geometry;

			editor_gizmo_immediate_data immediate_data = {
				.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_VIEW),
				.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_PROJECTION),
				.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_TRANSFORM),
				.mat_ssbo_genc_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_GENERAL),
				.view_index = UNPACK_U32_U16_AT(render_data->view_id, 1),
				.projection_index = UNPACK_U32_U16_AT(render_data->projection_id, 1),
				.model_index = UNPACK_U32_U16_AT(render_data->gizmo_transform_id, 1)};
			kshader_set_immediate_data(state->editor_gizmo_pass.gizmo_shader, &immediate_data, sizeof(editor_gizmo_immediate_data));

			// Draw it.
			b8 includes_index_data = g->geo.index_count > 0;

			if (!renderer_renderbuffer_draw(state->renderer, state->standard_vertex_buffer, g->geo.vertex_offset, g->geo.vertex_count, 0, includes_index_data)) {
				KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
				return false;
			}
			if (includes_index_data) {
				if (!renderer_renderbuffer_draw(state->renderer, state->index_buffer, g->geo.index_offset, g->geo.index_count, 0, !includes_index_data)) {
					KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
					return false;
				}
			}

			renderer_end_debug_label(); // gizmo label
		}
	}
#endif

	// Debug points render, if applicable
	if (state->debug_point_count) {
		renderer_begin_debug_label("Editor debug points", (colour4){0.4f, 1.0f, 0.4f, 1.0f});

		kshader_system_use_with_topology(state->colour_shader, PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT, 0);
		renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

		// Global Binding set
		u32 colour_set0_instance_id = 0;
		kshader_apply_binding_set(state->colour_shader, 0, colour_set0_instance_id);

		colour_3d_immediate_data immediate_data = {
			.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_VIEW),
			.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_PROJECTION),
			.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_TRANSFORM),
			.mat_ssbo_genc_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_GENERAL),
			.view_index = UNPACK_U32_U16_AT(kcamera_get_view_id(current_camera), 1),
			.projection_index = UNPACK_U32_U16_AT(kcamera_get_projection_id(current_camera), 1),
			// Just using the default debug identity matrix since transform doesn't matter here.
			.model_index = UNPACK_U32_U16_AT(state->default_identity_matrix_id, 1)};
		kshader_set_immediate_data(state->colour_shader, &immediate_data, sizeof(colour_3d_immediate_data));

		// NOTE: May need to do this earlier, like in frame prepare
		renderer_renderbuffer_load_range(
			state->renderer,
			state->standard_vertex_buffer,
			state->debug_points_vertex_buffer_offset,
			sizeof(colour_vertex_3d) * state->debug_point_count,
			state->debug_points,
			false);

		if (!renderer_renderbuffer_draw(
				state->renderer,
				state->standard_vertex_buffer,
				state->debug_points_vertex_buffer_offset,
				state->debug_point_count,
				0,
				false)) {
			KERROR("renderer_renderbuffer_draw failed to draw vertex buffer for debug points data");
			return false;
		}
		renderer_end_debug_label(); // debug points label
	}

	// Editor-specific Debug shapes
	// TODO: debug shape renderer?
	renderer_begin_debug_label("Editor debug shapes", (colour4){0.4f, 1.0f, 0.3f, 1.0f});
	if (state->mode == EDITOR_MODE_HF_TERRAIN && state->hft_edit_mode == HF_TERRAIN_EDIT_MODE_CHUNK) {
		KASSERT(kshader_system_use_with_topology(state->debug_shader, PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT, 0));

		// Global Binding set
		// "Global" should always be instance 0
		u32 instance_id = 0;
		kshader_apply_binding_set(state->debug_shader, 0, instance_id);

		// Render the debug data.
		kgeometry *geo = &state->hft_selected_chunk_debug_box;

		debug_shader_immediate_data immediate_data = {
			.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_VIEW),
			.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_PROJECTION),
			.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_TRANSFORM),
			.mat_ssbo_genc_offset = kmatrix_system_get_offset_by_type(state->matrix_system, KMATRIX_TYPE_GENERAL),
			.view_index = UNPACK_U32_U16_AT(kcamera_get_view_id(current_camera), 1),
			.projection_index = UNPACK_U32_U16_AT(kcamera_get_projection_id(current_camera), 1),
			// Just using the default debug identity matrix since transform doesn't matter here.
			.model_index = UNPACK_U32_U16_AT(state->default_identity_matrix_id, 1),
			.colour = vec4_create(1.0f, 0.5f, 0.0f, 1.0f)}; // HACK: hardcoded colour.
		kshader_set_immediate_data(state->debug_shader, &immediate_data, sizeof(immediate_data));

		// Draw it.
		b8 includes_index_data = geo->index_count > 0;

		if (!renderer_renderbuffer_draw(state->renderer, state->standard_vertex_buffer, geo->vertex_buffer_offset, geo->vertex_count, 0, includes_index_data)) {
			KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
			return false;
		}
		if (includes_index_data) {
			if (!renderer_renderbuffer_draw(state->renderer, state->index_buffer, geo->index_buffer_offset, geo->index_count, 0, !includes_index_data)) {
				KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
				return false;
			}
		}
	}
	renderer_end_debug_label(); // debug shapes label

	// Editor end render
	renderer_end_rendering(state->renderer, p_frame_data);

	renderer_end_debug_label(); // editor label
	return true;
}

void editor_on_window_resize (struct editor_state *state, const struct kwindow *window) {
	if (!window->width || !window->height) {
		return;
	}

	// Resize cameras.
	rect_2di world_vp_rect = {0, 0, window->width, window->height};

	kcamera_set_vp_rect(state->editor_camera, world_vp_rect);

	// Send the resize off to the scene, if it exists.
	kscene_on_window_resize(state->edit_scene, window);

	// UI elements
	kui_control_position_set(state->kui_state, state->scene_inspector_bg_panel, (vec3){window->width - (state->scene_inspector_width + 10), 10});
	kui_control_position_set(state->kui_state, state->entity_inspector_bg_panel, (vec3){window->width - (state->entity_inspector_width + 10), 10});

	kui_control_position_set(state->kui_state, state->tree_inspector_bg_panel, (vec3){window->width - (state->tree_inspector_width + 10), 10});
	kui_control_position_set(state->kui_state, state->hf_terrain_bg_panel, (vec3){window->width - (state->hf_terrain_window_width + 10), 10});

	// HACK: hardcoded offset.
	f32 tree_bottom_offset = 420.0f;
	kui_panel_set_height(state->kui_state, state->tree_inspector_bg_panel, window->height - tree_bottom_offset);
	kui_scrollable_control_resize(state->kui_state, state->tree_scrollable_control, (vec2){state->tree_inspector_width, window->height - tree_bottom_offset - 50.0f});

	// HACK: hardcoded offset.
	f32 hf_terrain_bottom_offset = 200.0f;
	kui_panel_set_height(state->kui_state, state->hf_terrain_bg_panel, window->height - hf_terrain_bottom_offset);
	kui_scrollable_control_resize(state->kui_state, state->hft_general_scrollable_control, (vec2){state->hf_terrain_window_width, window->height - hf_terrain_bottom_offset - 200.0f});

	/* // texture browser.

	u32 x = 0;
	u32 y = 0;
	f32 inspector_width = state->tex_browser_window_size.x - state->tex_browser_right_col_x;
	f32 scrollable_width = state->tex_browser_window_size.x - inspector_width;

	for (u32 i = 0; i < state->tex_browser_tex_count; ++i) {

		// positioning
		b8 has_space = (state->tex_tile_size.x * (x + 1)) <= scrollable_width;
		if (!has_space) {
			x = 0;
			y++;
		}

		vec3 pos = (vec3){state->tex_tile_size.x * x, state->tex_tile_size.y * y, 0};
		kui_control_position_set(state->kui_state, state->tex_browser_image_boxes[i], pos);
		kui_control_position_set(state->kui_state, state->tex_browser_labels[i], vec3_sub(pos, vec3_create(0, state->font_size + state->imagebox_padding, 0)));

		if (has_space) {
			++x;
		}
	}

	f32 scrollable_height = state->tex_tile_size.y * (y + 1);
	kui_scrollable_set_content_size(state->kui_state, state->tex_browser_scrollable_control, scrollable_width, scrollable_height);
	kui_scrollable_control_resize(state->kui_state, state->tex_browser_scrollable_control, (vec2){scrollable_width, 275}); */
}

void editor_setup_keymaps (struct editor_state *state) {
	state->editor_keymap = keymap_create();
	keymap *km = &state->editor_keymap;
	/* state->editor_keymap.overrides_all = true; */

	keymap_binding_add(km, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_LEFT);
	keymap_binding_add(km, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_LEFT);

	keymap_binding_add(km, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_RIGHT);
	keymap_binding_add(km, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_RIGHT);

	keymap_binding_add(km, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_UP);
	keymap_binding_add(km, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_LOOK_DOWN);

	keymap_binding_add(km, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_FORWARD);
	keymap_binding_add(km, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, EDITOR_ACTION_SPRINT_FORWARD);
	keymap_binding_add(km, KEY_S, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_BACKWARD);
	keymap_binding_add(km, KEY_Q, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_LEFT);
	keymap_binding_add(km, KEY_E, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_RIGHT);
	keymap_binding_add(km, KEY_SPACE, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_UP);
	keymap_binding_add(km, KEY_X, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_MOVE_DOWN);

	keymap_binding_add(km, KEY_0, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_RENDER_MODE_DEFAULT);
	keymap_binding_add(km, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_RENDER_MODE_LIGHTING);
	keymap_binding_add(km, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_RENDER_MODE_NORMALS);
	keymap_binding_add(km, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_RENDER_MODE_CASCADES);
	keymap_binding_add(km, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_RENDER_MODE_WIREFRAME);

	keymap_binding_add(km, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_GIZMO_MODE_NONE);
	keymap_binding_add(km, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_GIZMO_MODE_MOVE);
	keymap_binding_add(km, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_GIZMO_MODE_ROTATE);
	keymap_binding_add(km, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_GIZMO_MODE_SCALE);
	keymap_binding_add(km, KEY_G, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_GIZMO_TOGGLE_ORIENTATION);

	// ctrl s
	keymap_binding_add(km, KEY_S, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, EDITOR_ACTION_SCENE_SAVE);

	keymap_binding_add(km, KEY_Z, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, EDITOR_ACTION_ZOOM_EXTENTS);
}

void editor_destroy_keymaps (struct editor_state *state) {
	keymap_clear(&state->editor_keymap);
}

b8 editor_on_action (struct editor_state *state, u32 action_code) {
	switch (action_code) {
	case EDITOR_ACTION_MOVE_FORWARD:
		if (!editor_has_focused_control(state)) {
			f32 delta = get_engine_delta_time();
			kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * delta);
		}
		return true;
	case EDITOR_ACTION_MOVE_BACKWARD:
		if (!editor_has_focused_control(state)) {
			f32 delta = get_engine_delta_time();
			kcamera_move_backward(state->editor_camera, state->editor_camera_backward_move_speed * delta);
		}
		return true;
	case EDITOR_ACTION_SPRINT_FORWARD:
		if (!editor_has_focused_control(state)) {
			f32 delta = get_engine_delta_time();
			kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * 2 * delta);
		}
		return true;
	case EDITOR_ACTION_MOVE_LEFT:
		if (!editor_has_focused_control(state)) {
			f32 delta = get_engine_delta_time();
			kcamera_move_left(state->editor_camera, state->editor_camera_forward_move_speed * delta);
		}
		return true;
	case EDITOR_ACTION_MOVE_RIGHT:
		if (!editor_has_focused_control(state)) {
			f32 delta = get_engine_delta_time();
			kcamera_move_right(state->editor_camera, state->editor_camera_forward_move_speed * delta);
		}
		return true;
	case EDITOR_ACTION_MOVE_UP:
		if (!editor_has_focused_control(state)) {
			kcamera_move_up(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
		}
		return true;
	case EDITOR_ACTION_MOVE_DOWN:
		if (!editor_has_focused_control(state)) {
			kcamera_move_down(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
		}
		return true;
	case EDITOR_ACTION_LOOK_LEFT:
		kcamera_yaw(state->editor_camera, 1.0f * get_engine_delta_time());
		return true;
	case EDITOR_ACTION_LOOK_RIGHT:
		kcamera_yaw(state->editor_camera, -1.0f * get_engine_delta_time());
		return true;
	case EDITOR_ACTION_LOOK_UP:
		kcamera_pitch(state->editor_camera, 1.0f * get_engine_delta_time());
		return true;
	case EDITOR_ACTION_LOOK_DOWN:
		kcamera_pitch(state->editor_camera, -1.0f * get_engine_delta_time());
		return true;
	case EDITOR_ACTION_ZOOM_EXTENTS:
		zoom_extents(state);
		return true;

	case EDITOR_ACTION_RENDER_MODE_DEFAULT:
		if (!editor_has_focused_control(state)) {
			console_command_execute("render_mode_set 0");
		}
		return true;
	case EDITOR_ACTION_RENDER_MODE_LIGHTING:
		if (!editor_has_focused_control(state)) {
			console_command_execute("render_mode_set 1");
		}
		return true;
	case EDITOR_ACTION_RENDER_MODE_NORMALS:
		if (!editor_has_focused_control(state)) {
			console_command_execute("render_mode_set 2");
		}
		return true;
	case EDITOR_ACTION_RENDER_MODE_CASCADES:
		if (!editor_has_focused_control(state)) {
			console_command_execute("render_mode_set 3");
		}
		return true;
	case EDITOR_ACTION_RENDER_MODE_WIREFRAME:
		if (!editor_has_focused_control(state)) {
			console_command_execute("render_mode_set 4");
		}
		return true;

	case EDITOR_ACTION_GIZMO_MODE_NONE:
		editor_gizmo_mode_set(&state->gizmo, EDITOR_GIZMO_MODE_NONE);
		return true;
	case EDITOR_ACTION_GIZMO_MODE_MOVE:
		editor_gizmo_mode_set(&state->gizmo, EDITOR_GIZMO_MODE_MOVE);
		return true;
	case EDITOR_ACTION_GIZMO_MODE_ROTATE:
		editor_gizmo_mode_set(&state->gizmo, EDITOR_GIZMO_MODE_ROTATE);
		return true;
	case EDITOR_ACTION_GIZMO_MODE_SCALE:
		editor_gizmo_mode_set(&state->gizmo, EDITOR_GIZMO_MODE_SCALE);
		return true;
	case EDITOR_ACTION_GIZMO_TOGGLE_ORIENTATION: {
		editor_gizmo_orientation o = editor_gizmo_orientation_get(&state->gizmo);
		o = (o == EDITOR_GIZMO_ORIENTATION_LOCAL) ? EDITOR_GIZMO_ORIENTATION_GLOBAL : EDITOR_GIZMO_ORIENTATION_LOCAL;
		editor_gizmo_orientation_set(&state->gizmo, o);
		return true;
	}

	case EDITOR_ACTION_SCENE_SAVE:
		if (!editor_has_focused_control(state)) {
			save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
		}
		return true;
	}

	return false;
}

static f32 get_engine_delta_time (void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_delta_get(engine);
}

static f32 get_engine_total_time (void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_total_get(engine);
}

static b8 editor_has_focused_control (editor_state *editor) {
	return editor->kui_state->focused.val != INVALID_KUI_CONTROL.val;
}

static void save_scene (const struct kscene *scene, kname package_name, kname asset_name) {
	if (scene) {
		kscene_state scene_state = kscene_state_get(scene);
		if (scene_state == KSCENE_STATE_LOADED) {
			KDEBUG("Saving current scene...");
			const char *serialized = kscene_serialize(scene);
			if (!serialized) {
				KERROR("Scene serialization failed! Scene save thus fails. Check logs.");
				return;
			}

			// Write the text asset to disk
			if (!asset_system_write_text(engine_systems_get()->asset_state, package_name, asset_name, serialized)) {
				KERROR("Failed to save scene asset.");
			}
		} else {
			KERROR("Current scene is not in a loaded state, and cannot be saved.");
		}
	} else {
		KERROR("No scene is open to be saved.");
	}
}

static void zoom_extents (struct editor_state *state) {
	KTRACE("Zoom extents");

	if (darray_length(state->selection_list)) {

		/* ktransform t = kscene_get_entity_transform(state->edit_scene, state->selection_list[0]);
		vec3 center = ktransform_world_position_get(t); */

		mat4 view = kcamera_get_view(state->editor_camera);

		f32 fov = kcamera_get_fov(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		f32 aspect = (f32)vp_rect.width / vp_rect.height;
		f32 tan_half_fov_y = ktan(fov * 0.5f);
		f32 tan_half_fov_x = tan_half_fov_y * aspect;

		f32 required_distance = 0.0f;

		aabb box = kscene_get_aabb(state->edit_scene, state->selection_list[0]);
		vec3 min = box.min;
		vec3 max = box.max;

		vec3 corners[8] = {
			{min.x, min.y, min.z},
			{max.x, min.y, min.z},
			{min.x, max.y, min.z},
			{max.x, max.y, min.z},
			{min.x, min.y, max.z},
			{max.x, min.y, max.z},
			{min.x, max.y, max.z},
			{max.x, max.y, max.z},
		};

		vec3 center = vec3_zero();
		for (u32 i = 0; i < 8; ++i) {
			// Center is the average of all points.
			center = vec3_add(center, corners[i]);

			// Move the corner to camera space.
			corners[i] = vec3_transform(corners[i], 1.0f, view);

			f32 x = kabs(corners[i].x);
			f32 y = kabs(corners[i].y);
			f32 z = -corners[i].z; // camera looks -z

			// Ignore corners behind camera .
			if (z <= 0.0f) {
				continue;
			}

			f32 d_x = x / tan_half_fov_x;
			f32 d_y = y / tan_half_fov_y;

			f32 d = KMAX(d_x, d_y);
			if (d > required_distance) {
				required_distance = d;
			}
		}
		center = vec3_div_scalar(center, 8.0f);

		// Pad it a bit.
		required_distance *= 1.05f;

		vec3 position = vec3_mul_scalar(kcamera_forward(state->editor_camera), required_distance);
		position = vec3_sub(center, position);

		kcamera_set_position(state->editor_camera, position);
	}
}

static void editor_command_execute (console_command_context context) {
	editor_state *state = (editor_state *)context.listener;
	if (strings_equal(context.command_name, "editor_save_scene")) {
		save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
	} else if (strings_equal(context.command_name, "editor_select_parent")) {
		editor_select_parent(state);
	} else if (strings_equal(context.command_name, "editor_dump_hierarchy")) {
		kscene_dump_hierarchy(state->edit_scene);
	} else if (strings_equal(context.command_name, "editor_set_selected_rotation")) {
		if (context.argument_count != 4) {
			KERROR("editor_set_selected_rotation requires 4 arguments (quaternion x, y, z, w)");
			return;
		} else {
			quat q = {0};
			for (u8 i = 0; i < 4; ++i) {
				string_to_f32(context.arguments[i].value, &q.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_rotation requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_rotation(state->edit_scene, state->selection_list[0], q);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_position")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_position requires 3 arguments (position x, y, z)");
			return;
		} else {
			vec3 p = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &p.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_position requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_position(state->edit_scene, state->selection_list[0], p);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_scale")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_scale requires 3 arguments (scale x, y, z)");
			return;
		} else {
			vec3 scale = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &scale.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_scale requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_scale(state->edit_scene, state->selection_list[0], scale);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_add_model")) {
		// editor_add_model "name with spaces" "asset name with spaces" "package name with spaces"
		// editor_add_model "barrels entity" "barrels model" Testbed
		kname name = kname_create(context.arguments[0].value);
		kname asset_name = kname_create(context.arguments[1].value);
		// Third property is optional and defaults to application package name.
		kname package_name = context.argument_count == 3 ? kname_create(context.arguments[2].value) : INVALID_KNAME;
		// Assign as a child of the first currently selected entity, if it exists.
		kentity parent = darray_length(state->selection_list) ? state->selection_list[0] : KENTITY_INVALID;

		kentity new_entity = kscene_add_model(state->edit_scene, name, KTRANSFORM_INVALID, parent, asset_name, package_name, 0, 0);

		// Select it
		editor_select_entities(state, 1, &new_entity);
	} else if (strings_equal(context.command_name, "ofd")) {

		// HACK: open file dialog hack for testing. Remove this.
		platform_open_file_dialog_options options = {
			.allow_multiselect = true,
			.title = "Select some danged files, ya dingus!",
			.filter = "Image Files (*.bmp;*.jpg;*.gif;*.png)|*.bmp;*.jpg;*.gif;*.png|All files (*.*)|*.*",
		};
		platform_open_file_dialog_result result = platform_open_file_dialog_open(options);
		KTRACE("Open file dialog success: %s", bool_to_string(result.success));
		for (u8 i = 0; i < result.file_count; ++i) {
			KTRACE("File selected (%u): '%s'", i, result.file_paths[i]);
		}

		// cleanup result
		if (result.file_count) {
			for (u8 i = 0; i < result.file_count; ++i) {
				string_free(result.file_paths[i]);
			}
			kfree(result.file_paths);
		}
	}
}

static void editor_register_events (struct editor_state *state) {
	KASSERT(event_register(EVENT_CODE_BUTTON_PRESSED, state, editor_on_button));
	KASSERT(event_register(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button));
	KASSERT(event_register(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag));
}

static void editor_unregister_events (struct editor_state *state) {
	event_unregister(EVENT_CODE_BUTTON_PRESSED, state, editor_on_button);
	event_unregister(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button);
	event_unregister(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move);
	event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag);
}

static void editor_register_commands (struct editor_state *state) {
	KASSERT(console_command_register("editor_save_scene", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_select_parent", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_dump_hierarchy", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_position", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_rotation", 4, 4, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_scale", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_add_model", 2, 3, state, editor_command_execute));
}

static void editor_unregister_commands (struct editor_state *state) {
	console_command_unregister("editor_save_scene");
	console_command_unregister("editor_select_parent");
	console_command_unregister("editor_dump_hierarchy");
	console_command_unregister("editor_set_selected_position");
	console_command_unregister("editor_set_selected_rotation");
	console_command_unregister("editor_set_selected_scale");
	console_command_unregister("editor_add_model");

	// HACK: testing ofd... remove this
	console_command_unregister("ofd");
}

void editor_on_lib_load (struct editor_state *state) {
	if (state->is_running) {
		editor_register_events(state);
		editor_register_commands(state);
	}
}
void editor_on_lib_unload (struct editor_state *state) {
	editor_unregister_events(state);
	editor_unregister_commands(state);
}

KAPI void editor_register_user_mode_ui_control (struct editor_state *state, u16 user_mode, kui_control base_control) {
	KASSERT(state);
	KASSERT(user_mode > EDITOR_MODE_USER_SPACE);
	state->base_mode_controls[user_mode] = base_control;
}

static b8 save_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Save button clicked.");

	console_command_execute("editor_save_scene");

	// Don't allow the event to popagate.
	return false;
}
static b8 mode_scene_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Scene mode button clicked.");
	kui_base_control *base = kui_system_get_base(state, self);
	editor_set_mode(base->user_data, EDITOR_MODE_SCENE);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_entity_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Entity mode button clicked.");
	kui_base_control *base = kui_system_get_base(state, self);
	editor_set_mode(base->user_data, EDITOR_MODE_ENTITY);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_tree_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Tree mode button clicked.");
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	if (edit_state->mode != EDITOR_MODE_TREE) {
		editor_set_mode(edit_state, EDITOR_MODE_TREE);

		edit_state->trigger_tree_refresh = true;
	}
	// Don't allow the event to popagate.
	return false;
}

static b8 mode_hf_terrain_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("HF Terrain mode button clicked.");
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	if (edit_state->mode != EDITOR_MODE_HF_TERRAIN) {
		editor_set_mode(edit_state, EDITOR_MODE_HF_TERRAIN);
	}
	// Don't allow the event to popagate.
	return false;
}

static b8 texture_browser_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Texture browser button clicked.");
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	b8 is_open = texture_browser_is_open(&edit_state->tex_browser);
	if (is_open) {
		texture_browser_close(&edit_state->tex_browser);
	} else {
		texture_browser_open_for_browsing(&edit_state->tex_browser);
	}

	// Don't allow the event to popagate.
	return false;
}

static void show_bvh_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT, event.checked);
}

static void show_grid_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT, event.checked);
}

static void show_debug_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_ENABLED_BIT, event.checked);
	kui_control_set_is_active(edit_state->kui_state, edit_state->view_bvh_checkbox, event.checked);
	kui_control_set_is_active(edit_state->kui_state, edit_state->view_grid_checkbox, event.checked);
}

static void scene_visibility_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	if (self.val == edit_state->view_skybox_checkbox.val) {
		kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_RENDER_SKYBOX_BIT, event.checked);
	} else if (self.val == edit_state->view_fog_checkbox.val) {
		kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_RENDER_FOG_BIT, event.checked);
	} else {
		KFATAL("Something is hooked to this that shouldn't be.");
	}
}

static b8 editor_on_mouse_move (u16 code, void *sender, void *listener_inst, event_context context) {
	editor_state *state = (editor_state *)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	if (state->mode == EDITOR_MODE_ENTITY) {
		if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(MOUSE_BUTTON_LEFT)) {
			b8 has_selection = state->selection_list && darray_length(state->selection_list);
			if (has_selection) {
				i16 x = context.data.i16[0];
				i16 y = context.data.i16[1];

				mat4 view = kcamera_get_view(state->editor_camera);
				vec3 origin = kcamera_get_position(state->editor_camera);
				rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
				mat4 projection = kcamera_get_projection(state->editor_camera);

				ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

				editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER);
			}
		}
	} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
		state->debug_point_count = 0;
		hf_terrain *terrain = kscene_hf_terrain_get(state->edit_scene);

		i16 x = context.data.i16[0];
		i16 y = context.data.i16[1];

		mat4 view = kcamera_get_view(state->editor_camera);
		vec3 origin = kcamera_get_position(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		mat4 projection = kcamera_get_projection(state->editor_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);
		KTRACE("r direction = %V3.3f", &r.direction);

		hf_block block;
		hf_chunk chunk;
		vec3 pos;
		vec3 normal;
		if (kscene_hf_terrain_raycast(state->edit_scene, &r, true, &block, &chunk, &pos, &normal)) {
			// TODO: move some sort of indicator over terrain to show what will be interacted with on mouse down/drag.

			// Vary action based on selected hf terrain sub-editor mode (i.e. paint vs elevation change).
			switch (state->hft_edit_mode) {
			case HF_TERRAIN_EDIT_MODE_GENERAL:
				break;
			case HF_TERRAIN_EDIT_MODE_PAINT:
				break;
			case HF_TERRAIN_EDIT_MODE_ELEVATION: {
				// Operate on the closest vertex.
				i64 index = -1;
				u32 tx = 0;
				u32 tz = 0;
				hf_vertex_3d *v = hf_terrain_chunk_get_closest_vertex(terrain, &block, &chunk, pos, &tx, &tz, &index);

				// HACK: Just the first one for now, add to this once there diameter is working for this.
				state->debug_point_count = 1;
				state->debug_points[0].position = v->position;
				state->debug_points[0].colour = vec4_create(1.0f, 0.0f, 0.0f, 1.0f);
			} break;
			case HF_TERRAIN_EDIT_MODE_CHUNK:
				break;
			case HF_TERRAIN_EDIT_MODE_REMOVE:
				break;
			default:
				KFATAL("Hwhat");
			}
		}
	}

	return false; // Allow other event handlers to process this event.
}

static void hf_terrain_paint (editor_state *state, vec3 pos, vec3 normal, const hf_block *block, const hf_chunk *chunk) {
	f32 local_x = pos.x - block->aabb.min.x;
	f32 local_z = pos.z - block->aabb.min.z;
	f32 u = local_x / HF_BLOCK_SIZE_WORLD;
	f32 v = local_z / HF_BLOCK_SIZE_WORLD;

	u *= HF_QUAD_SCALE;
	v *= HF_QUAD_SCALE;

	if (u >= HF_TERRAIN_SPLATMAP_RESOLUTION || v >= HF_TERRAIN_SPLATMAP_RESOLUTION) {
		return;
	}

	u32 brush_diameter = state->hft_paint_brush_diameter;
	i8 brush_strength = state->hft_paint_brush_strength;
	u8 material_index = state->hft_paint_material_index;

	i32 radius_texels = brush_diameter * 0.5f;

	i32 absolute_min_x = kfloor(u - radius_texels);
	i32 absolute_min_z = kfloor(v - radius_texels);
	i32 absolute_max_x = kceil(u + radius_texels);
	i32 absolute_max_z = kceil(v + radius_texels);

	i32 min_x = KMAX(absolute_min_x, 0);
	i32 min_z = KMAX(absolute_min_z, 0);
	i32 max_x = KMIN(absolute_max_x, HF_TERRAIN_SPLATMAP_RESOLUTION);
	i32 max_z = KMIN(absolute_max_z, HF_TERRAIN_SPLATMAP_RESOLUTION);

	// TODO: Check if there is overflow into a neighboring block.
	// If there is, these unclipped coords will need to be carried over to them as well,
	// translated to match thier splatmap's coords.

	u32 width = KMAX(max_x - min_x, 1);
	u32 height = KMAX(max_z - min_z, 1);

	// NOTE: the brush center should be based on the texel position, not the center of the area being painted.
	vec2 brush_center = vec2_create(absolute_min_x + radius_texels, absolute_min_z + radius_texels);
	f32 strength = brush_strength / 255.0f;

	// FIXME: Maybe have some sort of buffer that sticks around for this...
	u32 total_px = width * height;
	u8 *new_colour = KALLOC_TYPE_CARRAY(u8, total_px * 4);
	for (u32 z = 0; z < height; ++z) {
		for (u32 x = 0; x < width; ++x) {
			vec2 pos = vec2_create(min_x + x, min_z + z);
			f32 distance = vec2_distance(pos, brush_center);
			f32 weight = kfalloff_smooth(distance / radius_texels);
			u32 new_colour_base = ((z * width + x) * 4);
			u32 cur_colour_base = (((min_z + z) * HF_TERRAIN_SPLATMAP_RESOLUTION + (min_x + x)) * 4);

			f32 delta = weight * strength;
			f32 weights[4];
			for (u8 c = 0; c < 4; ++c) {
				weights[c] = block->splatmap_pixels[cur_colour_base + c] / 255.0f;
			}
			weights[material_index] += delta;

			// Calculate overflow between all channels and reduce the others if needed.
			f32 sum = 0.0f;
			for (u8 c = 0; c < 4; ++c) {
				sum += weights[c];
			}

			f32 overflow = sum - 1.0f;
			if (overflow > 0.0f) {
				f32 reducible = 0.0f;

				for (u8 c = 0; c < 4; ++c) {
					if (c == material_index) {
						continue;
					}
					reducible += weights[c];
				}

				if (reducible <= 0.0f) {
					// Nothing to reduce. Clamp?
					weights[material_index] -= overflow;
				} else {
					f32 scale = overflow / reducible;
					for (u8 c = 0; c < 4; ++c) {
						if (c == material_index) {
							continue;
						}

						f32 reduction = weights[c] * scale; // * (1.0f - weight);
						weights[c] -= reduction;
					}
				}
			}

			for (u8 c = 0; c < 4; ++c) {
				u8 px_channel = (u8)KCLAMP(weights[c] * 255, 0, 255);
				block->splatmap_pixels[cur_colour_base + c] = px_channel;
				new_colour[new_colour_base + c] = px_channel;
			}
		}
	}

	texture_write_data(block->splatmap, 32, min_x, min_z, 0, width, height, new_colour, false);

	kfree(new_colour);
}

static void hf_terrain_adjust_vertex_at (hf_terrain *terrain, u32 index, f32 amount, b8 set_height) {
	if (set_height) {
		terrain->vertices[index].position.y = amount;
	} else {
		terrain->vertices[index].position.y += amount;
	}
}

static hf_chunk *hf_terrain_get_next_vertex_index (const hf_terrain *terrain, const hf_block *block, const hf_chunk *chunk, u32 x, u32 z, i8 rel_x, i8 rel_z, i64 *out_index) {
	i16 block_x = block->x;
	i16 block_z = block->z;
	i16 chunk_x = chunk->x;
	i16 chunk_z = chunk->z;

	i16 next_x = x + rel_x;
	i16 next_z = z + rel_z;

	// Crosses a chunk border.
	if (next_x > HF_CHUNK_QUAD_COUNT) {
		chunk_x++;
		next_x -= HF_VERTEX_STRIDE;
		// Check if crossing a block border
		if (chunk_x >= HF_BLOCK_CHUNK_DIM) {
			block_x++;
			chunk_x = 0;
			if (block_x >= terrain->block_count_x) {
				// This would proceed beyond the furthest border. Boot.
				*out_index = -1;
				return KNULL;
			}
		}
	} else if (next_x < 0) {
		chunk_x--;
		next_x += HF_VERTEX_STRIDE;
		// Check if crossing a block border
		if (chunk_x < 0) {
			block_x--;
			chunk_x = HF_BLOCK_CHUNK_DIM - 1;
			if (block_x < 0) {
				// This would proceed beyond the furthest border. Boot.
				*out_index = -1;
				return KNULL;
			}
		}
	}

	// Crosses a chunk border.
	if (next_z > HF_CHUNK_QUAD_COUNT) {
		chunk_z++;
		next_z -= HF_VERTEX_STRIDE;
		// Check if crossing a block border
		if (chunk_z >= HF_BLOCK_CHUNK_DIM) {
			block_z++;
			chunk_z = 0;
			if (block_z >= terrain->block_count_z) {
				// This would proceed beyond the furthest border. Boot.
				*out_index = -1;
				return KNULL;
			}
		}
	} else if (next_z < 0) {
		chunk_z--;
		next_z += HF_VERTEX_STRIDE;
		// Check if crossing a block border
		if (chunk_z < 0) {
			block_z--;
			chunk_z = HF_BLOCK_CHUNK_DIM - 1;
			if (block_z < 0) {
				// This would proceed beyond the furthest border. Boot.
				*out_index = -1;
				return KNULL;
			}
		}
	}

	hf_block *new_block = hf_terrain_get_block_at(terrain, block_x, block_z);
	hf_chunk *new_chunk = hf_terrain_block_get_chunk_at(new_block, chunk_x, chunk_z);

	*out_index = hf_terrain_chunk_get_vert_index_at(new_chunk, next_x, next_z);
	return new_chunk;
}

static void hf_terrain_recalc_and_update_verts (hf_terrain *terrain, hf_chunk **p_chunks, u8 count) {

	/* hf_terrain_recalculate_vertices(terrain); */

	struct renderer_system_state *renderer_state = engine_systems_get()->renderer_system;
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_state, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

	u32 size = sizeof(hf_vertex_3d) * HF_CHUNK_VERTEX_COUNT;
	for (u8 i = 0; i < count; ++i) {
		hf_terrain_chunk_recalculate_vertices(terrain, p_chunks[i]);
		if (!renderer_renderbuffer_load_range(
				renderer_state,
				vertex_buffer,
				p_chunks[i]->vertex_buffer_offset,
				size,
				terrain->vertices + p_chunks[i]->vertex_offset,
				false)) {
			KERROR("HF terrain vert updates Failed!");
		}
	}
}

static b8 update_list_includes (
	hf_chunk *chunk,
	u8 chunk_update_count,
	hf_chunk *update_list[128]) {
	for (u8 i = 0; i < chunk_update_count; ++i) {
		if (update_list[i] == chunk) {
			return true;
		}
	}
	return false;
}

static void adjust_terrain_vertex_at (
	editor_state *state,
	hf_terrain *terrain,
	hf_block *block,
	hf_chunk *chunk,
	i32 center_x,
	i32 center_z,
	i64 index,
	f32 weighted_amount,
	u8 *chunk_update_count,
	hf_chunk *update_list[128]) {

	hf_terrain_adjust_vertex_at(terrain, index, weighted_amount, state->hft_elevation_set_height);
	if (!update_list_includes(chunk, *chunk_update_count, update_list)) {
		update_list[*chunk_update_count] = chunk;
		(*chunk_update_count)++;
	}
	// Check if the vertex is shared with the next chunk on the X axis.
	{
		hf_chunk *new_chunk = chunk;
		index = -1;
		if (center_x == HF_CHUNK_QUAD_COUNT) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, 1, 0, &index);
		} else if (center_x == 0) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, -1, 0, &index);
		}

		if (index >= 0) {
			hf_terrain_adjust_vertex_at(terrain, index, weighted_amount, state->hft_elevation_set_height);
			if (new_chunk != chunk && !update_list_includes(new_chunk, *chunk_update_count, update_list)) {
				update_list[*chunk_update_count] = new_chunk;
				(*chunk_update_count)++;
			}
		}
	}

	// Check if the vertex is shared with the next chunk on the Z axis.
	{
		hf_chunk *new_chunk = chunk;
		index = -1;
		if (center_z == HF_CHUNK_QUAD_COUNT) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, 0, 1, &index);
		} else if (center_z == 0) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, 0, -1, &index);
		}

		if (index >= 0) {
			hf_terrain_adjust_vertex_at(terrain, index, weighted_amount, state->hft_elevation_set_height);
			if (new_chunk != chunk && !update_list_includes(new_chunk, *chunk_update_count, update_list)) {
				update_list[*chunk_update_count] = new_chunk;
				(*chunk_update_count)++;
			}
		}
	}

	// If in a corner, also need to check if there's a diagonally-opposite shared vertex.
	{
		hf_chunk *new_chunk = chunk;
		index = -1;
		// All intercardinal directions need checking.
		if (center_x == HF_CHUNK_QUAD_COUNT && center_z == HF_CHUNK_QUAD_COUNT) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, 1, 1, &index);
		} else if (center_x == HF_CHUNK_QUAD_COUNT && center_z == 0) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, 1, -1, &index);
		} else if (center_x == 0 && center_z == HF_CHUNK_QUAD_COUNT) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, -1, 1, &index);
		} else if (center_x == 0 && center_z == 0) {
			new_chunk = hf_terrain_get_next_vertex_index(terrain, block, chunk, center_x, center_z, -1, -1, &index);
		}

		if (index >= 0) {
			hf_terrain_adjust_vertex_at(terrain, index, weighted_amount, state->hft_elevation_set_height);
			if (new_chunk != chunk && !update_list_includes(new_chunk, *chunk_update_count, update_list)) {
				update_list[*chunk_update_count] = new_chunk;
				(*chunk_update_count)++;
			}
		}
	}
}

static void hf_terrain_do_elevation (
	editor_state *state,
	vec3 pos,
	vec3 normal,
	hf_block *block,
	hf_chunk *chunk,
	i64 index,
	u32 center_x,
	u32 center_z,
	hf_vertex_3d *closest_vertex,
	f32 mod) {

	hf_terrain *terrain = kscene_hf_terrain_get(state->edit_scene);

	/* KINFO("%s - pos=%V3.3", __FUNCTION__, &pos); */

	// Ensure direction first.
	f32 center_amount = state->hft_elevation_amount * mod;

	u8 chunk_update_count = 0;
	hf_chunk *update_list[128];

	if (state->hft_elevation_diameter == 1) {
		f32 weighted_amount = center_amount * 1.0f;
		adjust_terrain_vertex_at(state, terrain, block, chunk, center_x, center_z, index, weighted_amount, &chunk_update_count, update_list);
	} else {

		// Work in a grid that's the size of the diameter.
		for (u8 z = 0; z < state->hft_elevation_diameter; ++z) {
			for (u8 x = 0; x < state->hft_elevation_diameter; ++x) {
				hf_block *b = block;
				hf_chunk *c = chunk;
				i32 block_x = b->x;
				i32 block_z = b->z;

				i32 chunk_x = c->x;
				i32 chunk_z = c->z;
				i16 xpos = kfloor((center_x - state->hft_elevation_diameter * 0.5f) + x);
				i16 zpos = kfloor((center_z - state->hft_elevation_diameter * 0.5f) + z);

				f32 distance = vec2_distance((vec2){xpos, zpos}, (vec2){center_x, center_z});
				f32 weight = kfalloff_smooth(distance / state->hft_elevation_diameter);

				// Move along to the next chunk (or block) if need be along X.
				if (xpos < 0) {
					// move a chunk back along x
					chunk_x--;
					if (chunk_x < 0) {
						// Move back a block, to the last chunk.
						block_x--;
						if (block_x < 0) {
							// Exceeded bounds, skip.
							continue;
						}
						chunk_x += HF_BLOCK_CHUNK_DIM;
					}
					xpos += HF_CHUNK_QUAD_COUNT; // vertex stride?
				} else if (xpos >= HF_CHUNK_QUAD_COUNT) {
					// Move a chunk forward along x.
					chunk_x++;
					if (chunk_x >= HF_BLOCK_CHUNK_DIM) {
						// Move forward a block;
						block_x++;
						if (block_x >= terrain->block_count_x) {
							// Exceeded bounds, skip.
							continue;
						}
						chunk_x -= HF_BLOCK_CHUNK_DIM;
					}
					xpos -= HF_CHUNK_QUAD_COUNT; // HF_VERTEX_STRIDE;
				}

				// Move along to the next chunk (or block) if need be along Z.
				if (zpos < 0) {
					// move a chunk back along z
					chunk_z--;
					if (chunk_z < 0) {
						// Move back a block, to the last chunk.
						block_z--;
						if (block_z < 0) {
							// Exceeded bounds, skip.
							continue;
						}
						chunk_z += HF_BLOCK_CHUNK_DIM;
					}
					zpos += HF_CHUNK_QUAD_COUNT; // HF_VERTEX_STRIDE;
				} else if (zpos >= HF_CHUNK_QUAD_COUNT) {
					// Move a chunk forward along z.
					chunk_z++;
					if (chunk_z >= HF_BLOCK_CHUNK_DIM) {
						// Move forward a block;
						block_z++;
						if (block_z >= terrain->block_count_z) {
							// Exceeded bounds, skip.
							continue;
						}
						chunk_z -= HF_BLOCK_CHUNK_DIM;
					}
					zpos -= HF_CHUNK_QUAD_COUNT; // HF_VERTEX_STRIDE;
				}

				// Ensure the correct block and chunk are selected.
				b = hf_terrain_get_block_at(terrain, block_x, block_z);
				c = hf_terrain_block_get_chunk_at(b, chunk_x, chunk_z);

				i32 idx = hf_terrain_chunk_get_vert_index_at(c, xpos, zpos);
				if (idx >= 0) {
					f32 weighted_amount = center_amount * weight;
					adjust_terrain_vertex_at(state, terrain, b, c, xpos, zpos, idx, weighted_amount, &chunk_update_count, update_list);
				}
			}
		}
	}

	// TODO:
	hf_terrain_recalc_and_update_verts(terrain, update_list, chunk_update_count);

	// FIXME: Should only do this on blocks containing updates.
	u32 block_count = terrain->block_count_x * terrain->block_count_z;
	for (u32 i = 0; i < block_count; ++i) {
		for (u32 c = 0; c < HF_BLOCK_CHUNK_COUNT; ++c) {
			terrain->blocks[i].aabb = extents_combine(terrain->blocks[i].aabb, terrain->blocks[i].chunks[c].aabb);
		}
	}
}

static b8 editor_on_drag (u16 code, void *sender, void *listener_inst, event_context context) {
	editor_state *state = (editor_state *)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	i16 x = context.data.i16[0];
	i16 y = context.data.i16[1];
	u16 drag_button = context.data.u16[4];

	// Only care about left button drags.
	if (drag_button == MOUSE_BUTTON_LEFT) {
		mat4 view = kcamera_get_view(state->editor_camera);
		vec3 origin = kcamera_get_position(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		mat4 projection = kcamera_get_projection(state->editor_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

		if (state->mode == EDITOR_MODE_ENTITY) {
			if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
				state->using_gizmo = true;
				// Drag start -- change the interaction mode to "dragging".
				editor_gizmo_interaction_begin(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
			} else if (code == EVENT_CODE_MOUSE_DRAGGED) {
				editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
			} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
				editor_gizmo_interaction_end(&state->gizmo);
				state->using_gizmo = false;
			}

			// Update inspector position controls.
			{
				vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
				kui_textbox_f32_set(state->kui_state, state->entity_position_x_textbox, position.x);
				kui_textbox_f32_set(state->kui_state, state->entity_position_y_textbox, position.y);
				kui_textbox_f32_set(state->kui_state, state->entity_position_z_textbox, position.z);
			}

			// Update inspector orientation controls.
			{
				quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
				kui_textbox_f32_set(state->kui_state, state->entity_orientation_x_textbox, rotation.x);
				kui_textbox_f32_set(state->kui_state, state->entity_orientation_y_textbox, rotation.y);
				kui_textbox_f32_set(state->kui_state, state->entity_orientation_z_textbox, rotation.z);
				kui_textbox_f32_set(state->kui_state, state->entity_orientation_w_textbox, rotation.w);
			}

			// Update inspector scale controls.
			{
				vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
				kui_textbox_f32_set(state->kui_state, state->entity_scale_x_textbox, scale.x);
				kui_textbox_f32_set(state->kui_state, state->entity_scale_y_textbox, scale.y);
				kui_textbox_f32_set(state->kui_state, state->entity_scale_z_textbox, scale.z);
			}
		} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
			hf_terrain *terrain = kscene_hf_terrain_get(state->edit_scene);

			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];
			i16 delta_y = context.data.i16[3];

			mat4 view = kcamera_get_view(state->editor_camera);
			vec3 origin = kcamera_get_position(state->editor_camera);
			rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
			mat4 projection = kcamera_get_projection(state->editor_camera);

			ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

			hf_block block;
			hf_chunk chunk;
			vec3 pos;
			vec3 normal;
			if (kscene_hf_terrain_raycast(state->edit_scene, &r, true, &block, &chunk, &pos, &normal)) {

				// Vary action based on selected hf terrain sub-editor mode (i.e. paint vs elevation change).
				switch (state->hft_edit_mode) {
				case HF_TERRAIN_EDIT_MODE_GENERAL:
					break;
				case HF_TERRAIN_EDIT_MODE_PAINT:
					hf_terrain_paint(state, pos, normal, &block, &chunk);
					break;
				case HF_TERRAIN_EDIT_MODE_ELEVATION: {

					// Operate on the closest vertex.
					i64 index = -1;
					u32 tx = 0;
					u32 tz = 0;
					hf_vertex_3d *v = hf_terrain_chunk_get_closest_vertex(terrain, &block, &chunk, pos, &tx, &tz, &index);

					hf_terrain_do_elevation(state, pos, normal, &block, &chunk, index, tx, tz, v, delta_y > 0 ? -1 : 1);
					// HACK: Just the first one for now, add to this once there diameter is working for this.
					state->debug_point_count = 1;
					state->debug_points[0].position = v->position;
					state->debug_points[0].colour = vec4_create(1.0f, 0.0f, 0.0f, 1.0f);
				} break;
				case HF_TERRAIN_EDIT_MODE_CHUNK:
					// NOTE: This mode does nothing on drag.
					break;
				case HF_TERRAIN_EDIT_MODE_REMOVE:
					break;
				default:
					KFATAL("Hwhat");
				}
			}
		}
	}

	return false; // Let other handlers handle.
}

i32 raycast_hit_kquicksort_compare (void *a, void *b) {
	raycast_hit *a_typed = a;
	raycast_hit *b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return -1;
	} else if (a_typed->distance < b_typed->distance) {
		return 1;
	}
	return 0;
}
i32 raycast_hit_kquicksort_compare_desc (void *a, void *b) {
	raycast_hit *a_typed = a;
	raycast_hit *b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return 1;
	} else if (a_typed->distance < b_typed->distance) {
		return -1;
	}
	return 0;
}

/* KAPI void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn); */

static b8 editor_on_button (u16 code, void *sender, void *listener_inst, event_context context) {
	if (code == EVENT_CODE_BUTTON_PRESSED) {

		u16 button = context.data.u16[4];
		switch (button) {
		case MOUSE_BUTTON_LEFT: {
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];
			editor_state *state = (editor_state *)listener_inst;

			if (state->edit_scene) {
				kscene_state scene_state = kscene_state_get(state->edit_scene);
				if (scene_state == KSCENE_STATE_LOADED) {

					if (state->mode == EDITOR_MODE_HF_TERRAIN) {
						if (state->hft_edit_mode == HF_TERRAIN_EDIT_MODE_CHUNK) {

							mat4 view = kcamera_get_view(state->editor_camera);
							mat4 projection = kcamera_get_projection(state->editor_camera);
							vec3 origin = kcamera_get_position(state->editor_camera);
							rect_2di current_vp_rect = kcamera_get_vp_rect(state->editor_camera);

							if (point_in_rect_2di((vec2i){x, y}, current_vp_rect)) {
								ray r = ray_from_screen((vec2i){x, y}, current_vp_rect, origin, view, projection);
								r.max_distance = 2000.0f;
								// Ignore collisions occurring where the ray's origin is inside a BVH node.
								FLAG_SET(r.flags, RAY_FLAG_IGNORE_IF_INSIDE_BIT, true);

								hf_block block;
								hf_chunk chunk;
								vec3 pos;
								vec3 normal;
								if (kscene_hf_terrain_raycast(state->edit_scene, &r, true, &block, &chunk, &pos, &normal)) {

									hf_terrain *t = kscene_hf_terrain_get(state->edit_scene);
									hf_block *p_block = hf_terrain_get_block_at(t, block.x, block.z);
									hf_chunk *p_chunk = hf_terrain_block_get_chunk_at(p_block, chunk.x, chunk.z);
									state->selected_chunk = p_chunk;

									// Update the selected chunk visual debug box.
									geometry_recalculate_line_box3d_by_extents(&state->hft_selected_chunk_debug_box, p_chunk->aabb, vec3_zero());
									renderer_geometry_vertex_update(&state->hft_selected_chunk_debug_box, 0, state->hft_selected_chunk_debug_box.vertex_count, state->hft_selected_chunk_debug_box.vertices, false);

									// Set the selected chunk and update the UI controls.
									for (u8 i = 0; i < 5; ++i) {
										/* KTRACE("Chunk [%u, %u], Material slot %u is using index %u", chunk.x, chunk.z, i, chunk.material_indices[i]); */
										kui_textbox_i64_set(state->kui_state, state->hft_chunk_material_textboxes[i], chunk.material_indices[i]);
									}
								}
							}
						}
					}
				}
			}
		}
		}
		//
	} else if (code == EVENT_CODE_BUTTON_RELEASED) {
		u16 button = context.data.u16[4];
		switch (button) {
		case MOUSE_BUTTON_LEFT: {
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];
			editor_state *state = (editor_state *)listener_inst;

			if (state->edit_scene) {

				if (state->using_gizmo) {
					return false;
				}
				kscene_state scene_state = kscene_state_get(state->edit_scene);
				if (scene_state == KSCENE_STATE_LOADED) {
					mat4 view = kcamera_get_view(state->editor_camera);
					mat4 projection = kcamera_get_projection(state->editor_camera);
					vec3 origin = kcamera_get_position(state->editor_camera);
					rect_2di current_vp_rect = kcamera_get_vp_rect(state->editor_camera);

					// Multi-select
					b8 multiselect = (input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL));

					struct kscene *current_scene = state->edit_scene;
					// Cast a ray into the scene and see if anything can be selected.
					if (point_in_rect_2di((vec2i){x, y}, current_vp_rect)) {
						ray r = ray_from_screen((vec2i){x, y}, current_vp_rect, origin, view, projection);
						r.max_distance = 2000.0f;
						// Ignore collisions occurring where the ray's origin is inside a BVH node.
						FLAG_SET(r.flags, RAY_FLAG_IGNORE_IF_INSIDE_BIT, true);
						raycast_result result = {0};

						if (state->mode == EDITOR_MODE_ENTITY) {
							// Only allow selection in entity mode.
							if (kscene_raycast(current_scene, &r, &result)) {

								u32 hit_count = result.hits ? darray_length(result.hits) : 0;
								if (!hit_count) {
									KINFO("Nothing hit from raycast.");
									editor_clear_selected_entities(state);
								} else {
									if (!multiselect) {
										KINFO("Not multiselecting, clearing selection...");
										editor_clear_selected_entities(state);
									}

									// Sort hits by distance.
									kquick_sort(sizeof(raycast_hit), result.hits, 0, hit_count - 1, raycast_hit_kquicksort_compare);

									for (u32 i = 0; i < hit_count; ++i) {
										// Each thing. Use this to make selections, etc.
										raycast_hit *hit = &result.hits[i];

										kentity entity = (kentity)hit->user;

										// Skip BVH-only hits.
										if (hit->type == RAYCAST_HIT_TYPE_BVH_AABB) {
											KTRACE("Skipping BVH AABB hit (name='%k')", kscene_get_entity_name(state->edit_scene, entity));
											continue;
										}

										// Add to selection.
										editor_add_to_selected_entities(state, 1, &entity);
										// NOTE: only taking the first thing from the list.
										break;
									}
								}
							}
						} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
							// TODO: raycast to terrain, return point and normal. Perhaps block/chunk data too?
						}
					}
				}
			}
		} break;
		}
	}

	// Allow other handlers to process the event.
	return false;
}

static void scene_name_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_name(editor->edit_scene, entry_control_text);
			}
		}
	}
}

static void scene_fog_colour_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		kui_control next_control = INVALID_KUI_CONTROL;
		u8 element_index = 0;
		if (self.val == editor->scene_fog_colour_r_textbox.val) {
			element_index = 0;
			next_control = editor->scene_fog_colour_g_textbox;
		} else if (self.val == editor->scene_fog_colour_g_textbox.val) {
			element_index = 1;
			next_control = editor->scene_fog_colour_b_textbox;
		} else if (self.val == editor->scene_fog_colour_b_textbox.val) {
			element_index = 2;
			next_control = editor->scene_fog_colour_a_textbox;
		} else if (self.val == editor->scene_fog_colour_a_textbox.val) {
			element_index = 3;
			next_control = editor->scene_fog_colour_r_textbox;
		} else {
			KFATAL("Invalid control passed");
		}

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				colour4 fog_colour = kscene_get_fog_colour(editor->edit_scene);
				const char *val = kui_textbox_text_get(state, self);
				f32 parsed;
				if (string_to_f32(val, &parsed)) {
					fog_colour.elements[element_index] = parsed;
					kscene_set_fog_colour(editor->edit_scene, fog_colour);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, next_control);
		}
	}
}

static void entity_name_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_entity_name(editor->edit_scene, editor->selection_list[0], kname_create(entry_control_text));
			}
		}
	}
}

static void entity_position_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					position.x = x;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_y_textbox);
		}
	}
}
static void entity_position_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					position.y = y;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_z_textbox);
		}
	}
}
static void entity_position_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					position.z = z;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_x_textbox);
		}
	}
}

static void entity_orientation_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					rotation.x = x;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_y_textbox);
		}
	}
}
static void entity_orientation_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					rotation.y = y;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_z_textbox);
		}
	}
}

static void entity_orientation_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					rotation.z = z;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_w_textbox);
		}
	}
}

static void entity_orientation_w_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 w;
				if (string_to_f32(val, &w)) {
					rotation.w = w;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_x_textbox);
		}
	}
}

static void entity_scale_x_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					scale.x = x;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_y_textbox);
		}
	}
}
static void entity_scale_y_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					scale.y = y;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_z_textbox);
		}
	}
}
static void entity_scale_z_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char *val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					scale.z = z;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_x_textbox);
		}
	}
}

// TODO: all this stuff should exist in a kui_tree_control.
struct tree_hierarchy;
// An individual node within the hierarchy tree.
typedef struct tree_hierarchy_node {

	b8 expanded;

	// Pointer back to the tree.
	struct tree_hierarchy *tree;

	// user context for the node.
	u32 user_data_size;
	void *user_data;

	// A handle to the control associated with this item.
	kui_control tree_item;

	// Pointer to the parent.
	struct tree_hierarchy_node *parent;

	// Child nodes.
	u32 child_count;
	struct tree_hierarchy_node *children;
} tree_hierarchy_node;

// Top-level representation of the tree hierarchy.
typedef struct tree_hierarchy {
	// user context for the entire tree.
	u32 user_data_size;
	void *user_data;

	u32 root_count;
	struct tree_hierarchy_node *root_nodes;
} tree_hierarchy;

typedef struct hierarchy_node_context {
	editor_state *editor;
	kentity entity;
	tree_hierarchy_node *hierarchy_node;
} hierarchy_node_context;

static tree_hierarchy tree;

static void tree_node_cleanup_r (tree_hierarchy_node *node) {
	for (u32 i = 0; i < node->child_count; ++i) {
		tree_node_cleanup_r(&node->children[i]);
	}

	if (node->child_count && node->children) {
		kfree(node->children);
	}
}

static void tree_setup_node_r (editor_state *state, kscene_hierarchy_node *scene_node, tree_hierarchy_node *tree_node, tree_hierarchy_node *parent_node, u32 index, f32 *y_offset) {
	kui_state *kui_state = state->kui_state;

	kname name = kscene_get_entity_name(state->edit_scene, scene_node->entity);

	tree_node->child_count = scene_node->child_count;
	if (tree_node->child_count) {
		tree_node->children = KALLOC_TYPE_CARRAY(tree_hierarchy_node, tree_node->child_count);
	}

	const char *tree_item_name = string_format("tree_item_%i", index);

	tree_node->tree_item = kui_tree_item_control_create(
		kui_state,
		tree_item_name,
		state->tree_inspector_width - 10,
		FONT_TYPE_SYSTEM,
		state->font_name,
		state->font_size,
		kname_string_get(name),
		tree_node->child_count > 0);

	string_free(tree_item_name);

	if (parent_node) {
		/* kui_tree_item_control_add_child_tree_item(kui_state, parent_node->tree_item, tree_node); */
		kui_base_control *parent_base = kui_system_get_base(state->kui_state, parent_node->tree_item);
		kui_tree_item_control *typed_parent_control = (kui_tree_item_control *)parent_base;
		KASSERT(kui_system_control_add_child(kui_state, typed_parent_control->child_container, tree_node->tree_item));
	} else {
		// Add to the content container of the scrollable control.
		KASSERT(kui_system_control_add_child(kui_state, state->tree_content_container, tree_node->tree_item));
	}

	*y_offset += KUI_TREE_ITEM_HEIGHT;

	hierarchy_node_context *context = KALLOC_TYPE(hierarchy_node_context, MEMORY_TAG_EDITOR);
	context->editor = state;
	context->entity = scene_node->entity;
	context->hierarchy_node = tree_node;

	kui_control_set_user_data(kui_state, tree_node->tree_item, sizeof(hierarchy_node_context), context, true, MEMORY_TAG_EDITOR);
	kui_control_set_on_click(kui_state, tree_node->tree_item, tree_item_clicked);
	kui_tree_item_set_on_expanded(kui_state, tree_node->tree_item, tree_item_expanded);
	kui_tree_item_set_on_collapsed(kui_state, tree_node->tree_item, tree_item_collapsed);

	// Recurse children.
	for (u32 i = 0; i < tree_node->child_count; ++i) {
		tree_setup_node_r(state, &scene_node->children[i], &tree_node->children[i], tree_node, index + 1, y_offset);
	}
}

static f32 refresh_tree_item_expansion_r (editor_state *state, tree_hierarchy_node *node, f32 y_offset) {

	f32 accumulated_y_offset = 0.0f;
	kui_control_position_set(state->kui_state, node->tree_item, (vec3){44, y_offset, 0});

	accumulated_y_offset += KUI_TREE_ITEM_HEIGHT;

	if (node->expanded && node->child_count && node->children) {
		for (u32 i = 0; i < node->child_count; ++i) {
			accumulated_y_offset += refresh_tree_item_expansion_r(state, &node->children[i], i * KUI_TREE_ITEM_HEIGHT);
		}
	}

	return accumulated_y_offset;
}

static void refresh_tree_expansion (editor_state *state, tree_hierarchy *tree) {
	f32 accumulated_height = 0.0f;
	for (u32 i = 0; i < tree->root_count; ++i) {
		accumulated_height += refresh_tree_item_expansion_r(state, &tree->root_nodes[i], accumulated_height);
	}

	kui_scrollable_set_content_size(state->kui_state, state->tree_scrollable_control, state->tree_inspector_width, accumulated_height);
}

static void tree_clear (editor_state *state) {
	// Destroy current tree.
	if (tree.root_count && tree.root_nodes) {
		// First, cleanup the nodes recursively.
		for (u32 i = 0; i < tree.root_count; ++i) {
			tree_hierarchy_node *node = &tree.root_nodes[i];
			tree_node_cleanup_r(node);
		}

		kfree(tree.root_nodes);
		tree.root_count = 0;
		tree.root_nodes = KNULL;

		kui_control_destroy_all_children(state->kui_state, state->tree_content_container);
	}
}

static void tree_refresh (editor_state *state) {
	KTRACE("Tree refresh starting.");
	if (state->edit_scene) {
		tree_clear(state);

		// Refresh the data.
		u32 node_count = 0;
		kscene_hierarchy_node *scene_nodes = kscene_get_hierarchy(state->edit_scene, &node_count);
		if (node_count && scene_nodes) {

			tree.root_count = node_count;
			tree.root_nodes = KALLOC_TYPE_CARRAY(tree_hierarchy_node, tree.root_count);

			// Create all the new tree items.
			f32 y_offset = 0.0f;
			for (u32 i = 0; i < node_count; ++i) {
				kscene_hierarchy_node *scene_node = &scene_nodes[i];

				tree_setup_node_r(state, scene_node, &tree.root_nodes[i], KNULL, i, &y_offset);
			}

			// Cleanup once done building
			kscene_cleanup_hierarchy(scene_nodes, node_count);
			node_count = 0;
			scene_nodes = KNULL;
		}

		refresh_tree_expansion(state, &tree);
	}

	KTRACE("Tree refresh complete.");
}

static b8 tree_item_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context *context = (hierarchy_node_context *)kui_control_get_user_data(state, self);

	editor_clear_selected_entities(context->editor);
	editor_add_to_selected_entities(context->editor, 1, &context->entity);

	return true;
}

static b8 tree_item_expanded (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context *context = (hierarchy_node_context *)kui_control_get_user_data(state, self);

	context->hierarchy_node->expanded = true;

	refresh_tree_expansion(context->editor, &tree);

	return true;
}

static b8 tree_item_collapsed (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context *context = (hierarchy_node_context *)kui_control_get_user_data(state, self);

	context->hierarchy_node->expanded = false;

	refresh_tree_expansion(context->editor, &tree);

	return true;
}

static void hft_elevation_diameter_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_elevation_diameter = KCLAMP((u32)x, 1, 64);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_elevation_amount_textbox);
		}
	}
}
static void hft_elevation_amount_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					editor->hft_elevation_amount = (f32)KCLAMP((f32)x, -127, 127);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_elevation_diameter_textbox);
		}
	}
}

static void hf_terrain_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	editor_state *edit_state = base->user_data;

	static kui_control checkboxes[HF_TERRAIN_EDIT_MODE_COUNT] = {0};
	checkboxes[HF_TERRAIN_EDIT_MODE_GENERAL] = edit_state->hft_mode_general_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_PAINT] = edit_state->hft_mode_paint_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_ELEVATION] = edit_state->hft_mode_elevation_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_CHUNK] = edit_state->hft_mode_chunk_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_REMOVE] = edit_state->hft_mode_remove_checkbox;

	static kui_control content_controls[HF_TERRAIN_EDIT_MODE_COUNT] = {0};
	content_controls[HF_TERRAIN_EDIT_MODE_GENERAL] = edit_state->hft_mode_general_content;
	content_controls[HF_TERRAIN_EDIT_MODE_PAINT] = edit_state->hft_mode_paint_content;
	content_controls[HF_TERRAIN_EDIT_MODE_ELEVATION] = edit_state->hft_mode_elevation_content;
	content_controls[HF_TERRAIN_EDIT_MODE_CHUNK] = edit_state->hft_mode_chunk_content;
	content_controls[HF_TERRAIN_EDIT_MODE_REMOVE] = edit_state->hft_mode_remove_content;

	for (u8 i = 0; i < HF_TERRAIN_EDIT_MODE_COUNT; ++i) {
		if (self.val == checkboxes[i].val) {
			edit_state->hft_edit_mode = (hf_terrain_edit_mode)i;
			kui_control_set_is_active(state, content_controls[i], true);
			kui_control_set_is_visible(state, content_controls[i], true);
		} else {
			kui_checkbox_set_checked(state, checkboxes[i], false);
			kui_control_set_is_active(state, content_controls[i], false);
			kui_control_set_is_visible(state, content_controls[i], false);
		}
	}

	switch (edit_state->hft_edit_mode) {

	case HF_TERRAIN_EDIT_MODE_GENERAL:
		KTRACE("HF Terrain edit mode set to general.");
		break;
	case HF_TERRAIN_EDIT_MODE_PAINT:
		KTRACE("HF Terrain edit mode set to paint.");
		break;
	case HF_TERRAIN_EDIT_MODE_ELEVATION:
		KTRACE("HF Terrain edit mode set to elevation.");
		break;
	case HF_TERRAIN_EDIT_MODE_CHUNK:
		KTRACE("HF Terrain edit mode set to chunk.");
		break;
	case HF_TERRAIN_EDIT_MODE_REMOVE:
		KTRACE("HF Terrain edit mode set to remove.");
		break;
	case HF_TERRAIN_EDIT_MODE_COUNT:
		KERROR("Invalid HF terrain edit mode!");
		break;
	}
}

static void hf_terrain_erase_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	editor_state *editor = kui_control_get_user_data(state, self);

	const char *entry_control_text = kui_textbox_text_get(state, editor->hft_paint_brush_strength_textbox);
	u32 len = string_length(entry_control_text);
	if (len > 0) {
		const char *val = kui_textbox_text_get(state, editor->hft_paint_brush_strength_textbox);
		i64 x;
		if (string_to_i64(val, &x)) {
			editor->hft_paint_brush_strength = (i8)KCLAMP((i8)(x *= -1), -127, 127);
			kui_textbox_i64_set(state, editor->hft_paint_brush_strength_textbox, x);
		}
	}
}

static void hf_terrain_set_height_checkbox_check_changed (struct kui_state *state, kui_control self, struct kui_checkbox_event event) {
	editor_state *editor = kui_control_get_user_data(state, self);

	editor->hft_elevation_set_height = event.checked;
}

static void hft_chunk_material_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		hf_terrain_chunk_material_context *context = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					// Set the material index for the given chunk.
					// TODO: clamp/validate
					if (context->editor->selected_chunk) {
						context->editor->selected_chunk->material_indices[context->material_slot] = x;
					}
				}
			}
		}
		if (key_code == KEY_TAB) {
			// TODO: focus next control
			/* kui_system_focus_control(state, editor->hft_elevation_diameter_textbox); */
		}
	}
}

static b8 hft_save_button_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	editor_state *editor = kui_control_get_user_data(state, self);

	b8 result = kscene_hf_terrain_save(editor->edit_scene);
	KTRACE("HF Terrain save %s", result ? "success" : "failure");
	if (!result) {
		KERROR("Failed to save HF Terrain. See logs for details.");
	}

	return false;
}

typedef struct hft_tex_browser_context {
	editor_state *editor;
	kui_control image_box;
	u8 material_index;
	hf_terrain_material_map map;
} hft_tex_browser_context;

static void hft_tex_browser_selected_callback (ktexture texture, void *context) {
	hft_tex_browser_context *typed_context = context;

	kui_image_box_control_texture_set(typed_context->editor->kui_state, typed_context->image_box, texture);

	// Update the terrain material listing.

	hf_terrain *terrain = kscene_hf_terrain_get(typed_context->editor->edit_scene);
	hf_terrain_material_texture_set(terrain, typed_context->material_index, typed_context->map, texture);

	kfree(context);
}
static void hft_tex_browser_cancelled_callback (void *context) {
	/* hft_tex_browser_context* typed_context = context; */

	kfree(context);
}

static b8 hft_material_imagebox_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	hf_terrain_material_imagebox_context *ctrl_context = kui_control_get_user_data(state, self);
	editor_state *editor = ctrl_context->editor;
	// Open the texture browser in select mode, passing along a callback with enough context data to
	// set the texture of the provided imagebox to the selected texture _if_ a selection is made.
	hft_tex_browser_context *context = KALLOC_TYPE(hft_tex_browser_context, MEMORY_TAG_EDITOR);
	context->editor = editor;
	context->image_box = self;
	context->material_index = ctrl_context->material_index;
	context->map = ctrl_context->map;
	// TODO: pass stuct with "selection_criteria_info"
	texture_browser_open_for_selection(&editor->tex_browser, context, hft_tex_browser_selected_callback, hft_tex_browser_cancelled_callback);
	//
	// Note that a texture size matching the other terrain materials texture sizes is required.
	//
	// In the callback, the image will need to be set in the terrain material listing as well, and loaded into
	// the array texture on the appropriate layer.
	//
	// Ensure that the terrain painting mode editor gets the update.
	return false;
}

static void hft_paint_brush_diameter_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_brush_diameter = KCLAMP((u32)x, 1, 64);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_paint_brush_strength_textbox);
		}
	}
}
static void hft_paint_brush_strength_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_brush_strength = (i8)KCLAMP((i8)x, -127, 127);

					// Update the checkbox to be checked if negative.
					b8 checked = x < 0;
					kui_checkbox_set_checked(state, editor->hft_paint_brush_erase_checkbox, checked);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_paint_brush_diameter_textbox);
		}
	}
}
static void hft_paint_material_index_textbox_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state *editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char *val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_material_index = (u8)KCLAMP((u8)x, 0, HF_TERRAIN_CHUNK_MAX_MATERIALS - 2);
				}
			}
		}
	}
}
