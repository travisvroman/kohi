#include "testbed_main.h"

#include <assets/kasset_types.h>
#include <core/keymap.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <input_types.h>
#include <math/geometry.h>
#include <math/geometry_2d.h>
#include <math/math_types.h>
#include <renderer/renderer_types.h>
#include <strings/kname.h>

// Runtime
#include <application/application_types.h>
#include <containers/darray.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/input.h>
#include <core/kvar.h>
#include <core/metrics.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <renderer/kforward_renderer.h>
#include <renderer/renderer_frontend.h>
#include <resources/debug/debug_box3d.h>
#include <resources/skybox.h>
#include <resources/water_plane.h>
#include <strings/kstring.h>
#include <systems/asset_system.h>
#include <systems/kcamera_system.h>
#include <systems/kmaterial_system.h>
#include <systems/ktimeline_system.h>
#include <systems/ktransform_system.h>
#include <systems/light_system.h>
#include <systems/plugin_system.h>
#include <systems/texture_system.h>
#include <time/kclock.h>
#include <time/time_utils.h>
#include <world/kscene.h>

// Standard UI.
#include <controls/kui_button.h>
#include <controls/kui_label.h>
#include <controls/kui_panel.h>
#include <kui_plugin_main.h>
#include <kui_system.h>
#include <kui_types.h>
#include <renderer/kui_renderer.h>

// Audio
#include <audio/audio_frontend.h>

// TODO: debug only stuff, change to debug-only down the road when this isn't as critical to have.
#include <debug_console.h>

// Utils plugin
#include <editor/editor_gizmo.h>

// Game files
#if TESTBED_EDITOR
#	include "editor/editor.h"
#endif
#include "kui_types.h"
#include "testbed.klib_version.h"
#include "testbed_types.h"

struct kaudio_system_state;

/**
 * @brief Returns the scene to be rendered.
 * NOTE: Anything that needs the "current scene" should get a pointer to it through here.
 *
 * @param app A pointer to the application handle.
 *
 * @returns If in editor mode, returns the editor scene, if in game mode, return the active zone's scene. Otherwise KNULL.
 */
static struct kscene *get_current_render_scene (application *app);
static kcamera get_current_render_camera (application *app);

static void setup_keymaps (application *app);
static void remove_keymaps (application *app);

static f32 get_engine_delta_time (void);
static f32 get_engine_total_time (void);

static void game_register_events (application *app);
static void game_unregister_events (application *app);
static b8 game_on_mouse_move (u16 code, void *sender, void *listener_inst, event_context context);
static b8 game_on_drag (u16 code, void *sender, void *listener_inst, event_context context);
static b8 game_on_button (u16 code, void *sender, void *listener_inst, event_context context);
static b8 game_on_event (u16 code, void *sender, void *listener_inst, event_context context);

static void trigger_scene_load (console_command_context context);
static void trigger_scene_unload (console_command_context context);

static void game_register_commands (application *app);
static void game_unregister_commands (application *app);
static void game_command_exit (console_command_context context);
static void game_command_load_scene (console_command_context context);
static void game_command_unload_scene (console_command_context context);

static void game_command_set_camera_pos (console_command_context context);
static void game_command_set_camera_rot (console_command_context context);
static void game_command_set_render_mode (console_command_context context);

u64 application_state_size (void) {
	return sizeof(application_state);
}

b8 application_boot (struct application *app) {
	KINFO("Booting %s (%s)...", app->app_config.name, KVERSION);

	// Allocate the game state.
	app->state = kallocate(sizeof(application_state), MEMORY_TAG_GAME);
	app->state->running = false;

	application_config *config = &app->app_config;

	/* config->frame_allocator_size = MEBIBYTES(64); */
	config->app_frame_data_size = sizeof(application_frame_data);

	// Setup game constants.
	game_constants *constants = &app->state->game.constants;
	constants->base_movement_speed = 2.0f;
	constants->turn_speed = 2.5f;

	// Keymaps
	setup_keymaps(app);

	input_keymap_push(&app->state->global_keymap);

	// Register game events.
	game_register_events(app);

	// Register console commands.
	game_register_commands(app);

	// Set default game mode and keymap
	app->state->mode = TESTBED_APP_MODE_WORLD;
	input_keymap_push(&app->state->world_keymap);

	return true;
}

b8 application_initialize (struct application *app) {
	KINFO("Initializing application...");

	app->state->audio_system = engine_systems_get()->audio_system;

	// Get the standard ui plugin.
	app->state->kui_plugin = plugin_system_get(engine_systems_get()->plugin_system, "kohi.plugin.ui.kui");
	app->state->kui_plugin_state = app->state->kui_plugin->plugin_state;
	app->state->kui_state = app->state->kui_plugin_state->state;

	// Setup forward renderer.
	// Get colourbuffer and depthbuffer from the currently active window.
	kwindow *current_window = engine_active_window_get();
	ktexture global_colourbuffer = current_window->renderer_state->colourbuffer;
	ktexture global_depthbuffer = current_window->renderer_state->depthbuffer;
	if (!kforward_renderer_create(app, global_colourbuffer, global_depthbuffer, &app->state->game_renderer)) {
		KFATAL("Failed to create forward renderer! Application boot failed.");
		return false;
	}

	// Setup Standard UI renderer.
	if (!kui_renderer_create(&app->state->kui_renderer)) {
		KFATAL("Failed to create Standard UI renderer! Application boot failed.");
		return false;
	}

#if KOHI_DEBUG
	if (!debug_console_create(app->state->kui_state, &app->state->debug_console)) {
		KERROR("Failed to create debug console.");
		return false;
	}
#endif

	// TODO: Initialize game systems

	// Camera setup.
	rect_2di world_vp_rect = {0, 0, 1280 - 40, 720 - 40};
	vec3 world_cam_pos = (vec3){12.0f, 1.5f, -16.0f};
	vec3 world_cam_euler_rot_radians = (vec3){0.0f, deg_to_rad(-90.0f), 0.0f};
	app->state->world_camera = kcamera_create(KCAMERA_TYPE_3D, world_vp_rect, world_cam_pos, world_cam_euler_rot_radians, deg_to_rad(45.0f), 0.1f, 1000.0f);

	// Use a camera for UI rendering, too.
	rect_2di ui_vp_rect = {0, 0, 1280, 720};
	app->state->ui_camera = kcamera_create(KCAMERA_TYPE_2D, ui_vp_rect, vec3_zero(), vec3_zero(), 0.0f, 0.0f, 100.0f);

	// Setup the clear colour.
	renderer_clear_colour_set(engine_systems_get()->renderer_system, (vec4){0.0f, 0.2f, 0.2f, 1.0f});

#if TESTBED_EDITOR
	u64 editor_mem_req = 0;
	editor_initialize(&editor_mem_req, KNULL, kname_create("Testbed"));
	// TODO: Editor tag? or custom tag?
	app->state->editor = kallocate(editor_mem_req, MEMORY_TAG_GAME);
	if (!editor_initialize(&editor_mem_req, app->state->editor, kname_create("Testbed"))) {
		KERROR("Failed to initialize editor.");
		return false;
	}

	// Editor mode keymap
	editor_setup_keymaps(app->state->editor);

	keymap_binding_add(&app->state->editor->editor_keymap, KEY_F11, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_EDITOR_CLOSE);
#endif

	// Setup some UI elements
	kui_state *kui_state = app->state->kui_state;

	// Create test ui text objects
	// black background text
	app->state->debug_text_shadow = kui_label_control_create(kui_state, "testbed_mono_test_text_black", FONT_TYPE_BITMAP, kname_create("Ubuntu Mono 21px"), 21, "test text 123,\n\tyo!");
	kui_label_colour_set(kui_state, app->state->debug_text_shadow, (vec4){0, 0, 0, 1});
	KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, app->state->debug_text_shadow));

	app->state->debug_text = kui_label_control_create(kui_state, "testbed_mono_test_text", FONT_TYPE_BITMAP, kname_create("Ubuntu Mono 21px"), 21, "test text 123,\n\tyo!");
	KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, app->state->debug_text));
	// Move debug text to new bottom of screen.
	kui_control_position_set(kui_state, app->state->debug_text_shadow, vec3_create(20, app->state->height - 75, 0));
	kui_control_position_set(kui_state, app->state->debug_text, vec3_create(21, app->state->height - 74, 0));

	// Context-sensitive text
	app->state->context_sensitive_text = kui_label_control_create(kui_state, "testbed_UTF_test_sys_text", FONT_TYPE_SYSTEM, kname_create("Noto Sans CJK JP"), 31, "");
	kui_label_colour_set(kui_state, app->state->context_sensitive_text, (vec4){0, 1, 0, 1});
	KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, app->state->context_sensitive_text));
	kui_control_position_set(kui_state, app->state->context_sensitive_text, vec3_create(20, app->state->height - 50, 0));

#if KOHI_DEBUG
	// Ensure the debug console is on top.
	if (!debug_console_load(&app->state->debug_console)) {
		KERROR("Failed to load debug console.");
		return false;
	}
#endif // debug only

	// Clocks
	kzero_memory(&app->state->update_clock, sizeof(kclock));
	kzero_memory(&app->state->prepare_clock, sizeof(kclock));
	kzero_memory(&app->state->render_clock, sizeof(kclock));

	// Audio
	// Set some channel volumes. TODO: Load these from game prefs
	kaudio_master_volume_set(app->state->audio_system, 0.9f);
	kaudio_channel_volume_set(app->state->audio_system, 0, 1.0f);
	kaudio_channel_volume_set(app->state->audio_system, 1, 1.0f);
	kaudio_channel_volume_set(app->state->audio_system, 2, 1.0f);
	kaudio_channel_volume_set(app->state->audio_system, 3, 1.0f);
	kaudio_channel_volume_set(app->state->audio_system, 4, 1.0f);
	kaudio_channel_volume_set(app->state->audio_system, 7, 0.9f);

	app->state->scene_name = kname_create("test_scene");
	app->state->scene_package_name = kname_create("Testbed");

	app->state->running = true;

	return true;
}

b8 application_update (struct application *app, struct frame_data *p_frame_data) {
	application_frame_data *app_frame_data = p_frame_data->app_frame_data;
	if (!app_frame_data) {
		return true;
	}

	if (!app->state->running) {
		return true;
	}

	/* game_constants* constants = &app->state->game.constants; */
	/* player_state* player = &app->state->game.player; */

	kclock_start(&app->state->update_clock);

	vec3 pos = vec3_zero();
	vec3 rot = vec3_zero();

#if TESTBED_EDITOR
	if (app->state->mode == TESTBED_APP_MODE_EDITOR) {
		editor_update(app->state->editor, p_frame_data);

		// Update the debug text with camera position.
		pos = kcamera_get_position(app->state->editor->editor_camera);
		rot = kcamera_get_euler_rotation(app->state->editor->editor_camera);
	}
#endif

	// Game world updates
	if (app->state->mode == TESTBED_APP_MODE_WORLD) {

		// Update the debug text with camera position.
		pos = kcamera_get_position(app->state->world_camera);
		rot = kcamera_get_euler_rotation(app->state->world_camera);

		struct kscene *cur_scene = get_current_render_scene(app);
		if (cur_scene) {
			// Update the current scene. TODO: Perhaps the zone system should do this?
			if (!kscene_update(cur_scene, p_frame_data)) {
				KWARN("Failed to update main scene.");
			}

			kscene_state scene_state = kscene_state_get(cur_scene);
			if (scene_state == KSCENE_STATE_LOADED) {

				// Update LODs for the scene based on distance from the camera.
				// FIXME: update terrain LOD based on camera position.
				/* scene_update_lod_from_view_position(cur_scene, p_frame_data, pos, near_clip, far_clip); */

				// Handle player and camera movement.

				// Update the listener orientation.
				vec3 position = kcamera_get_position(app->state->world_camera);
				vec3 forward = kcamera_forward(app->state->world_camera);
				vec3 up = kcamera_up(app->state->world_camera);
				kaudio_system_listener_orientation_set(engine_systems_get()->audio_system, position, forward, up);
			} // end scene loaded
		}
	} // End WORLD state

	// Gather info and update debug display.
	{
		b8 left_down = input_is_button_down(MOUSE_BUTTON_LEFT);
		b8 right_down = input_is_button_down(MOUSE_BUTTON_RIGHT);
		i32 mouse_x, mouse_y;
		input_get_mouse_position(&mouse_x, &mouse_y);

		// Convert to NDC
		f32 mouse_x_ndc = range_convert_f32((f32)mouse_x, 0.0f, (f32)app->state->width, -1.0f, 1.0f);
		f32 mouse_y_ndc = range_convert_f32((f32)mouse_y, 0.0f, (f32)app->state->height, -1.0f, 1.0f);

		f64 fps, frame_time;
		metrics_frame(&fps, &frame_time);

		// Keep a running average of update and render timers over the last ~1 second.
		static f64 accumulated_ms = 0;
		static f32 total_update_seconds = 0;
		static f32 total_prepare_seconds = 0;
		static f32 total_render_seconds = 0;

		static f32 total_update_avg_us = 0;
		static f32 total_prepare_avg_us = 0;
		static f32 total_render_avg_us = 0;
		static f32 total_avg = 0; // total average across the frame

		total_update_seconds += app->state->last_update_elapsed;
		total_prepare_seconds += app->state->prepare_clock.elapsed;
		total_render_seconds += app->state->render_clock.elapsed;
		accumulated_ms += frame_time;

		// Once ~1 second has gone by, calculate the average and wipe the accumulators.
		if (accumulated_ms >= 1000.0f) {
			total_update_avg_us = (total_update_seconds / accumulated_ms) * K_SEC_TO_US_MULTIPLIER;
			total_prepare_avg_us = (total_prepare_seconds / accumulated_ms) * K_SEC_TO_US_MULTIPLIER;
			total_render_avg_us = (total_render_seconds / accumulated_ms) * K_SEC_TO_US_MULTIPLIER;
			total_avg = total_update_avg_us + total_prepare_avg_us + total_render_avg_us;
			total_render_seconds = 0;
			total_prepare_seconds = 0;
			total_update_seconds = 0;
			accumulated_ms = 0;
		}

		char *vsync_text = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? "YES" : " NO";
		const char *time_str = time_as_string_from_seconds(get_engine_total_time());
		const char *game_mode_text = testbed_application_mode_to_string(app->state->mode);

		// Calculate frame allocator pressure from the previous frame.
		f32 size_div = 0;
		f32 total_div = 0;
		u64 allocated = app->state->prev_framealloc_allocated;
		u64 total = app->state->prev_framealloc_total;
		const char *size_str = get_unit_for_size(allocated, &size_div);
		const char *total_str = get_unit_for_size(total, &total_div);

		char *text_buffer = string_format(
			"\
FPS: %5.1f(%4.1fms)        Pos=%V3.3 Rot=%V3D.3\n\
Upd: %8.3fus, Prep: %8.3fus, Rend: %8.3fus, Tot: %8.3fus \n\
Mouse: X=%-5d Y=%-5d   L=%s R=%s   NDC: X=%.6f, Y=%.6f\n\
VSync: %s Drawn: %-5u (%-5u shadow pass), Mode: %s, Run time: %s\n\
FAllocP: %.2f%s/%.2f%s (%.3f %%)",
			fps,
			frame_time,
			&pos,
			&rot,
			total_update_avg_us,
			total_prepare_avg_us,
			total_render_avg_us,
			total_avg,
			mouse_x, mouse_y,
			left_down ? "Y" : "N",
			right_down ? "Y" : "N",
			mouse_x_ndc,
			mouse_y_ndc,
			vsync_text,
			p_frame_data->drawn_mesh_count,
			p_frame_data->drawn_shadow_mesh_count,
			game_mode_text,
			time_str,
			size_div,
			size_str,
			total_div,
			total_str,
			((f32)allocated / (f32)total) * 100);

		// Update the text control.
		kui_label_text_set(app->state->kui_state, app->state->debug_text, text_buffer);
		kui_label_text_set(app->state->kui_state, app->state->debug_text_shadow, text_buffer);
		string_free(text_buffer);
		string_free(time_str);
	}

#if KOHI_DEBUG
	debug_console_update(&app->state->debug_console);
#endif

	kclock_update(&app->state->update_clock);
	app->state->last_update_elapsed = app->state->update_clock.elapsed;

	return true;
}

b8 application_prepare_frame (struct application *app, struct frame_data *p_frame_data) {
	if (!app->state->running) {
		return false;
	}

	kclock_start(&app->state->prepare_clock);

	p_frame_data->drawn_mesh_count = 0;

	kwindow *current_window = engine_active_window_get();
	ktexture global_colourbuffer = current_window->renderer_state->colourbuffer;
	ktexture global_depthbuffer = current_window->renderer_state->depthbuffer;

	frame_allocator_int *frame_allocator = &p_frame_data->allocator;

	// Setup the frame's render data structures.
	// Forward renderer
	p_frame_data->render_data = frame_allocator->allocate(sizeof(kforward_renderer_render_data));
	kzero_memory(p_frame_data->render_data, sizeof(kforward_renderer_render_data));
	// kui renderer
	p_frame_data->kui_render_data = frame_allocator->allocate(sizeof(kui_render_data));
	kzero_memory(p_frame_data->kui_render_data, sizeof(kui_render_data));
	kui_render_data *ui_render_data = p_frame_data->kui_render_data;
	// Editor
#if TESTBED_EDITOR
	app->state->editor->editor_gizmo_render_data = frame_allocator->allocate(sizeof(keditor_gizmo_pass_render_data));
	kzero_memory(app->state->editor->editor_gizmo_render_data, sizeof(keditor_gizmo_pass_render_data));
	keditor_gizmo_pass_render_data *editor_gizmo_render_data = app->state->editor->editor_gizmo_render_data;
#endif

	struct kscene *current_scene = get_current_render_scene(app);
	kcamera current_camera = get_current_render_camera(app);

	// SCENE
	kscene_frame_prepare(current_scene, p_frame_data, app->state->render_mode, current_camera);

// Editor frame prepare
#if TESTBED_EDITOR
	b8 draw_gizmo = app->state->mode == TESTBED_APP_MODE_EDITOR;
	editor_frame_prepare(app->state->editor, p_frame_data, app->state->editor->editor_camera, draw_gizmo, editor_gizmo_render_data);
#endif

	// Kohi UI pass
	{
		ui_render_data->projection = kcamera_get_projection(app->state->ui_camera);
		ui_render_data->view = mat4_identity();
		ui_render_data->colour_buffer = global_colourbuffer;
		ui_render_data->depth_stencil_buffer = global_depthbuffer;
		ui_render_data->ui_atlas = app->state->kui_state->atlas_texture;
		ui_render_data->shader_set0_binding_instance_id = app->state->kui_state->shader_set0_binding_instance_id;

		// Renderables.
		ui_render_data->renderables = darray_create_with_allocator(kui_renderable, &p_frame_data->allocator);
		if (!kui_system_render(app->state->kui_state, INVALID_KUI_CONTROL, p_frame_data, ui_render_data)) {
			KERROR("The standard ui system failed to render.");
		}

		ui_render_data->renderable_count = darray_length(ui_render_data->renderables);
		ui_render_data->renderables = ui_render_data->renderables;
	}

	kclock_update(&app->state->prepare_clock);

	return true;
}

b8 application_render_frame (struct application *app, struct frame_data *p_frame_data) {
	// Start the frame
	if (!app->state->running) {
		return true;
	}

	kclock_start(&app->state->render_clock);

	// Render the frame via the forward renderer.
	b8 result = kforward_renderer_render_frame(&app->state->game_renderer, p_frame_data, p_frame_data->render_data);
	if (!result) {
		KERROR("Failed to render forward frame! See logs for details.");
	}

#if TESTBED_EDITOR
	b8 draw_gizmo = app->state->mode == TESTBED_APP_MODE_EDITOR;
	kwindow *current_window = engine_active_window_get();
	ktexture global_colourbuffer = current_window->renderer_state->colourbuffer;
	if (!editor_render(app->state->editor, p_frame_data, app->state->editor->editor_camera, global_colourbuffer, draw_gizmo, app->state->editor->editor_gizmo_render_data)) {
		KERROR("Failed to render editor frame! See logs for details.");
	}
#endif

	// Standard ui render.
	if (!kui_renderer_render_frame(&app->state->kui_renderer, p_frame_data, p_frame_data->kui_render_data)) {
		KERROR("Failed to render kui frame! See logs for details.");
	}

	kclock_update(&app->state->render_clock);

	// Save off frame metrics.
	frame_allocator_int *frame_allocator = &p_frame_data->allocator;
	app->state->prev_framealloc_allocated = frame_allocator->allocated();
	app->state->prev_framealloc_total = frame_allocator->total_space();

	return result;
}

void application_on_window_resize (struct application *app, const struct kwindow *window) {
	if (!app->state) {
		return;
	}

	app->state->width = window->width;
	app->state->height = window->height;
	if (!window->width || !window->height) {
		return;
	}

	// Resize cameras.
	rect_2di world_vp_rect = {0, 0, app->state->width, app->state->height};
	// Set the vp_rect on all relevant cameras based on the new window size.
	kcamera_set_vp_rect(app->state->world_camera, world_vp_rect);

	// Send the update to any currently loaded world scene.
	if (app->state->current_scene) {
		kscene_on_window_resize(app->state->current_scene, window);
	}

#if TESTBED_EDITOR
	// This will also pass the resize on to any open "editor scene"
	editor_on_window_resize(app->state->editor, window);
#endif

	// UI camera needs it too.
	rect_2di ui_vp_rect = {0, 0, app->state->width, app->state->height};
	kcamera_set_vp_rect(app->state->ui_camera, ui_vp_rect);

	// Move debug text to new bottom of screen.
	kui_control_position_set(app->state->kui_state, app->state->debug_text, vec3_create(20, app->state->height - 136, 0));
	kui_control_position_set(app->state->kui_state, app->state->debug_text_shadow, vec3_create(21, app->state->height - 135, 0));

	kui_control_position_set(app->state->kui_state, app->state->context_sensitive_text, vec3_create(21, app->state->height - 170, 0));
}

#if KOHI_DEBUG
static b8 application_on_debug_action (struct application *app_inst, u32 action_code) {
	application_state *state = app_inst->state;

	switch (action_code) {
	// Debug actions
	// No-op unless a debug build
	case GAME_ACTION_CONSOLE_VIS: {
		b8 console_visible = debug_console_visible(&state->debug_console);
		console_visible = !console_visible;

		debug_console_visible_set(&state->debug_console, console_visible);
		if (console_visible) {
			input_keymap_push(&state->console_keymap);
		} else {
			input_keymap_pop();
		}
		return true;
	}
	case GAME_ACTION_CONSOLE_SCROLL_UP:
		state->debug_console.accumulated_time += get_engine_delta_time();
		if (state->debug_console.accumulated_time >= 0.1f) {
			debug_console_move_up(&state->debug_console);
			state->debug_console.accumulated_time = 0.0f;
		}
		return true;
	case GAME_ACTION_CONSOLE_SCROLL_DOWN:
		state->debug_console.accumulated_time += get_engine_delta_time();
		if (state->debug_console.accumulated_time >= 0.1f) {
			debug_console_move_down(&state->debug_console);
			state->debug_console.accumulated_time = 0.0f;
		}
		return true;
	case GAME_ACTION_CONSOLE_HISTORY_BACK:
		debug_console_history_back(&state->debug_console);
		return true;
	case GAME_ACTION_CONSOLE_HISTORY_FORWARD:
		debug_console_history_forward(&state->debug_console);
		return true;
		//
	}

	return false;
}
#endif

#if TESTBED_EDITOR
// only called when in-editor
static b8 application_on_editor_action (struct application *app_inst, u32 action_code) {
	application_state *state = app_inst->state;

	if (editor_on_action(state->editor, action_code)) {
		// Handled
		return true;
	}

	// Technically an editor command, but handle here.
	switch (action_code) {
	case GAME_ACTION_CONSOLE_VIS: {
		b8 console_visible = debug_console_visible(&state->debug_console);
		console_visible = !console_visible;

		debug_console_visible_set(&state->debug_console, console_visible);
		if (console_visible) {
			input_keymap_push(&state->console_keymap);
		} else {
			input_keymap_pop();
		}
		return true;
	}
	}

	return false;
}
#endif

/**
 * Gameplay actions
 */
static b8 application_on_gameplay_action (struct application *app_inst, u32 action_code) {
	application_state *state = app_inst->state;

	f32 delta = get_engine_delta_time();
	game_constants *constants = &state->game.constants;

	switch (action_code) {

	case GAME_ACTION_TURN_LEFT:
		kcamera_yaw(state->world_camera, 2.5f * delta);
		return true;
	case GAME_ACTION_TURN_RIGHT:
		kcamera_yaw(state->world_camera, -2.5f * delta);
		return true;
	case GAME_ACTION_LOOK_UP:
		kcamera_pitch(state->world_camera, 1.0f * delta);
		return true;
	case GAME_ACTION_LOOK_DOWN:
		kcamera_pitch(state->world_camera, -1.0f * delta);
		return true;
	case GAME_ACTION_MOVE_FORWARD:
		kcamera_move_forward(state->world_camera, constants->base_movement_speed * delta);
		return true;
	case GAME_ACTION_SPRINT_FORWARD:
		kcamera_move_forward(state->world_camera, (constants->base_movement_speed * 2.0f) * delta);
		return true;
	case GAME_ACTION_MOVE_BACKWARD:
		kcamera_move_backward(state->world_camera, constants->base_movement_speed * delta);
		return true;
	case GAME_ACTION_MOVE_LEFT:
		kcamera_move_left(state->world_camera, constants->base_movement_speed * delta);
		return true;
	case GAME_ACTION_MOVE_RIGHT:
		kcamera_move_right(state->world_camera, constants->base_movement_speed * delta);
		return true;
	case GAME_ACTION_MOVE_UP:
		kcamera_move_up(state->world_camera, constants->base_movement_speed * delta);
		return true;
	case GAME_ACTION_MOVE_DOWN:
		kcamera_move_down(state->world_camera, constants->base_movement_speed * delta);
		return true;
	}
	return false;
}

void application_on_action (struct application *app_inst, u32 action_code) {
	application_state *state = app_inst->state;

#if KOHI_DEBUG
	if (application_on_debug_action(app_inst, action_code)) {
		// Handled by debug.
		return;
	}
#endif

#if TESTBED_EDITOR
	if (application_on_editor_action(app_inst, action_code)) {
		// Handled by editor.
		return;
	}
#endif

	if (state->mode == TESTBED_APP_MODE_WORLD) {
		if (application_on_gameplay_action(app_inst, action_code)) {
			// Handled by editor.
			return;
		}
	}

	// TODO: break this up based on current game state (i.e. menu vs play vs title screen, etc.)
	switch (action_code) {
#if TESTBED_EDITOR
	case GAME_ACTION_EDITOR_CLOSE: {
		if (state->mode == TESTBED_APP_MODE_EDITOR) {
			if (editor_close(state->editor)) {
				// TODO: Should be the zone that was just edited.

				// Load up the current editor scene.
				kasset_text *asset = asset_system_request_text_sync(engine_systems_get()->asset_state, "test_scene");
				if (!asset) {
					KERROR("Failed to load test_scene scene asset.");
					return;
				}
				state->current_scene = kscene_create(kname_create("test_scene"), asset->content, 0, 0, false);

				state->mode = TESTBED_APP_MODE_WORLD;
				KTRACE("Changed to world mode, forget about it cuhh.");
				if (!input_keymap_pop()) {
					KERROR("No keymap was popped during editor->world");
				}
				input_keymap_push(&state->world_keymap);
			} else {
				KINFO("Editor failed to close, but this might not be an error.");
			}
		}
	} break;
	case GAME_ACTION_EDITOR_OPEN: {
		if (state->mode == TESTBED_APP_MODE_WORLD) {
			if (!state->current_scene) {
				// TODO: prompt for a selection.
				KERROR("Can't switch to editor without a scene loaded first.");
				return;
			}

			KINFO("Attempting to open editor for scene '%k', package='%k'...", state->scene_name, state->scene_package_name);
			if (editor_open(state->editor, state->scene_name, state->scene_package_name)) {
				KINFO("Unloading active zone scene...");
				// Unload the current zone's scene from the world.
				kscene_destroy(state->current_scene);
				state->current_scene = KNULL;
				KINFO("Zone scene unloaded.");

				state->mode = TESTBED_APP_MODE_EDITOR;
				KINFO("Editor opened successfully.");
			} else {
				KERROR("Editor failed to open.");
			}
		}
	} break;
#endif
	case GAME_ACTION_QUIT:
		KDEBUG("GAME_ACTION_QUIT");
		event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
		break;
	case GAME_ACTION_VSYNC_TOGGLE: {
		char cmd[30];
		string_ncopy(cmd, "kvar_set_int vsync 0", 29);
		b8 vsync_enabled = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT);
		u32 length = string_length(cmd);
		cmd[length - 1] = vsync_enabled ? '1' : '0';
		console_command_execute(cmd);
	} break;
	case GAME_ACTION_LOAD_TEST_SCENE: {
		char *command = string_format("load_scene %s", "test_zone");
		console_command_execute(command);
		string_free(command);
	} break;
	case GAME_ACTION_UNLOAD_SCENE: {
		// Just execute it as a console command as if it were entered in the debug console.
		console_command_execute("unload_zone");
	} break;
	}
}

void application_shutdown (struct application *app) {
	app->state->running = false;

#if TESTBED_EDITOR
	editor_shutdown(app->state->editor);
#endif

	// Shutdown game systems.

	// Also destroy the game renderer.
	kforward_renderer_destroy(&app->state->game_renderer);

#if KOHI_DEBUG
	debug_console_unload(&app->state->debug_console);
#endif
}

void application_lib_on_unload (struct application *app) {
	// Unregister game events.
	game_unregister_events(app);
	game_unregister_commands(app);
#if KOHI_DEBUG
	debug_console_on_lib_unload(&app->state->debug_console);
#endif
#if TESTBED_EDITOR
	editor_on_lib_unload(app->state->editor);
#endif
	// TODO: re-enable
	/* game_remove_keymaps(app); */
}

void application_lib_on_load (struct application *app) {
#if KOHI_DEBUG
	debug_console_on_lib_load(&app->state->debug_console, app->stage >= APPLICATION_STAGE_BOOT_COMPLETE);
#endif

	// Only do these things if already booted (i.e. to prevent on initial load.)
	if (app->stage >= APPLICATION_STAGE_BOOT_COMPLETE) {
		// TODO: re-enable
		/* game_setup_keymaps(app); */

		// (Re-)Register game events.
		game_register_events(app);

		// (Re-)Register game console commands.
		game_register_commands(app);

#if TESTBED_EDITOR
		editor_on_lib_load(app->state->editor);
#endif
	}
}

static struct kscene *get_current_render_scene (application *app) {
	if (app->state->mode == TESTBED_APP_MODE_WORLD) {
		return app->state->current_scene;
	}
#if TESTBED_EDITOR
	else if (app->state->mode == TESTBED_APP_MODE_EDITOR) {
		return app->state->editor->edit_scene;
	}
#endif

	return KNULL;
}

static kcamera get_current_render_camera (application *app) {
	if (app->state->mode == TESTBED_APP_MODE_WORLD) {
		return app->state->world_camera;
	}
#if TESTBED_EDITOR
	else if (app->state->mode == TESTBED_APP_MODE_EDITOR) {
		return app->state->editor->editor_camera;
	}
#endif

	return DEFAULT_KCAMERA;
}

static void setup_keymaps (application *app) {

	// Global keymap
	app->state->global_keymap = keymap_create();
	keymap_binding_add(&app->state->global_keymap, KEY_ESCAPE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_QUIT);
	keymap_binding_add(&app->state->global_keymap, KEY_V, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_VSYNC_TOGGLE);
#if KOHI_DEBUG
	keymap_binding_add(&app->state->global_keymap, KEY_GRAVE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_VIS);
#endif

	keymap_binding_add(&app->state->global_keymap, KEY_L, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_LOAD_TEST_SCENE);
	keymap_binding_add(&app->state->global_keymap, KEY_U, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_UNLOAD_SCENE);

	// World mode keymap
	{
		app->state->world_keymap = keymap_create();
#if TESTBED_EDITOR
		keymap_binding_add(&app->state->world_keymap, KEY_F11, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_EDITOR_OPEN);
#endif

		keymap_binding_add(&app->state->world_keymap, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_TURN_LEFT);
		keymap_binding_add(&app->state->world_keymap, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_TURN_LEFT);
		keymap_binding_add(&app->state->world_keymap, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_TURN_LEFT);
		keymap_binding_add(&app->state->world_keymap, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_TURN_LEFT);

		keymap_binding_add(&app->state->world_keymap, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_TURN_RIGHT);
		keymap_binding_add(&app->state->world_keymap, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_TURN_RIGHT);
		keymap_binding_add(&app->state->world_keymap, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_TURN_RIGHT);
		keymap_binding_add(&app->state->world_keymap, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_TURN_RIGHT);

		keymap_binding_add(&app->state->world_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_LOOK_UP);
		keymap_binding_add(&app->state->world_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_LOOK_UP);
		keymap_binding_add(&app->state->world_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_LOOK_DOWN);
		keymap_binding_add(&app->state->world_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_LOOK_DOWN);

		keymap_binding_add(&app->state->world_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_FORWARD);
		keymap_binding_add(&app->state->world_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, GAME_ACTION_SPRINT_FORWARD);
		keymap_binding_add(&app->state->world_keymap, KEY_S, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_BACKWARD);
		keymap_binding_add(&app->state->world_keymap, KEY_Q, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_LEFT);
		keymap_binding_add(&app->state->world_keymap, KEY_E, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_RIGHT);

		keymap_binding_add(&app->state->world_keymap, KEY_SPACE, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_UP);
		keymap_binding_add(&app->state->world_keymap, KEY_Z, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_MOVE_DOWN);
	}

	// A console-specific keymap. Is not pushed by default.
	{
#if KOHI_DEBUG
		app->state->console_keymap = keymap_create();
		app->state->console_keymap.overrides_all = true;
		keymap_binding_add(&app->state->console_keymap, KEY_GRAVE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_VIS);
		keymap_binding_add(&app->state->console_keymap, KEY_ESCAPE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_VIS);

		keymap_binding_add(&app->state->console_keymap, KEY_PAGEUP, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_SCROLL_UP);
		keymap_binding_add(&app->state->console_keymap, KEY_PAGEDOWN, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_SCROLL_DOWN);
		keymap_binding_add(&app->state->console_keymap, KEY_PAGEUP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_SCROLL_UP);
		keymap_binding_add(&app->state->console_keymap, KEY_PAGEDOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_SCROLL_DOWN);

		keymap_binding_add(&app->state->console_keymap, KEY_UP, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_HISTORY_BACK);
		keymap_binding_add(&app->state->console_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, GAME_ACTION_CONSOLE_HISTORY_FORWARD);
#endif
	}

// If this was done with the console open, push its keymap.
#if KOHI_DEBUG
	b8 console_visible = debug_console_visible(&app->state->debug_console);
	if (console_visible) {
		input_keymap_push(&app->state->console_keymap);
	}
#endif
}

static void remove_keymaps (application *app) {
	//
}

static f32 get_engine_delta_time (void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_delta_get(engine);
}

static f32 get_engine_total_time (void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_total_get(engine);
}

static void game_register_events (application *app) {
	KASSERT(event_register(GAME_EVENT_CODE_SHOW_CONTEXT_DISPLAY, app, game_on_event));
	KASSERT(event_register(GAME_EVENT_CODE_HIDE_CONTEXT_DISPLAY, app, game_on_event));
	KASSERT(event_register(EVENT_CODE_BUTTON_RELEASED, app, game_on_button));
	KASSERT(event_register(EVENT_CODE_MOUSE_MOVED, app, game_on_mouse_move));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, app, game_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_END, app, game_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAGGED, app, game_on_drag));
}

static void game_unregister_events (application *app) {
	KASSERT(event_unregister(GAME_EVENT_CODE_SHOW_CONTEXT_DISPLAY, app, game_on_event));
	KASSERT(event_unregister(GAME_EVENT_CODE_HIDE_CONTEXT_DISPLAY, app, game_on_event));
	KASSERT(event_unregister(EVENT_CODE_BUTTON_RELEASED, app, game_on_button));
	KASSERT(event_unregister(EVENT_CODE_MOUSE_MOVED, app, game_on_mouse_move));
	KASSERT(event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, app, game_on_drag));
	KASSERT(event_unregister(EVENT_CODE_MOUSE_DRAG_END, app, game_on_drag));
	KASSERT(event_unregister(EVENT_CODE_MOUSE_DRAGGED, app, game_on_drag));
}

static void game_register_commands (application *app) {
	KASSERT(console_command_register("exit", 0, 0, app, game_command_exit));
	KASSERT(console_command_register("quit", 0, 0, app, game_command_exit));
	KASSERT(console_command_register("load_scene", 1, 1, app, game_command_load_scene));
	KASSERT(console_command_register("unload_scene", 0, 0, app, game_command_unload_scene));
	KASSERT(console_command_register("set_camera_pos", 3, 3, app, game_command_set_camera_pos));
	KASSERT(console_command_register("set_camera_rot", 3, 3, app, game_command_set_camera_rot));
	KASSERT(console_command_register("render_mode_set", 1, 1, app, game_command_set_render_mode));
}

static void game_unregister_commands (application *app) {
	KASSERT(console_command_unregister("exit"));
	KASSERT(console_command_unregister("quit"));
	KASSERT(console_command_unregister("load_scene"));
	KASSERT(console_command_unregister("unload_scene"));
	KASSERT(console_command_unregister("set_camera_pos"));
	KASSERT(console_command_unregister("set_camera_rot"));
	KASSERT(console_command_unregister("render_mode_set"));
}

static b8 game_on_mouse_move (u16 code, void *sender, void *listener_inst, event_context context) {
	application *app = (application *)listener_inst;
	application_state *state = app->state;

	if (!state->running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(MOUSE_BUTTON_LEFT)) {
		/* i16 x = context.data.i16[0];
		i16 y = context.data.i16[1];

		mat4 view = kcamera_get_view(state->current_camera);
		vec3 origin = kcamera_get_position(state->current_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->current_camera);
		mat4 projection = kcamera_get_projection(state->current_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection); */
	}

	return false; // Allow other event handlers to process this event.
}

static b8 game_on_drag (u16 code, void *sender, void *listener_inst, event_context context) {
	application *app = (application *)listener_inst;
	application_state *state = app->state;

	if (!state->running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	/* i16 x = context.data.i16[0];
	i16 y = context.data.i16[1]; */
	u16 drag_button = context.data.u16[4];

	// Only care about left button drags.
	if (drag_button == MOUSE_BUTTON_LEFT) {
	}

	return false; // Let other handlers handle.
}

/* KAPI void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn); */

static b8 game_on_button (u16 code, void *sender, void *listener_inst, event_context context) {
	if (code == EVENT_CODE_BUTTON_PRESSED) {
		//
	} else if (code == EVENT_CODE_BUTTON_RELEASED) {
		u16 button = context.data.u16[4];
		switch (button) {
		case MOUSE_BUTTON_LEFT: {
			/* i16 x = context.data.i16[1]; */
			/* i16 y = context.data.i16[2]; */
			application *app = listener_inst;

			struct kscene *current_scene = get_current_render_scene(app);
			if (current_scene) {
				kscene_state scene_state = kscene_state_get(current_scene);
				if (scene_state == KSCENE_STATE_LOADED) {
				}
			}
		} break;
		}
	}

	// Allow other handlers to process the event.
	return false;
}

static b8 game_on_event (u16 code, void *sender, void *listener_inst, event_context context) {
	application *app = (application *)listener_inst;

	switch (code) {

	case GAME_EVENT_CODE_SHOW_CONTEXT_DISPLAY: {
		KTRACE("Show context display: '%s'", context.data.s);
		kui_label_text_set(app->state->kui_state, app->state->context_sensitive_text, context.data.s);
	} break;

	case GAME_EVENT_CODE_HIDE_CONTEXT_DISPLAY: {
		KTRACE("Hide context display.");
		kui_label_text_set(app->state->kui_state, app->state->context_sensitive_text, "");
	} break;
	}

	// Allow other systems to handle this
	return false;
}

static void trigger_scene_load (console_command_context context) {
	application *app = (application *)context.listener;

	// Trigger loading of the scene.
	kasset_text *asset = asset_system_request_text_sync(engine_systems_get()->asset_state, "test_scene");
	if (!asset) {
		KERROR("Failed to load test_scene scene asset.");
		return;
	}
	app->state->current_scene = kscene_create(kname_create("test_scene"), asset->content, 0, 0, false);
}

static void trigger_scene_unload (console_command_context context) {
	application *app = (application *)context.listener;

	// Trigger unloading of the scene.
	if (app->state->current_scene) {
		kscene_destroy(app->state->current_scene);
		app->state->current_scene = KNULL;
	}
}

static void game_command_exit (console_command_context context) {
	KDEBUG("game exit called!");
	event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
}

static void game_command_load_scene (console_command_context context) {
	trigger_scene_load(context);
}

static void game_command_unload_scene (console_command_context context) {
	trigger_scene_unload(context);
}

static void game_command_set_camera_pos (console_command_context context) {
	KTRACE("teleport disabled.");
	application *app = (application *)context.listener;

	vec3 new_position = {0};
	string_to_f32(context.arguments[0].value, &new_position.x);
	string_to_f32(context.arguments[1].value, &new_position.y);
	string_to_f32(context.arguments[2].value, &new_position.z);
	kcamera_set_position(get_current_render_camera(app), new_position);
}

// Takes rotation in degrees
static void game_command_set_camera_rot (console_command_context context) {
	KTRACE("teleport disabled.");
	application *app = (application *)context.listener;

	vec3 new_rotation_degrees = {0};
	string_to_f32(context.arguments[0].value, &new_rotation_degrees.x);
	string_to_f32(context.arguments[1].value, &new_rotation_degrees.y);
	string_to_f32(context.arguments[2].value, &new_rotation_degrees.z);

	kcamera_set_euler_rotation(get_current_render_camera(app), new_rotation_degrees);
}

static void game_command_set_render_mode (console_command_context context) {
	if (context.argument_count == 1) {
		application *app = (application *)context.listener;

		string_to_u32(context.arguments[0].value, &app->state->render_mode);
	}
}
