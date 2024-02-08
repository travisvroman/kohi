#include "testbed_main.h"

#include <containers/darray.h>
#include <core/console.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/input.h>
#include <core/kclock.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <core/logger.h>
#include <core/metrics.h>
#include <math/geometry_2d.h>
#include <math/geometry_3d.h>
#include <math/kmath.h>
#include <renderer/camera.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/terrain.h>

#include "core/engine.h"
#include "defines.h"
#include "game_state.h"
#include "math/math_types.h"
#include "renderer/viewport.h"
#include "systems/camera_system.h"

// Standard UI.
#include <controls/sui_button.h>
#include <controls/sui_label.h>
#include <controls/sui_panel.h>
#include <passes/ui_pass.h>
#include <standard_ui_system.h>

// Rendergraphs.
#include "graphs/editor_rendergraph.h"
#include "graphs/standard_ui_rendergraph.h"
#include "renderer/graphs/forward_rendergraph.h"
#include "renderer/rendergraph.h"

// TODO: Editor temp
#include <resources/debug/debug_box3d.h>
#include <resources/debug/debug_line3d.h>

#include "editor/editor_gizmo.h"

// TODO: temp
#include <core/identifier.h>
#include <math/transform.h>
#include <resources/loaders/audio_loader.h>
#include <resources/mesh.h>
#include <resources/scene.h>
#include <resources/skybox.h>
#include <systems/audio_system.h>
#include <systems/geometry_system.h>
#include <systems/light_system.h>
#include <systems/material_system.h>
#include <systems/resource_system.h>
#include <systems/shader_system.h>
// Standard ui
#include <core/systems_manager.h>
#include <standard_ui_system.h>

#include "debug_console.h"
// Game code.
#include "game_commands.h"
#include "game_keybinds.h"
// TODO: end temp

#include "testbed_lib_version.h"

/** @brief A private structure used to sort geometry by distance from the camera. */
typedef struct geometry_distance {
    /** @briefThe geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

void application_register_events(struct application* game_inst);
void application_unregister_events(struct application* game_inst);
static b8 load_main_scene(struct application* game_inst);
static b8 create_rendergraphs(application* app);
static b8 initialize_rendergraphs(application* app);
static b8 prepare_rendergraphs(application* app, frame_data* p_frame_data);
static b8 execute_rendergraphs(application* app, frame_data* p_frame_data);
static void destroy_rendergraphs(application* app);
static void refresh_rendergraph_pfns(application* app);

static void clear_debug_objects(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    if (state->test_boxes) {
        u32 box_count = darray_length(state->test_boxes);
        for (u32 i = 0; i < box_count; ++i) {
            debug_box3d* box = &state->test_boxes[i];
            debug_box3d_unload(box);
            debug_box3d_destroy(box);
        }
        darray_clear(state->test_boxes);
    }

    if (state->test_lines) {
        u32 line_count = darray_length(state->test_lines);
        for (u32 i = 0; i < line_count; ++i) {
            debug_line3d* line = &state->test_lines[i];
            debug_line3d_unload(line);
            debug_line3d_destroy(line);
        }
        darray_clear(state->test_lines);
    }
}

b8 game_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    application* game_inst = (application*)listener_inst;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    switch (code) {
        case EVENT_CODE_OBJECT_HOVER_ID_CHANGED: {
            state->hovered_object_id = context.data.u32[0];
            return true;
        }
        case EVENT_CODE_SET_RENDER_MODE: {
            i32 mode = context.data.i32[0];
            switch (mode) {
                default:
                case RENDERER_VIEW_MODE_DEFAULT:
                    KDEBUG("Renderer mode set to default.");
                    state->render_mode = RENDERER_VIEW_MODE_DEFAULT;
                    break;
                case RENDERER_VIEW_MODE_LIGHTING:
                    KDEBUG("Renderer mode set to lighting.");
                    state->render_mode = RENDERER_VIEW_MODE_LIGHTING;
                    break;
                case RENDERER_VIEW_MODE_NORMALS:
                    KDEBUG("Renderer mode set to normals.");
                    state->render_mode = RENDERER_VIEW_MODE_NORMALS;
                    break;
                case RENDERER_VIEW_MODE_CASCADES:
                    KDEBUG("Renderer mode set to cascades.");
                    state->render_mode = RENDERER_VIEW_MODE_CASCADES;
                    break;
                case RENDERER_VIEW_MODE_WIREFRAME:
                    KDEBUG("Renderer mode set to wireframe.");
                    state->render_mode = RENDERER_VIEW_MODE_WIREFRAME;
                    break;
            }
            return true;
        }
    }

    return false;
}

b8 game_on_debug_event(u16 code, void* sender, void* listener_inst, event_context data) {
    application* game_inst = (application*)listener_inst;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    if (code == EVENT_CODE_DEBUG0) {
        const char* names[3] = {
            "cobblestone",
            "paving",
            "paving2"};
        static i8 choice = 2;

        // Save off the old names.
        const char* old_name = names[choice];

        choice++;
        choice %= 3;

        // Just swap out the material on the first mesh if it exists.
        geometry* g = state->meshes[0].geometries[0];
        if (g) {
            // Acquire the new material.
            g->material = material_system_acquire(names[choice]);
            if (!g->material) {
                KWARN("event_on_debug_event no material found! Using default material.");
                g->material = material_system_get_default();
            }

            // Release the old diffuse material.
            material_system_release(old_name);
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG1) {
        if (state->main_scene.state < SCENE_STATE_LOADING) {
            KDEBUG("Loading main scene...");
            if (!load_main_scene(game_inst)) {
                KERROR("Error loading main scene");
            }
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG2) {
        if (state->main_scene.state == SCENE_STATE_LOADED) {
            KDEBUG("Unloading scene...");

            scene_unload(&state->main_scene, false);
            clear_debug_objects(game_inst);
            KDEBUG("Done.");
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG3) {
        if (state->test_audio_file) {
            // Cycle between first 5 channels.
            static i8 channel_id = -1;
            channel_id++;
            channel_id = channel_id % 5;
            KTRACE("Playing sound on channel %u", channel_id);
            audio_system_channel_play(channel_id, state->test_audio_file, false);
        }
    } else if (code == EVENT_CODE_DEBUG4) {
        if (state->test_loop_audio_file) {
            static b8 playing = true;
            playing = !playing;
            if (playing) {
                // Play on channel 6
                if (!audio_system_channel_emitter_play(6, &state->test_emitter)) {
                    KERROR("Failed to play test emitter.");
                }
            } else {
                // Stop channel 6.
                audio_system_channel_stop(6);
            }
        }
    }

    return false;
}

static b8 game_on_drag(u16 code, void* sender, void* listener_inst, event_context context) {
    i16 x = context.data.i16[0];
    i16 y = context.data.i16[1];
    u16 drag_button = context.data.u16[2];
    testbed_game_state* state = (testbed_game_state*)listener_inst;

    // Only care about left button drags.
    if (drag_button == BUTTON_LEFT) {
        mat4 view = camera_view_get(state->world_camera);
        vec3 origin = camera_position_get(state->world_camera);

        viewport* v = &state->world_viewport;
        ray r = ray_from_screen(
            vec2_create((f32)x, (f32)y),
            (v->rect),
            origin,
            view,
            v->projection);

        if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
            state->using_gizmo = true;
            // Drag start -- change the interaction mode to "dragging".
            editor_gizmo_interaction_begin(&state->gizmo, state->world_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
        } else if (code == EVENT_CODE_MOUSE_DRAGGED) {
            editor_gizmo_handle_interaction(&state->gizmo, state->world_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
        } else if (code == EVENT_CODE_MOUSE_DRAG_END) {
            editor_gizmo_interaction_end(&state->gizmo);
            state->using_gizmo = false;
        }
    }

    return false;  // Let other handlers handle.
}

b8 game_on_button(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_BUTTON_PRESSED) {
        //
    } else if (code == EVENT_CODE_BUTTON_RELEASED) {
        u16 button = context.data.u16[0];
        switch (button) {
            case BUTTON_LEFT: {
                i16 x = context.data.i16[1];
                i16 y = context.data.i16[2];
                testbed_game_state* state = (testbed_game_state*)listener_inst;

                // If the scene isn't loaded, don't do anything else.
                if (state->main_scene.state < SCENE_STATE_LOADED) {
                    return false;
                }

                // If "manipulating gizmo", don't do below logic.
                if (state->using_gizmo) {
                    return false;
                }

                mat4 view = camera_view_get(state->world_camera);
                vec3 origin = camera_position_get(state->world_camera);

                viewport* v = &state->world_viewport;
                // Only allow this action in the "primary" viewport.
                if (point_in_rect_2d((vec2){(f32)x, (f32)y}, v->rect)) {
                    ray r = ray_from_screen(
                        vec2_create((f32)x, (f32)y),
                        (v->rect),
                        origin,
                        view,
                        v->projection);

                    raycast_result r_result;
                    if (scene_raycast(&state->main_scene, &r, &r_result)) {
                        u32 hit_count = darray_length(r_result.hits);
                        for (u32 i = 0; i < hit_count; ++i) {
                            raycast_hit* hit = &r_result.hits[i];
                            KINFO("Hit! id: %u, dist: %f", hit->unique_id, hit->distance);

                            // Create a debug line where the ray cast starts and ends (at the intersection).
                            debug_line3d test_line;
                            debug_line3d_create(r.origin, hit->position, 0, &test_line);
                            debug_line3d_initialize(&test_line);
                            debug_line3d_load(&test_line);
                            // Yellow for hits.
                            debug_line3d_colour_set(&test_line, (vec4){1.0f, 1.0f, 0.0f, 1.0f});

                            darray_push(state->test_lines, test_line);

                            // Create a debug box to show the intersection point.
                            debug_box3d test_box;

                            debug_box3d_create((vec3){0.1f, 0.1f, 0.1f}, 0, &test_box);
                            debug_box3d_initialize(&test_box);
                            debug_box3d_load(&test_box);

                            extents_3d ext;
                            ext.min = vec3_create(hit->position.x - 0.05f, hit->position.y - 0.05f, hit->position.z - 0.05f);
                            ext.max = vec3_create(hit->position.x + 0.05f, hit->position.y + 0.05f, hit->position.z + 0.05f);
                            debug_box3d_extents_set(&test_box, ext);

                            darray_push(state->test_boxes, test_box);

                            // Object selection
                            if (i == 0) {
                                state->selection.unique_id = hit->unique_id;
                                state->selection.xform = scene_transform_get_by_id(&state->main_scene, hit->unique_id);
                                if (state->selection.xform) {
                                    KINFO("Selected object id %u", hit->unique_id);
                                    // state->gizmo.selected_xform = state->selection.xform;
                                    editor_gizmo_selected_transform_set(&state->gizmo, state->selection.xform);
                                    // transform_parent_set(&state->gizmo.xform, state->selection.xform);
                                }
                            }
                        }
                    } else {
                        KINFO("No hit");

                        // Create a debug line where the ray cast starts and continues to.
                        debug_line3d test_line;
                        debug_line3d_create(r.origin, vec3_add(r.origin, vec3_mul_scalar(r.direction, 100.0f)), 0, &test_line);
                        debug_line3d_initialize(&test_line);
                        debug_line3d_load(&test_line);
                        // Magenta for non-hits.
                        debug_line3d_colour_set(&test_line, (vec4){1.0f, 0.0f, 1.0f, 1.0f});

                        darray_push(state->test_lines, test_line);

                        if (state->selection.xform) {
                            KINFO("Object deselected.");
                            state->selection.xform = 0;
                            state->selection.unique_id = INVALID_ID;

                            editor_gizmo_selected_transform_set(&state->gizmo, 0);
                        }

                        // TODO: hide gizmo, disable input, etc.
                    }
                }

            } break;
        }
    }

    return false;
}

static b8 game_on_mouse_move(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(BUTTON_LEFT)) {
        i16 x = context.data.i16[0];
        i16 y = context.data.i16[1];

        testbed_game_state* state = (testbed_game_state*)listener_inst;

        mat4 view = camera_view_get(state->world_camera);
        vec3 origin = camera_position_get(state->world_camera);

        viewport* v = &state->world_viewport;
        ray r = ray_from_screen(
            vec2_create((f32)x, (f32)y),
            (v->rect),
            origin,
            view,
            v->projection);

        editor_gizmo_handle_interaction(&state->gizmo, state->world_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER);
    }
    return false;  // Allow other event handlers to recieve this event.
}

static void sui_test_button_on_click(struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        KDEBUG("Clicked '%s'!", self->name);
    }
}

u64 application_state_size(void) {
    return sizeof(testbed_game_state);
}

b8 application_boot(struct application* game_inst) {
    KINFO("Booting testbed (%s)...", KVERSION);

    // Allocate the game state.
    game_inst->state = kallocate(sizeof(testbed_game_state), MEMORY_TAG_GAME);
    ((testbed_game_state*)game_inst->state)->running = false;

    debug_console_create(&((testbed_game_state*)game_inst->state)->debug_console);

    application_config* config = &game_inst->app_config;

    config->frame_allocator_size = MEBIBYTES(64);
    config->app_frame_data_size = sizeof(testbed_application_frame_data);

    // Configure fonts.
    config->font_config.auto_release = false;
    config->font_config.default_bitmap_font_count = 1;

    bitmap_font_config bmp_font_config = {};
    // UbuntuMono21px NotoSans21px
    bmp_font_config.name = "Ubuntu Mono 21px";
    bmp_font_config.resource_name = "UbuntuMono21px";
    bmp_font_config.size = 21;
    config->font_config.bitmap_font_configs = kallocate(sizeof(bitmap_font_config) * 1, MEMORY_TAG_ARRAY);
    config->font_config.bitmap_font_configs[0] = bmp_font_config;

    system_font_config sys_font_config;
    sys_font_config.default_size = 20;
    sys_font_config.name = "Noto Sans";
    sys_font_config.resource_name = "NotoSansCJK";

    config->font_config.default_system_font_count = 1;
    config->font_config.system_font_configs = kallocate(sizeof(sys_font_config) * 1, MEMORY_TAG_ARRAY);
    config->font_config.system_font_configs[0] = sys_font_config;

    config->font_config.max_bitmap_font_count = 101;
    config->font_config.max_system_font_count = 101;

    if (!create_rendergraphs(game_inst)) {
        KERROR("Failed to create render graphs. Aborting application.");
        return false;
    }

    // Keymaps
    game_setup_keymaps(game_inst);
    // Console commands
    game_setup_commands(game_inst);

    return true;
}

b8 application_initialize(struct application* game_inst) {
    KDEBUG("game_initialize() called!");

    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    if (!initialize_rendergraphs(game_inst)) {
        KERROR("Failed to initialize rendergraphs. See logs for details.");
        return false;
    }

    systems_manager_state* sys_mgr_state = engine_systems_manager_state_get(game_inst);
    standard_ui_system_config standard_ui_cfg = {0};
    standard_ui_cfg.max_control_count = 1024;
    if (!systems_manager_register(sys_mgr_state, K_SYSTEM_TYPE_STANDARD_UI_EXT, standard_ui_system_initialize, standard_ui_system_shutdown, standard_ui_system_update, standard_ui_system_render_prepare_frame, &standard_ui_cfg)) {
        KERROR("Failed to register standard ui system.");
        return false;
    }

    application_register_events(game_inst);

    // Register resource loaders.
    resource_system_loader_register(audio_resource_loader_create());

    state->selection.unique_id = INVALID_ID;
    state->selection.xform = 0;

    debug_console_load(&state->debug_console);

    state->test_lines = darray_create(debug_line3d);
    state->test_boxes = darray_create(debug_box3d);

    // Viewport setup.
    // World Viewport
    rect_2d world_vp_rect = vec4_create(20.0f, 20.0f, 1280.0f - 40.0f, 720.0f - 40.0f);
    if (!viewport_create(world_vp_rect, deg_to_rad(45.0f), 0.1f, 1000.0f, RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE, &state->world_viewport)) {
        KERROR("Failed to create world viewport. Cannot start application.");
        return false;
    }

    // UI Viewport
    rect_2d ui_vp_rect = vec4_create(0.0f, 0.0f, 1280.0f, 720.0f);
    if (!viewport_create(ui_vp_rect, 0.0f, -100.0f, 100.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC, &state->ui_viewport)) {
        KERROR("Failed to create UI viewport. Cannot start application.");
        return false;
    }

    // TODO: test
    rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, 1280.0f - 40.0f, 720.0f - 40.0f);
    if (!viewport_create(world_vp_rect2, deg_to_rad(45.0f), 0.01f, 10.0f, RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE, &state->world_viewport2)) {
        KERROR("Failed to create world viewport 2. Cannot start application.");
        return false;
    }

    state->forward_move_speed = 5.0f * 5.0f;
    state->backward_move_speed = 2.5f * 5.0f;

    // Setup editor gizmo.
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
    editor_rendergraph_gizmo_set(&state->editor_graph, &state->gizmo);

    // World meshes
    // Invalidate all meshes.
    for (u32 i = 0; i < 10; ++i) {
        state->meshes[i].generation = INVALID_ID_U8;
        state->ui_meshes[i].generation = INVALID_ID_U8;
    }

    // Create test ui text objects
    if (!sui_label_control_create("testbed_mono_test_text", FONT_TYPE_BITMAP, "Ubuntu Mono 21px", 21, "test text 123,\n\tyo!", &state->test_text)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    } else {
        if (!sui_label_control_load(&state->test_text)) {
            KERROR("Failed to load test text.");
        } else {
            void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
            if (!standard_ui_system_register_control(sui_state, &state->test_text)) {
                KERROR("Unable to register control.");
            } else {
                if (!standard_ui_system_control_add_child(sui_state, 0, &state->test_text)) {
                    KERROR("Failed to parent test text.");
                } else {
                    state->test_text.is_active = true;
                    if (!standard_ui_system_update_active(sui_state, &state->test_text)) {
                        KERROR("Unable to update active state.");
                    }
                }
            }
        }
    }
    // Move debug text to new bottom of screen.
    sui_control_position_set(&state->test_text, vec3_create(20, game_inst->app_config.start_height - 75, 0));

    // Standard ui stuff.
    if (!sui_panel_control_create("test_panel", (vec2){300.0f, 300.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.5f}, &state->test_panel)) {
        KERROR("Failed to create test panel.");
    } else {
        if (!sui_panel_control_load(&state->test_panel)) {
            KERROR("Failed to load test panel.");
        } else {
            transform_translate(&state->test_panel.xform, (vec3){950, 350});
            void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
            if (!standard_ui_system_register_control(sui_state, &state->test_panel)) {
                KERROR("Unable to register control.");
            } else {
                if (!standard_ui_system_control_add_child(sui_state, 0, &state->test_panel)) {
                    KERROR("Failed to parent test panel.");
                } else {
                    state->test_panel.is_active = true;
                    if (!standard_ui_system_update_active(sui_state, &state->test_panel)) {
                        KERROR("Unable to update active state.");
                    }
                }
            }
        }
    }

    if (!sui_button_control_create("test_button", &state->test_button)) {
        KERROR("Failed to create test button.");
    } else {
        // Assign a click handler.
        state->test_button.on_click = sui_test_button_on_click;

        // Move and rotate it some.
        // quat rotation = quat_from_axis_angle((vec3){0, 0, 1}, deg_to_rad(-45.0f), false);
        // transform_translate_rotate(&state->test_button.xform, (vec3){50, 50, 0}, rotation);

        if (!sui_button_control_load(&state->test_button)) {
            KERROR("Failed to load test button.");
        } else {
            void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
            if (!standard_ui_system_register_control(sui_state, &state->test_button)) {
                KERROR("Unable to register control.");
            } else {
                if (!standard_ui_system_control_add_child(sui_state, &state->test_panel, &state->test_button)) {
                    KERROR("Failed to parent test button.");
                } else {
                    state->test_button.is_active = true;
                    if (!standard_ui_system_update_active(sui_state, &state->test_button)) {
                        KERROR("Unable to update active state.");
                    }
                }
            }
        }
    }

    if (!sui_label_control_create("testbed_UTF_test_sys_text", FONT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "Press 'L' to load a \n\tscene!\n\n\tこんにちは 한", &state->test_sys_text)) {
        KERROR("Failed to load basic ui system text.");
        return false;
    } else {
        if (!sui_label_control_load(&state->test_sys_text)) {
            KERROR("Failed to load test system text.");
        } else {
            void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
            if (!standard_ui_system_register_control(sui_state, &state->test_sys_text)) {
                KERROR("Unable to register control.");
            } else {
                if (!standard_ui_system_control_add_child(sui_state, 0, &state->test_sys_text)) {
                    KERROR("Failed to parent test system text.");
                } else {
                    state->test_sys_text.is_active = true;
                    if (!standard_ui_system_update_active(sui_state, &state->test_sys_text)) {
                        KERROR("Unable to update active state.");
                    }
                }
            }
        }
    }
    sui_control_position_set(&state->test_sys_text, vec3_create(950, 450, 0));
    // TODO: end temp load/prepare stuff

    state->world_camera = camera_system_acquire("world");
    camera_position_set(state->world_camera, (vec3){5.83f, 4.35f, 18.68f});
    camera_rotation_euler_set(state->world_camera, (vec3){-29.43f, -42.41f, 0.0f});

    // TODO: temp test
    state->world_camera_2 = camera_system_acquire("world_2");
    camera_position_set(state->world_camera_2, (vec3){5.83f, 4.35f, 18.68f});
    camera_rotation_euler_set(state->world_camera_2, (vec3){-29.43f, -42.41f, 0.0f});
    // camera_position_set(state->world_camera_2, vec3_zero());
    // camera_rotation_euler_set(state->world_camera_2, vec3_zero());

    // kzero_memory(&game_inst->frame_data, sizeof(app_frame_data));

    kzero_memory(&state->update_clock, sizeof(kclock));
    kzero_memory(&state->prepare_clock, sizeof(kclock));
    kzero_memory(&state->render_clock, sizeof(kclock));

    // Load up a test audio file.
    state->test_audio_file = audio_system_chunk_load("Test.ogg");
    if (!state->test_audio_file) {
        KERROR("Failed to load test audio file.");
    }
    // Looping audio file.
    state->test_loop_audio_file = audio_system_chunk_load("Fire_loop.ogg");
    // Test music
    state->test_music = audio_system_stream_load("Woodland Fantasy.mp3");
    if (!state->test_music) {
        KERROR("Failed to load test music file.");
    }

    // Setup a test emitter.
    state->test_emitter.file = state->test_loop_audio_file;
    state->test_emitter.volume = 1.0f;
    state->test_emitter.looping = true;
    state->test_emitter.falloff = 1.0f;
    state->test_emitter.position = vec3_create(10.0f, 0.8f, 20.0f);

    // Set some channel volumes.
    audio_system_master_volume_set(0.9f);
    audio_system_channel_volume_set(0, 1.0f);
    audio_system_channel_volume_set(1, 0.75f);
    audio_system_channel_volume_set(2, 0.50f);
    audio_system_channel_volume_set(3, 0.25);
    audio_system_channel_volume_set(4, 0.0f);

    audio_system_channel_volume_set(7, 0.9f);

    // Try playing the emitter.
    /* if (!audio_system_channel_emitter_play(6, &state->test_emitter)) {
        KERROR("Failed to play test emitter.");
    }
    audio_system_channel_play(7, state->test_music, true); */

    state->running = true;

    return true;
}

b8 application_update(struct application* game_inst, struct frame_data* p_frame_data) {
    testbed_application_frame_data* app_frame_data = (testbed_application_frame_data*)p_frame_data->application_frame_data;
    if (!app_frame_data) {
        return true;
    }

    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    if (!state->running) {
        return true;
    }

    kclock_start(&state->update_clock);

    // TODO: testing resize
    static f32 button_height = 50.0f;
    button_height = 50.0f + (ksin(p_frame_data->total_time) * 20.0f);
    sui_button_control_height_set(&state->test_button, (i32)button_height);

    // Update the bitmap text with camera position. NOTE: just using the default camera for now.
    vec3 pos = camera_position_get(state->world_camera);
    vec3 rot = camera_rotation_euler_get(state->world_camera);

    viewport* view_viewport = &state->world_viewport;

    f32 near_clip = view_viewport->near_clip;
    f32 far_clip = view_viewport->far_clip;

    if (state->main_scene.state >= SCENE_STATE_LOADED) {
        if (!scene_update(&state->main_scene, p_frame_data)) {
            KWARN("Failed to update main scene.");
        }

        // Update LODs for the scene based on distance from the camera.
        scene_update_lod_from_view_position(&state->main_scene, p_frame_data, pos, near_clip, far_clip);

        editor_gizmo_update(&state->gizmo);

        // // Perform a small rotation on the first mesh.
        // quat rotation = quat_from_axis_angle((vec3){0, 1, 0}, -0.5f * p_frame_data->delta_time, false);
        // transform_rotate(&state->meshes[0].transform, rotation);

        // // Perform a similar rotation on the second mesh, if it exists.
        // transform_rotate(&state->meshes[1].transform, rotation);

        // // Perform a similar rotation on the third mesh, if it exists.
        // transform_rotate(&state->meshes[2].transform, rotation);
        if (state->p_light_1) {
            state->p_light_1->data.colour = (vec4){
                KCLAMP(ksin(p_frame_data->total_time) * 75.0f + 50.0f, 0.0f, 100.0f),
                KCLAMP(ksin(p_frame_data->total_time - (K_2PI / 3)) * 75.0f + 50.0f, 0.0f, 100.0f),
                KCLAMP(ksin(p_frame_data->total_time - (K_4PI / 3)) * 75.0f + 50.0f, 0.0f, 100.0f),
                1.0f};
            state->p_light_1->data.position.z = 20.0f + ksin(p_frame_data->total_time);

            // Make the audio emitter follow it.
            state->test_emitter.position = vec3_from_vec4(state->p_light_1->data.position);
        }
    }

    // Track allocation differences.
    state->prev_alloc_count = state->alloc_count;
    state->alloc_count = get_memory_alloc_count();

    // Also tack on current mouse state.
    b8 left_down = input_is_button_down(BUTTON_LEFT);
    b8 right_down = input_is_button_down(BUTTON_RIGHT);
    i32 mouse_x, mouse_y;
    input_get_mouse_position(&mouse_x, &mouse_y);

    // Convert to NDC
    f32 mouse_x_ndc = range_convert_f32((f32)mouse_x, 0.0f, (f32)state->width, -1.0f, 1.0f);
    f32 mouse_y_ndc = range_convert_f32((f32)mouse_y, 0.0f, (f32)state->height, -1.0f, 1.0f);

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
    static f32 total_avg = 0;  // total average across the frame

    total_update_seconds += state->last_update_elapsed;
    total_prepare_seconds += state->prepare_clock.elapsed;
    total_render_seconds += state->render_clock.elapsed;
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

    char* vsync_text = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? "YES" : " NO";
    char text_buffer[2048];
    string_format(
        text_buffer,
        "\
FPS: %5.1f(%4.1fms)        Pos=[%7.3f %7.3f %7.3f] Rot=[%7.3f, %7.3f, %7.3f]\n\
Upd: %8.3fus, Prep: %8.3fus, Rend: %8.3fus, Tot: %8.3fus \n\
Mouse: X=%-5d Y=%-5d   L=%s R=%s   NDC: X=%.6f, Y=%.6f\n\
VSync: %s Drawn: %-5u (%-5u shadow pass) Hovered: %s%u",
        fps,
        frame_time,
        pos.x, pos.y, pos.z,
        rad_to_deg(rot.x), rad_to_deg(rot.y), rad_to_deg(rot.z),
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
        state->hovered_object_id == INVALID_ID ? "none" : "",
        state->hovered_object_id == INVALID_ID ? 0 : state->hovered_object_id);
    if (state->running) {
        sui_label_text_set(&state->test_text, text_buffer);
    }

    debug_console_update(&((testbed_game_state*)game_inst->state)->debug_console);

    vec3 forward = camera_forward(state->world_camera);
    vec3 up = camera_up(state->world_camera);
    audio_system_listener_orientation_set(pos, forward, up);

    kclock_update(&state->update_clock);
    state->last_update_elapsed = state->update_clock.elapsed;

    return true;
}

b8 application_prepare_frame(struct application* app_inst, struct frame_data* p_frame_data) {
    testbed_game_state* state = (testbed_game_state*)app_inst->state;
    if (!state->running) {
        return false;
    }

    kclock_start(&state->prepare_clock);

    if (!prepare_rendergraphs(app_inst, p_frame_data)) {
        KERROR("Preparation of rendergraphs failed. See logs for details.");
        return false;
    }

    kclock_update(&state->prepare_clock);
    return true;
}

b8 application_render_frame(struct application* game_inst, struct frame_data* p_frame_data) {
    // Start the frame
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    if (!state->running) {
        return true;
    }

    kclock_start(&state->render_clock);

    if (!execute_rendergraphs(game_inst, p_frame_data)) {
        KERROR("Execution of rendergraphs failed. See logs for details.");
        return false;
    }

    kclock_update(&state->render_clock);

    return true;
}

void application_on_resize(struct application* game_inst, u32 width, u32 height) {
    if (!game_inst->state) {
        return;
    }

    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    state->width = width;
    state->height = height;
    if (!width || !height) {
        return;
    }

    /* f32 half_width = state->width * 0.5f; */

    // Resize viewports.
    // World Viewport - right side
    rect_2d world_vp_rect = vec4_create(0.0f, 0.0f, state->width, state->height);  // vec4_create(half_width + 20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport, world_vp_rect);

    // UI Viewport
    rect_2d ui_vp_rect = vec4_create(0.0f, 0.0f, state->width, state->height);
    viewport_resize(&state->ui_viewport, ui_vp_rect);

    // World viewport 2
    /* rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f); */
    rect_2d world_vp_rect2 = vec4_create(0.0f, 0.0f, state->width, state->height);  // vec4_create(half_width + 20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport2, world_vp_rect2);

    // TODO: temp
    // Move debug text to new bottom of screen.
    sui_control_position_set(&state->test_text, vec3_create(20, state->height - 95, 0));

    // Pass the resize onto the rendergraphs.
    if (!forward_rendergraph_on_resize(&state->forward_graph, width, height)) {
        KERROR("Error resizing forward rendergraph. See logs for details.");
    }
    if (!editor_rendergraph_on_resize(&state->editor_graph, width, height)) {
        KERROR("Error resizing editor rendergraph. See logs for details.");
    }
    if (!standard_ui_rendergraph_on_resize(&state->standard_ui_graph, width, height)) {
        KERROR("Error resizing Standard UI rendergraph. See logs for details.");
    }

    // TODO: end temp
}

void application_shutdown(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    state->running = false;

    if (state->main_scene.state == SCENE_STATE_LOADED) {
        KDEBUG("Unloading scene...");

        scene_unload(&state->main_scene, true);
        clear_debug_objects(game_inst);

        KDEBUG("Done.");
    }

    // TODO: Temp

    // Destroy ui texts
    debug_console_unload(&state->debug_console);

    // Destroy rendergraph(s)
    destroy_rendergraphs(game_inst);
}

void application_lib_on_unload(struct application* game_inst) {
    application_unregister_events(game_inst);
    debug_console_on_lib_unload(&((testbed_game_state*)game_inst->state)->debug_console);
    game_remove_commands(game_inst);
    game_remove_keymaps(game_inst);
}

void application_lib_on_load(struct application* game_inst) {
    application_register_events(game_inst);
    debug_console_on_lib_load(&((testbed_game_state*)game_inst->state)->debug_console, game_inst->stage >= APPLICATION_STAGE_BOOT_COMPLETE);
    if (game_inst->stage >= APPLICATION_STAGE_BOOT_COMPLETE) {
        game_setup_commands(game_inst);
        game_setup_keymaps(game_inst);
        refresh_rendergraph_pfns(game_inst);
    }
}

static void toggle_vsync(void) {
    b8 vsync_enabled = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT);
    vsync_enabled = !vsync_enabled;
    renderer_flag_enabled_set(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT, vsync_enabled);
}

static b8 game_on_kvar_changed(u16 code, void* sender, void* listener_inst, event_context data) {
    if (code == EVENT_CODE_KVAR_CHANGED && strings_equali(data.data.c, "vsync")) {
        toggle_vsync();
    }
    return false;
}

void application_register_events(struct application* game_inst) {
    if (game_inst->stage >= APPLICATION_STAGE_BOOT_COMPLETE) {
        // TODO: temp
        event_register(EVENT_CODE_DEBUG0, game_inst, game_on_debug_event);
        event_register(EVENT_CODE_DEBUG1, game_inst, game_on_debug_event);
        event_register(EVENT_CODE_DEBUG2, game_inst, game_on_debug_event);
        event_register(EVENT_CODE_DEBUG3, game_inst, game_on_debug_event);
        event_register(EVENT_CODE_DEBUG4, game_inst, game_on_debug_event);
        event_register(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, game_inst, game_on_event);
        event_register(EVENT_CODE_SET_RENDER_MODE, game_inst, game_on_event);
        event_register(EVENT_CODE_BUTTON_RELEASED, game_inst->state, game_on_button);
        event_register(EVENT_CODE_MOUSE_MOVED, game_inst->state, game_on_mouse_move);
        event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, game_inst->state, game_on_drag);
        event_register(EVENT_CODE_MOUSE_DRAG_END, game_inst->state, game_on_drag);
        event_register(EVENT_CODE_MOUSE_DRAGGED, game_inst->state, game_on_drag);
        // TODO: end temp

        event_register(EVENT_CODE_KVAR_CHANGED, 0, game_on_kvar_changed);
    }
}

void application_unregister_events(struct application* game_inst) {
    event_unregister(EVENT_CODE_DEBUG0, game_inst, game_on_debug_event);
    event_unregister(EVENT_CODE_DEBUG1, game_inst, game_on_debug_event);
    event_unregister(EVENT_CODE_DEBUG2, game_inst, game_on_debug_event);
    event_unregister(EVENT_CODE_DEBUG3, game_inst, game_on_debug_event);
    event_unregister(EVENT_CODE_DEBUG4, game_inst, game_on_debug_event);
    event_unregister(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, game_inst, game_on_event);
    event_unregister(EVENT_CODE_SET_RENDER_MODE, game_inst, game_on_event);
    event_unregister(EVENT_CODE_BUTTON_RELEASED, game_inst->state, game_on_button);
    event_unregister(EVENT_CODE_MOUSE_MOVED, game_inst->state, game_on_mouse_move);
    event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, game_inst->state, game_on_drag);
    event_unregister(EVENT_CODE_MOUSE_DRAG_END, game_inst->state, game_on_drag);
    event_unregister(EVENT_CODE_MOUSE_DRAGGED, game_inst->state, game_on_drag);
    // TODO: end temp

    event_unregister(EVENT_CODE_KVAR_CHANGED, 0, game_on_kvar_changed);
}

static void refresh_rendergraph_pfns(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    editor_rendergraph_refresh_pfns(&state->editor_graph);
}

static b8 create_rendergraphs(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    forward_rendergraph_config forward_config = {0};
    forward_config.shadowmap_resolution = 2048;
    if (!forward_rendergraph_create(&forward_config, &state->forward_graph)) {
        KERROR("Forward rendergraph failed to initialize.");
        return false;
    }

    editor_rendergraph_config editor_config = {0};
    editor_config.dummy = 0;
    if (!editor_rendergraph_create(&editor_config, &state->editor_graph)) {
        KERROR("Editor rendergraph failed to initialize.");
        return false;
    }

    standard_ui_rendergraph_config sui_config = {0};
    sui_config.dummy = 0;
    if (!standard_ui_rendergraph_create(&sui_config, &state->standard_ui_graph)) {
        KERROR("Standard UI rendergraph failed to initialize.");
        return false;
    }

    return true;
}

static b8 initialize_rendergraphs(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    if (!forward_rendergraph_initialize(&state->forward_graph)) {
        KERROR("Failed to load Forward rendergraph resources.");
        return false;
    }
    if (!editor_rendergraph_initialize(&state->editor_graph)) {
        KERROR("Failed to load Editor rendergraph resources.");
        return false;
    }
    if (!standard_ui_rendergraph_initialize(&state->standard_ui_graph)) {
        KERROR("Failed to load Standard UI rendergraph resources.");
        return false;
    }

    return true;
}

static b8 prepare_rendergraphs(application* app, frame_data* p_frame_data) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    // Prepare the configured rendergraphs.
    if (!forward_rendergraph_frame_prepare(&state->forward_graph, p_frame_data, state->world_camera, &state->world_viewport, &state->main_scene, state->render_mode)) {
        KERROR("Forward rendergraph failed to prepare frame data.");
        return false;
    }

    if (!editor_rendergraph_frame_prepare(&state->editor_graph, p_frame_data, state->world_camera, &state->world_viewport, &state->main_scene, state->render_mode)) {
        KERROR("Editor rendergraph failed to prepare frame data.");
        return false;
    }

    if (!standard_ui_rendergraph_frame_prepare(&state->standard_ui_graph, p_frame_data, 0, &state->ui_viewport, &state->main_scene, state->render_mode)) {
        KERROR("Standard UI rendergraph failed to prepare frame data.");
        return false;
    }

    return true;
}

static b8 execute_rendergraphs(application* app, frame_data* p_frame_data) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    if (!forward_rendergraph_execute(&state->forward_graph, p_frame_data)) {
        KERROR("Forward rendergraph failed to execute frame. See logs for details.");
        return false;
    }

    if (!editor_rendergraph_execute(&state->editor_graph, p_frame_data)) {
        KERROR("Editor rendergraph failed to execute frame. See logs for details.");
        return false;
    }

    if (!standard_ui_rendergraph_execute(&state->standard_ui_graph, p_frame_data)) {
        KERROR("Standard UI rendergraph failed to execute frame. See logs for details.");
        return false;
    }

    return true;
}

static void destroy_rendergraphs(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    forward_rendergraph_destroy(&state->forward_graph);
    editor_rendergraph_destroy(&state->editor_graph);
    standard_ui_rendergraph_destroy(&state->standard_ui_graph);
}

static b8 load_main_scene(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    // Load up config file
    // TODO: clean up resource.
    resource scene_resource;
    if (!resource_system_load("test_scene", RESOURCE_TYPE_scene, 0, &scene_resource)) {
        KERROR("Failed to load scene file, check above logs.");
        return false;
    }

    scene_config* scene_cfg = (scene_config*)scene_resource.data;

    // TODO: temp load/prepare stuff
    if (!scene_create(scene_cfg, &state->main_scene)) {
        KERROR("Failed to create main scene");
        return false;
    }

    // Initialize
    if (!scene_initialize(&state->main_scene)) {
        KERROR("Failed initialize main scene, aborting game.");
        return false;
    }

    state->p_light_1 = scene_point_light_get(&state->main_scene, "point_light_1");

    // Actually load the scene.
    return scene_load(&state->main_scene);
}
