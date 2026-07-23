#pragma once

#include "editor/texture_browser.h"
#include "kui_types.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "systems/kmatrix_system.h"
#include "systems/texture_system.h"
#include "world/heightfield_terrain.h"
#include <core/frame_data.h>
#include <core/keymap.h>
#include <kui_system.h>
#include <platform/platform.h>
#include <renderer/kforward_renderer.h>
#include <world/world_types.h>

#define EDITOR_AXIS_COLOUR_R \
	(colour4){1.0f, 0.5f, 0.5f, 1.0f}
#define EDITOR_AXIS_COLOUR_G \
	(colour4){0.5f, 1.0f, 0.5f, 1.0f}
#define EDITOR_AXIS_COLOUR_B \
	(colour4){0.5f, 0.5f, 1.0f, 1.0f}

#define EDITOR_HFT_PAINT_BRUSH_MAX_SIZE 64

#include "editor/editor_gizmo.h"

typedef struct keditor_gizmo_pass_render_data {
	mat4 projection;
	mat4 view;

	b8 visible;

	kdebug_geometry_render_data geometry;

	mat4 gizmo_transform;

	b8 do_pass;
} keditor_gizmo_pass_render_data;

typedef enum editor_mode {
	EDITOR_MODE_SCENE,
	EDITOR_MODE_ENTITY,
	EDITOR_MODE_TREE,
	EDITOR_MODE_HF_TERRAIN,
	EDITOR_MODE_ASSETS
} editor_mode;

typedef struct keditor_gizmo_pass_data {
	kshader gizmo_shader;
	u32 set0_instance_id;
} keditor_gizmo_pass_data;

typedef enum hf_terrain_edit_mode {
	HF_TERRAIN_EDIT_MODE_GENERAL,
	HF_TERRAIN_EDIT_MODE_PAINT,
	HF_TERRAIN_EDIT_MODE_ELEVATION,
	HF_TERRAIN_EDIT_MODE_CHUNK,
	HF_TERRAIN_EDIT_MODE_REMOVE,
	HF_TERRAIN_EDIT_MODE_COUNT
} hf_terrain_edit_mode;

typedef enum hf_terrain_elevation_edit_mode {
	HF_TERRAIN_ELEVATION_EDIT_MODE_ADJUST_HEIGHT,
	HF_TERRAIN_ELEVATION_EDIT_MODE_DECREASE_HEIGHT,
	HF_TERRAIN_ELEVATION_EDIT_MODE_INCREASE_HEIGHT,
	HF_TERRAIN_ELEVATION_EDIT_MODE_SET_HEIGHT,
	HF_TERRAIN_ELEVATION_EDIT_MODE_SMOOTH,
	HF_TERRAIN_ELEVATION_EDIT_MODE_COUNT
} hf_terrain_elevation_edit_mode;

// Editor actions reserve the action range from 0x1000-0x1FFF
typedef enum editor_action {
	EDITOR_ACTION_MIN = 0x1000,

	EDITOR_ACTION_MOVE_FORWARD = 0x1001,
	EDITOR_ACTION_MOVE_BACKWARD = 0x1002,
	EDITOR_ACTION_SPRINT_FORWARD = 0x1003,
	EDITOR_ACTION_MOVE_LEFT = 0x1004,
	EDITOR_ACTION_MOVE_RIGHT = 0x1005,
	EDITOR_ACTION_MOVE_UP = 0x1006,
	EDITOR_ACTION_MOVE_DOWN = 0x1007,
	EDITOR_ACTION_LOOK_LEFT = 0x1008,
	EDITOR_ACTION_LOOK_RIGHT = 0x1009,
	EDITOR_ACTION_LOOK_UP = 0x100A,
	EDITOR_ACTION_LOOK_DOWN = 0x100B,
	EDITOR_ACTION_ZOOM_EXTENTS = 0x100C,

	EDITOR_ACTION_RENDER_MODE_DEFAULT = 0x1010,
	EDITOR_ACTION_RENDER_MODE_LIGHTING = 0x1011,
	EDITOR_ACTION_RENDER_MODE_NORMALS = 0x1012,
	EDITOR_ACTION_RENDER_MODE_CASCADES = 0x1013,
	EDITOR_ACTION_RENDER_MODE_WIREFRAME = 0x1014,

	EDITOR_ACTION_GIZMO_MODE_NONE = 0x1200,
	EDITOR_ACTION_GIZMO_MODE_MOVE = 0x1201,
	EDITOR_ACTION_GIZMO_MODE_ROTATE = 0x1202,
	EDITOR_ACTION_GIZMO_MODE_SCALE = 0x1203,
	EDITOR_ACTION_GIZMO_TOGGLE_ORIENTATION = 0x1204,

	EDITOR_ACTION_SCENE_SAVE = 0x1300,

	EDITOR_ACTION_MAX = 0x1FFF,
} editor_action;

typedef struct editor_state {
	kname game_package_name;
	// Editor camera
	kcamera editor_camera;
	f32 editor_camera_forward_move_speed;
	f32 editor_camera_backward_move_speed;
	editor_gizmo gizmo;
	b8 using_gizmo;
	// Editor state
	// Darray of selected entities.
	kentity *selection_list;
	keymap editor_keymap;

	struct kmatrix_system_state *matrix_system;
	kmatrix_id default_identity_matrix_id;

	b8 is_running;

	// Pointer to the scene currently owned by the editor (NOT necessarily the scene owned by the game code currently!)
	struct kscene *edit_scene;
	kname scene_asset_name;
	kname scene_package_name;

	keditor_gizmo_pass_data editor_gizmo_pass;
	struct renderer_system_state *renderer;
	krenderbuffer standard_vertex_buffer;
	krenderbuffer index_buffer;

	keditor_gizmo_pass_render_data *editor_gizmo_render_data;

	// position and colour vertex data
	kshader colour_shader;
	// position only vertex data, colour applied via UBO
	kshader debug_shader;
	u8 debug_point_count;
	colour_vertex_3d debug_points[256];
	u64 debug_points_vertex_buffer_offset;

	editor_mode mode;

	u16 font_size;
	kname font_name;
	u16 textbox_font_size;
	kname textbox_font_name;

	// UI elements
	kui_state *kui_state;
	kui_control editor_root;

	// Main window
	kui_control main_bg_panel;
	kui_control save_button;
	kui_control mode_entity_button;
	kui_control mode_scene_button;
	kui_control mode_tree_button;
	kui_control mode_hf_terrain_button;
	kui_control texture_browser_button;
	kui_control view_label;
	kui_control view_debug_checkbox;
	kui_control view_bvh_checkbox;
	kui_control view_grid_checkbox;
	kui_control view_skybox_checkbox;
	kui_control view_fog_checkbox;

	// Scene Inspector window
	f32 scene_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 scene_inspector_right_col_x;
	kui_control scene_inspector_bg_panel;
	kui_control scene_inspector_title;
	kui_control scene_name_label;
	kui_control scene_name_textbox;
	kui_control scene_fog_colour_label;
	kui_control scene_fog_colour_r_textbox;
	kui_control scene_fog_colour_g_textbox;
	kui_control scene_fog_colour_b_textbox;
	kui_control scene_fog_colour_a_textbox;

	// Entity Inspector window
	f32 entity_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 entity_inspector_right_col_x;
	kui_control entity_inspector_bg_panel;
	kui_control entity_inspector_title;
	kui_control entity_name_label;
	kui_control entity_name_textbox;
	kui_control entity_position_label;
	kui_control entity_position_x_textbox;
	kui_control entity_position_y_textbox;
	kui_control entity_position_z_textbox;

	kui_control entity_orientation_label;
	kui_control entity_orientation_x_textbox;
	kui_control entity_orientation_y_textbox;
	kui_control entity_orientation_z_textbox;
	kui_control entity_orientation_w_textbox;

	kui_control entity_scale_label;
	kui_control entity_scale_x_textbox;
	kui_control entity_scale_y_textbox;
	kui_control entity_scale_z_textbox;

	// Tree window
	b8 trigger_tree_refresh;
	f32 tree_inspector_width;
	// Beginning position of the entity inspector right column.
	f32 tree_inspector_right_col_x;
	kui_control tree_inspector_bg_panel;
	kui_control tree_inspector_title;
	kui_control tree_scrollable_control;
	kui_control tree_content_container;

	// HF Terrain window
	hf_terrain_edit_mode hft_edit_mode;
	f32 hf_terrain_window_width;
	f32 hf_terrain_right_col_x;
	kui_control hf_terrain_bg_panel;
	kui_control hf_terrain_title;
	kui_control hf_terrain_save_button;
	kui_control hft_mode_general_checkbox;
	kui_control hft_mode_paint_checkbox;
	kui_control hft_mode_elevation_checkbox;
	kui_control hft_mode_chunk_checkbox;
	kui_control hft_mode_remove_checkbox;

	kui_control hft_mode_general_content;
	kui_control hft_general_scrollable_control;
	kui_control hft_general_content_container;
	kui_control hft_general_material_names[HF_TERRAIN_MAX_MATERIALS];
	kui_control hft_general_material_albedo_image_boxes[HF_TERRAIN_MAX_MATERIALS];
	kui_control hft_general_material_normal_image_boxes[HF_TERRAIN_MAX_MATERIALS];
	kui_control hft_general_material_mra_image_boxes[HF_TERRAIN_MAX_MATERIALS];

	kui_control hft_mode_paint_content;
	kui_control hft_paint_brush_diameter_textbox;
	kui_control hft_paint_brush_strength_textbox;
	kui_control hft_paint_brush_erase_checkbox;
	kui_control hft_paint_brush_material_index_textbox;
	kui_control hft_paint_brush_diameter_label;
	kui_control hft_paint_brush_strength_label;
	kui_control hft_paint_material_index_label;

	kui_control hft_mode_elevation_content;

	kui_control hft_mode_chunk_content;
	kui_control hft_chunk_material_labels[5];
	kui_control hft_chunk_material_textboxes[5];
	hf_chunk *selected_chunk;
	kgeometry hft_selected_chunk_debug_box;

	kui_control hft_mode_remove_content;

	// HF Paint state
	u32 hft_paint_brush_diameter;
	// Negative strength erases.
	i8 hft_paint_brush_strength;
	u8 hft_paint_material_index;

	// HF Elevation state
	// Elevation mod amount (negative is down, positive is up)
	u8 hft_elevation_diameter;
	f32 hft_elevation_amount;
	b8 hft_elevation_set_height;
	kui_control hft_elevation_diameter_textbox;
	kui_control hft_elevation_amount_textbox;
	kui_control hft_elevation_set_height_checkbox;
	kui_control hft_elevation_diameter_label;
	kui_control hft_elevation_amount_label;

	// Texture browser
	texture_browser tex_browser;

} editor_state;

KAPI b8 editor_initialize (u64 *memory_requirement, struct editor_state *state, kname gmae_package_name);
KAPI void editor_shutdown (struct editor_state *state);

KAPI b8 editor_open (struct editor_state *state, kname scene_name, kname scene_package_name);
KAPI b8 editor_close (struct editor_state *state);
KAPI void editor_set_mode (struct editor_state *state, editor_mode mode);

KAPI void editor_clear_selected_entities (struct editor_state *state);
KAPI void editor_select_entities (struct editor_state *state, u32 count, kentity *entities);
KAPI void editor_add_to_selected_entities (struct editor_state *state, u32 count, kentity *entities);
KAPI void editor_select_parent (struct editor_state *state);
KAPI b8 editor_selection_contains (struct editor_state *state, kentity entity);

KAPI void editor_update (struct editor_state *state, frame_data *p_frame_data);
KAPI void editor_frame_prepare (struct editor_state *state, frame_data *p_frame_data, kcamera current_camera, b8 draw_gizmo, keditor_gizmo_pass_render_data *gizmo_pass_render_data);
KAPI b8 editor_render (struct editor_state *state, frame_data *p_frame_data, kcamera current_camera, ktexture colour_buffer_target, b8 draw_gizmo, keditor_gizmo_pass_render_data *gizmo_pass_render_data);

KAPI void editor_on_window_resize (struct editor_state *state, const struct kwindow *window);

KAPI void editor_setup_keymaps (struct editor_state *state);
KAPI void editor_destroy_keymaps (struct editor_state *state);

KAPI b8 editor_on_action (struct editor_state *state, u32 action_code);

KAPI void editor_on_lib_load (struct editor_state *state);
KAPI void editor_on_lib_unload (struct editor_state *state);
