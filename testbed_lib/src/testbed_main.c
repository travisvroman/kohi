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
#include "resources/loaders/simple_scene_loader.h"
#include "systems/camera_system.h"

// Standard UI.
#include <controls/sui_button.h>
#include <controls/sui_label.h>
#include <controls/sui_panel.h>
#include <passes/ui_pass.h>
#include <standard_ui_system.h>

// Rendergraph and passes.
#include "passes/editor_pass.h"
#include "passes/scene_pass.h"
#include "passes/skybox_pass.h"
#include "renderer/rendergraph.h"
// Core shadow map pass.
#include <renderer/passes/shadow_map_pass.h>

// Views
/* #include "editor/render_view_wireframe.h"
#include "views/render_view_pick.h"
#include "views/render_view_ui.h"
#include "views/render_view_world.h" */

// TODO: Editor temp
#include <resources/debug/debug_box3d.h>
#include <resources/debug/debug_line3d.h>

#include "editor/editor_gizmo.h"
/* #include "editor/render_view_editor_world.h" */

// TODO: temp
#include <core/identifier.h>
#include <math/transform.h>
#include <resources/loaders/audio_loader.h>
#include <resources/mesh.h>
#include <resources/simple_scene.h>
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
    /** @brief The geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

b8 configure_render_views(application_config* config);
void application_register_events(struct application* game_inst);
void application_unregister_events(struct application* game_inst);
static b8 load_main_scene(struct application* game_inst);
static b8 configure_rendergraph(application* app);

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
        if (state->main_scene.state < SIMPLE_SCENE_STATE_LOADING) {
            KDEBUG("Loading main scene...");
            if (!load_main_scene(game_inst)) {
                KERROR("Error loading main scene");
            }
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG2) {
        if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
            KDEBUG("Unloading scene...");

            simple_scene_unload(&state->main_scene, false);
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
        shader* s = shader_system_get("Shader.Builtin.Terrain");
        if (!shader_system_reload(s)) {
            KERROR("Failed to reload terrain shader.");
        }
        /* if (state->test_loop_audio_file) {
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
        } */
    }

    return false;
}

b8 game_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    application* game_inst = (application*)listener_inst;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    if (code == EVENT_CODE_KEY_RELEASED) {
        u16 key_code = context.data.u16[0];
        // Change gizmo orientation.
        if (key_code == KEY_G) {
            editor_gizmo_orientation orientation = editor_gizmo_orientation_get(&state->gizmo);
            orientation++;
            if (orientation > EDITOR_GIZMO_ORIENTATION_MAX) {
                orientation = 0;
            }
            editor_gizmo_orientation_set(&state->gizmo, orientation);
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
                if (state->main_scene.state < SIMPLE_SCENE_STATE_LOADED) {
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
                    if (simple_scene_raycast(&state->main_scene, &r, &r_result)) {
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
                                state->selection.xform = simple_scene_transform_get_by_id(&state->main_scene, hit->unique_id);
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

    if (!configure_rendergraph(game_inst)) {
        KERROR("Failed to setup render graph. Aboring application.");
        return false;
    }

    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    if (!rendergraph_finalize(&state->frame_graph)) {
        KERROR("Failed to finalize rendergraph. See log for details.");
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
    if (!rendergraph_load_resources(&state->frame_graph)) {
        KERROR("Failed to load rendergraph resources.");
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
    resource_system_loader_register(simple_scene_resource_loader_create());
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

    if (state->main_scene.state >= SIMPLE_SCENE_STATE_LOADED) {
        if (!simple_scene_update(&state->main_scene, p_frame_data)) {
            KWARN("Failed to update main scene.");
        }

        // Update LODs for the scene based on distance from the camera.
        simple_scene_update_lod_from_view_position(&state->main_scene, p_frame_data, pos, near_clip, far_clip);

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

    // Skybox pass. This pass must always run, as it is what clears the screen.
    skybox_pass_extended_data* skybox_pass_ext_data = state->skybox_pass.pass_data.ext_data;
    state->skybox_pass.pass_data.vp = &state->world_viewport;
    camera* current_camera = state->world_camera;
    state->skybox_pass.pass_data.view_matrix = camera_view_get(current_camera);
    state->skybox_pass.pass_data.view_position = camera_position_get(current_camera);
    state->skybox_pass.pass_data.projection_matrix = state->world_viewport.projection;
    state->skybox_pass.pass_data.do_execute = true;
    skybox_pass_ext_data->sb = 0;

    // Tell our scene to generate relevant packet data. NOTE: Generates skybox and world packets.
    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        editor_gizmo_render_frame_prepare(&state->gizmo, p_frame_data);
        simple_scene_render_frame_prepare(&state->main_scene, p_frame_data);

        {
            skybox_pass_ext_data->sb = state->main_scene.sb;
        }

        camera* view_camera = state->world_camera;
        viewport* view_viewport = &state->world_viewport;

        f32 near = view_viewport->near_clip;
        f32 far = state->main_scene.dir_light ? state->main_scene.dir_light->data.shadow_distance + state->main_scene.dir_light->data.shadow_fade_distance : 0;
        f32 clip_range = far - near;

        f32 min_z = near;
        f32 max_z = near + clip_range;
        f32 range = max_z - min_z;
        f32 ratio = max_z / min_z;

        f32 cascade_split_multiplier = state->main_scene.dir_light ? state->main_scene.dir_light->data.shadow_split_mult : 0.95f;

        // Calculate splits based on view camera frustum.
        vec4 splits;
        for (u32 c = 0; c < MAX_SHADOW_CASCADE_COUNT; c++) {
            f32 p = (c + 1) / (f32)MAX_SHADOW_CASCADE_COUNT;
            f32 log = min_z * kpow(ratio, p);
            f32 uniform = min_z + range * p;
            f32 d = cascade_split_multiplier * (log - uniform) + uniform;
            splits.elements[c] = (d - near) / clip_range;
        }

        // Default values to use in the event there is no directional light.
        // These are required because the scene pass needs them.
        mat4 shadow_camera_lookats[MAX_SHADOW_CASCADE_COUNT];
        mat4 shadow_camera_projections[MAX_SHADOW_CASCADE_COUNT];
        vec3 shadow_camera_positions[MAX_SHADOW_CASCADE_COUNT];
        for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
            shadow_camera_lookats[i] = mat4_identity();
            shadow_camera_projections[i] = mat4_identity();
            shadow_camera_positions[i] = vec3_zero();
        }

        // Shadowmap pass - only runs if there is a directional light.
        if (state->main_scene.dir_light) {
            f32 last_split_dist = 0.0f;
            rendergraph_pass* pass = &state->shadowmap_pass;
            // Mark this pass as executable.
            pass->pass_data.do_execute = true;

            // Obtain the light direction.
            vec3 light_dir = vec3_normalized(vec3_from_vec4(state->main_scene.dir_light->data.direction));

            // Setup the extended data for the pass.
            shadow_map_pass_extended_data* ext_data = pass->pass_data.ext_data;
            ext_data->light = state->main_scene.dir_light;

            /* frustum culling_frustum; */
            vec3 culling_center;
            f32 culling_radius;

            // Get the view-projection matrix
            // TODO: pull max shadow dist + fade dist for far clip from light.
            mat4 shadow_dist_projection = mat4_perspective(
                view_viewport->fov,
                view_viewport->rect.width / view_viewport->rect.height,
                view_viewport->near_clip,
                200.0f + 25.0f);
            mat4 cam_view_proj = mat4_transposed(mat4_mul(camera_view_get(view_camera), shadow_dist_projection));

            for (u32 c = 0; c < MAX_SHADOW_CASCADE_COUNT; c++) {
                shadow_map_cascade_data* cascade = &ext_data->cascades[c];
                cascade->cascade_index = c;

                // NOTE: Each pass for cascades will need to do the following process.
                // The only real difference will be that the near/far clips will be adjusted for each.

                // Get the world-space corners of the view frustum.
                vec4 corners[8] = {0};
                frustum_corner_points_world_space(cam_view_proj, corners);

                // Adjust the corners by pulling/pushing the near/far according to the current split.
                f32 split_dist = splits.elements[c];
                for (u32 i = 0; i < 4; ++i) {
                    // far - near
                    vec4 dist = vec4_sub(corners[i + 4], corners[i]);
                    corners[i + 4] = vec4_add(corners[i], vec4_mul_scalar(dist, split_dist));
                    corners[i] = vec4_add(corners[i], vec4_mul_scalar(dist, last_split_dist));
                }

                // Calculate the center of the camera's frustum by averaging the points.
                // This is also used as the lookat point for the shadow "camera".
                vec3 center = vec3_zero();
                for (u32 i = 0; i < 8; ++i) {
                    center = vec3_add(center, vec3_from_vec4(corners[i]));
                }
                center = vec3_div_scalar(center, 8.0f);  // size
                if (c == MAX_CASCADE_COUNT - 1) {
                    culling_center = center;
                }

                // Get the furthest-out point from the center and use that as the extents.
                f32 radius = 0.0f;
                for (u32 i = 0; i < 8; ++i) {
                    f32 distance = vec3_distance(vec3_from_vec4(corners[i]), center);
                    radius = KMAX(radius, distance);
                }
                if (c == MAX_CASCADE_COUNT - 1) {
                    culling_radius = radius;
                }

                // Calculate the extents by using the radius from above.
                extents_3d extents;
                extents.max = vec3_create(radius, radius, radius);
                extents.min = vec3_mul_scalar(extents.max, -1.0f);

                // "Pull" the min inward and "push" the max outward on the z axis to make sure
                // shadow casters outside the view are captured as well (think trees above the player).
                // TODO: This should be adjustable/tuned per scene.
                f32 z_multiplier = 10.0f;
                if (extents.min.z < 0) {
                    extents.min.z *= z_multiplier;
                } else {
                    extents.min.z /= z_multiplier;
                }

                if (extents.max.z < 0) {
                    extents.max.z /= z_multiplier;
                } else {
                    extents.max.z *= z_multiplier;
                }

                // Generate lookat by moving along the opposite direction of the directional light by the
                // minimum extents. This is negated because the directional light points "down" and the camera
                // needs to be "up".
                shadow_camera_positions[c] = vec3_sub(center, vec3_mul_scalar(light_dir, -extents.min.z));
                shadow_camera_lookats[c] = mat4_look_at(shadow_camera_positions[c], center, vec3_up());

                // Generate ortho projection based on extents.
                shadow_camera_projections[c] = mat4_orthographic(extents.min.x, extents.max.x, extents.min.y, extents.max.y, extents.min.z, extents.max.z - extents.min.z);

                // Save these off to the pass data.
                cascade->view = shadow_camera_lookats[c];
                cascade->projection = shadow_camera_projections[c];

                // Store the split depth on the pass.
                cascade->split_depth = (near + split_dist * clip_range) * 1.0f;

                last_split_dist = split_dist;
            }

            // Gather the geometries to be rendered.
            // Note that this only needs to happen once, since all geometries visible by the furthest-out cascase
            // must also be drawn on the nearest cascade to ensure objects outside the view cast shadows into the
            // view properly.
            simple_scene* scene = &state->main_scene;
            ext_data->geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);
            if (!simple_scene_mesh_render_data_query_from_line(
                    scene,
                    light_dir,
                    culling_center,
                    culling_radius,
                    p_frame_data,
                    &ext_data->geometry_count, &ext_data->geometries)) {
                KERROR("Failed to query shadow map pass meshes.");
            }
            // Track the number of meshes drawn in the shadow pass.
            p_frame_data->drawn_shadow_mesh_count = ext_data->geometry_count;

            // Gather terrain geometries.
            ext_data->terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);
            if (!simple_scene_terrain_render_data_query_from_line(
                    scene,
                    light_dir,
                    culling_center,
                    culling_radius,
                    p_frame_data,
                    &ext_data->terrain_geometry_count, &ext_data->terrain_geometries)) {
                KERROR("Failed to query shadow map pass terrain geometries.");
            }

            // TODO: Counter for terrain geometries.
            p_frame_data->drawn_shadow_mesh_count += ext_data->terrain_geometry_count;
        }

        // Scene pass.
        {
            // Enable this pass for this frame.
            state->scene_pass.pass_data.do_execute = true;
            state->scene_pass.pass_data.vp = &state->world_viewport;
            camera* current_camera = state->world_camera;
            mat4 camera_view = camera_view_get(current_camera);
            mat4 camera_projection = state->world_viewport.projection;

            state->scene_pass.pass_data.view_matrix = camera_view;
            state->scene_pass.pass_data.view_position = camera_position_get(current_camera);
            state->scene_pass.pass_data.projection_matrix = camera_projection;

            scene_pass_extended_data* ext_data = state->scene_pass.pass_data.ext_data;
            // Pass over shadow map "camera" view and projection matrices (one per cascade).
            for (u32 c = 0; c < MAX_SHADOW_CASCADE_COUNT; c++) {
                ext_data->directional_light_views[c] = shadow_camera_lookats[c];
                ext_data->directional_light_projections[c] = shadow_camera_projections[c];

                shadow_map_pass_extended_data* sp_ext_data = state->shadowmap_pass.pass_data.ext_data;
                ext_data->cascade_splits.elements[c] = sp_ext_data->cascades[c].split_depth;
            }
            ext_data->render_mode = state->render_mode;
            // HACK: use the skybox cubemap as the irradiance texture for now.
            ext_data->irradiance_cube_texture = state->main_scene.sb->cubemap.texture;

            // Populate scene pass data.
            simple_scene* scene = &state->main_scene;

            // Camera frustum culling and count
            viewport* v = &state->world_viewport;
            vec3 forward = camera_forward(current_camera);
            vec3 right = camera_right(current_camera);
            vec3 up = camera_up(current_camera);
            frustum camera_frustum = frustum_create(&current_camera->position, &forward, &right,
                                                    &up, v->rect.width / v->rect.height, v->fov, v->near_clip, v->far_clip);

            p_frame_data->drawn_mesh_count = 0;

            ext_data->geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);

            // Query the scene for static meshes using the camera frustum.
            if (!simple_scene_mesh_render_data_query(
                    scene,
                    &camera_frustum,
                    current_camera->position,
                    p_frame_data,
                    &ext_data->geometry_count, &ext_data->geometries)) {
                KERROR("Failed to query scene pass meshes.");
            }

            // Track the number of meshes drawn in the shadow pass.
            p_frame_data->drawn_mesh_count = ext_data->geometry_count;

            // Add terrain(s)
            ext_data->terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);

            // Query the scene for terrain meshes using the camera frustum.
            if (!simple_scene_terrain_render_data_query(
                    scene,
                    &camera_frustum,
                    current_camera->position,
                    p_frame_data,
                    &ext_data->terrain_geometry_count, &ext_data->terrain_geometries)) {
                KERROR("Failed to query scene pass terrain geometries.");
            }

            // TODO: Counter for terrain geometries.
            p_frame_data->drawn_mesh_count += ext_data->terrain_geometry_count;

            // Debug geometry
            if (!simple_scene_debug_render_data_query(scene, &ext_data->debug_geometry_count, 0)) {
                KERROR("Failed to obtain count of debug render objects.");
                return false;
            }
            ext_data->debug_geometries = darray_reserve_with_allocator(geometry_render_data, ext_data->debug_geometry_count, &p_frame_data->allocator);

            if (!simple_scene_debug_render_data_query(scene, &ext_data->debug_geometry_count, &ext_data->debug_geometries)) {
                KERROR("Failed to obtain debug render objects.");
                return false;
            }
            // Make sure the count is correct before pushing.
            darray_length_set(ext_data->debug_geometries, ext_data->debug_geometry_count);

            // HACK: Inject raycast debug geometries into scene pass data.
            if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
                u32 line_count = darray_length(state->test_lines);
                for (u32 i = 0; i < line_count; ++i) {
                    geometry_render_data rd = {0};
                    rd.model = transform_world_get(&state->test_lines[i].xform);
                    geometry* g = &state->test_lines[i].geo;
                    rd.material = g->material;
                    rd.vertex_count = g->vertex_count;
                    rd.vertex_buffer_offset = g->vertex_buffer_offset;
                    rd.index_count = g->index_count;
                    rd.index_buffer_offset = g->index_buffer_offset;
                    rd.unique_id = INVALID_ID_U16;
                    darray_push(ext_data->debug_geometries, rd);
                    ext_data->debug_geometry_count++;
                }
                u32 box_count = darray_length(state->test_boxes);
                for (u32 i = 0; i < box_count; ++i) {
                    geometry_render_data rd = {0};
                    rd.model = transform_world_get(&state->test_boxes[i].xform);
                    geometry* g = &state->test_boxes[i].geo;
                    rd.material = g->material;
                    rd.vertex_count = g->vertex_count;
                    rd.vertex_buffer_offset = g->vertex_buffer_offset;
                    rd.index_count = g->index_count;
                    rd.index_buffer_offset = g->index_buffer_offset;
                    rd.unique_id = INVALID_ID_U16;
                    darray_push(ext_data->debug_geometries, rd);
                    ext_data->debug_geometry_count++;
                }
            }
        }  // scene loaded.

        // Editor pass
        {
            // Enable this pass for this frame.
            state->editor_pass.pass_data.do_execute = true;
            state->editor_pass.pass_data.vp = &state->world_viewport;
            state->editor_pass.pass_data.view_matrix = camera_view_get(current_camera);
            state->editor_pass.pass_data.view_position = camera_position_get(current_camera);
            state->editor_pass.pass_data.projection_matrix = state->world_viewport.projection;

            editor_pass_extended_data* ext_data = state->editor_pass.pass_data.ext_data;

            geometry* g = &state->gizmo.mode_data[state->gizmo.mode].geo;

            // vec3 camera_pos = camera_position_get(c);
            // vec3 gizmo_pos = transform_position_get(&packet_data->gizmo->xform);
            // TODO: Should get this from the camera/viewport.
            // f32 fov = deg_to_rad(45.0f);
            // f32 dist = vec3_distance(camera_pos, gizmo_pos);

            mat4 model = transform_world_get(&state->gizmo.xform);
            // f32 fixed_size = 0.1f;                            // TODO: Make this a configurable option for gizmo size.
            f32 scale_scalar = 1.0f;                   // ((2.0f * ktan(fov * 0.5f)) * dist) * fixed_size;
            state->gizmo.scale_scalar = scale_scalar;  // Keep a copy of this for hit detection.
            mat4 scale = mat4_scale((vec3){scale_scalar, scale_scalar, scale_scalar});
            model = mat4_mul(model, scale);

            geometry_render_data render_data = {0};
            render_data.model = model;
            render_data.material = g->material;
            render_data.vertex_count = g->vertex_count;
            render_data.vertex_buffer_offset = g->vertex_buffer_offset;
            render_data.index_count = g->index_count;
            render_data.index_buffer_offset = g->index_buffer_offset;
            render_data.unique_id = INVALID_ID;

            ext_data->debug_geometries = darray_create_with_allocator(geometry_render_data, &p_frame_data->allocator);
            darray_push(ext_data->debug_geometries, render_data);

#ifdef _DEBUG
            {
                geometry_render_data plane_normal_render_data = {0};
                plane_normal_render_data.model = transform_world_get(&state->gizmo.plane_normal_line.xform);
                geometry* g = &state->gizmo.plane_normal_line.geo;
                plane_normal_render_data.material = 0;
                plane_normal_render_data.material = g->material;
                plane_normal_render_data.vertex_count = g->vertex_count;
                plane_normal_render_data.vertex_buffer_offset = g->vertex_buffer_offset;
                plane_normal_render_data.index_count = g->index_count;
                plane_normal_render_data.index_buffer_offset = g->index_buffer_offset;
                plane_normal_render_data.unique_id = INVALID_ID;
                darray_push(ext_data->debug_geometries, plane_normal_render_data);
            }
#endif
            ext_data->debug_geometry_count = darray_length(ext_data->debug_geometries);
        }

        /* // Wireframe
        {
            render_view_packet* view_packet = &packet->views[TESTBED_PACKET_VIEW_WIREFRAME];
            const render_view* view = view_packet->view;

            render_view_wireframe_data wireframe_data = {0};
            // TODO: Get a list of geometries not culled for the current camera.
            //
            wireframe_data.selected_id = state->selection.unique_id;
            wireframe_data.world_geometries = packet->views[TESTBED_PACKET_VIEW_WORLD].geometries;
            wireframe_data.terrain_geometries = packet->views[TESTBED_PACKET_VIEW_WORLD].terrain_geometries;
            if (!render_view_system_packet_build(view, p_frame_data, &state->world_viewport2, state->world_camera_2, &wireframe_data, view_packet)) {
                KERROR("Failed to build packet for view 'wireframe'");
                return false;
            }
        } */
    } else {
        // Do not run these passes if the scene is not loaded.
        state->scene_pass.pass_data.do_execute = false;
        state->shadowmap_pass.pass_data.do_execute = false;
        state->editor_pass.pass_data.do_execute = false;
    }

    // UI
    {
        ui_pass_extended_data* ext_data = state->ui_pass.pass_data.ext_data;
        state->ui_pass.pass_data.vp = &state->ui_viewport;
        state->ui_pass.pass_data.view_matrix = mat4_identity();
        state->ui_pass.pass_data.projection_matrix = state->ui_viewport.projection;
        state->ui_pass.pass_data.do_execute = true;

        // Renderables.
        ext_data->sui_render_data.renderables = darray_create_with_allocator(standard_ui_renderable, &p_frame_data->allocator);
        void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
        if (!standard_ui_system_render(sui_state, 0, p_frame_data, &ext_data->sui_render_data)) {
            KERROR("The standard ui system failed to render.");
        }
    }

    // Pick
    /*{
        render_view_packet* view_packet = &packet->views[TESTBED_PACKET_VIEW_PICK];
        const render_view* view = view_packet->view;

        // Pick uses both world and ui packet data.
        pick_packet_data pick_packet = {0};
        pick_packet.ui_mesh_data = ui_packet.mesh_data;
        pick_packet.world_mesh_data = packet->views[TESTBED_PACKET_VIEW_WORLD].geometries;
        pick_packet.terrain_mesh_data = packet->views[TESTBED_PACKET_VIEW_WORLD].terrain_geometries;
        pick_packet.texts = ui_packet.texts;
        pick_packet.text_count = ui_packet.text_count;

        if (!render_view_system_packet_build(view, p_frame_data->frame_allocator, &pick_packet, view_packet)) {
            KERROR("Failed to build packet for view 'pick'.");
            return false;
        }
    }*/
    // TODO: end temp

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

    if (!rendergraph_execute_frame(&state->frame_graph, p_frame_data)) {
        KERROR("Failed to execute rendergraph frame.");
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

    // Pass the resize onto the rendergraph.
    rendergraph_on_resize(&state->frame_graph, state->width, state->height);

    // TODO: end temp
}

void application_shutdown(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    state->running = false;

    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        KDEBUG("Unloading scene...");

        simple_scene_unload(&state->main_scene, true);
        clear_debug_objects(game_inst);

        KDEBUG("Done.");
    }

    // TODO: Temp

    // Destroy ui texts
    debug_console_unload(&state->debug_console);

    // Destroy rendergraph(s)
    rendergraph_destroy(&state->frame_graph);
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

        event_register(EVENT_CODE_KEY_PRESSED, game_inst, game_on_key);
        event_register(EVENT_CODE_KEY_RELEASED, game_inst, game_on_key);

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

    event_unregister(EVENT_CODE_KEY_PRESSED, game_inst, game_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, game_inst, game_on_key);

    event_unregister(EVENT_CODE_KVAR_CHANGED, 0, game_on_kvar_changed);
}

#define RG_CHECK(expr)                             \
    if (!expr) {                                   \
        KERROR("Failed to execute: '%s'.", #expr); \
        return false;                              \
    }

static void refresh_rendergraph_pfns(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    state->skybox_pass.initialize = skybox_pass_initialize;
    state->skybox_pass.execute = skybox_pass_execute;
    state->skybox_pass.destroy = skybox_pass_destroy;

    state->shadowmap_pass.initialize = shadow_map_pass_initialize;
    state->shadowmap_pass.execute = shadow_map_pass_execute;
    state->shadowmap_pass.destroy = shadow_map_pass_destroy;
    state->shadowmap_pass.load_resources = shadow_map_pass_load_resources;
    /* state->shadowmap_pass.source_populate = shadow_map_pass_source_populate; */

    state->scene_pass.initialize = scene_pass_initialize;
    state->scene_pass.execute = scene_pass_execute;
    state->scene_pass.destroy = scene_pass_destroy;
    state->scene_pass.load_resources = scene_pass_load_resources;

    state->editor_pass.initialize = editor_pass_initialize;
    state->editor_pass.execute = editor_pass_execute;
    state->editor_pass.destroy = editor_pass_destroy;

    state->ui_pass.initialize = ui_pass_initialize;
    state->ui_pass.execute = ui_pass_execute;
    state->ui_pass.destroy = ui_pass_destroy;
}

static b8 configure_rendergraph(application* app) {
    testbed_game_state* state = (testbed_game_state*)app->state;

    if (!rendergraph_create("testbed_frame_rendergraph", app, &state->frame_graph)) {
        KERROR("Failed to create rendergraph.");
        return false;
    }

    // Add global sources.
    if (!rendergraph_global_source_add(&state->frame_graph, "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL)) {
        KERROR("Failed to add global colourbuffer source.");
        return false;
    }
    if (!rendergraph_global_source_add(&state->frame_graph, "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL)) {
        KERROR("Failed to add global depthbuffer source.");
        return false;
    }

    // Skybox pass
    RG_CHECK(rendergraph_pass_create(&state->frame_graph, "skybox", skybox_pass_create, 0, &state->skybox_pass));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "skybox", "colourbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "skybox", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "skybox", "colourbuffer", 0, "colourbuffer"));

    // Shadowmap pass
    const char* shadowmap_pass_name = "shadowmap_pass";
    shadow_map_pass_config shadow_pass_config = {0};
    shadow_pass_config.resolution = 2048;
    RG_CHECK(rendergraph_pass_create(&state->frame_graph, shadowmap_pass_name, shadow_map_pass_create, &shadow_pass_config, &state->shadowmap_pass));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, shadowmap_pass_name, "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_SELF));

    // Scene pass
    RG_CHECK(rendergraph_pass_create(&state->frame_graph, "scene", scene_pass_create, 0, &state->scene_pass));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "scene", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "scene", "depthbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "scene", "shadowmap"));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "scene", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "scene", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "scene", "colourbuffer", "skybox", "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "scene", "depthbuffer", 0, "depthbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "scene", "shadowmap", "shadowmap_pass", "depthbuffer"));

    // Editor pass
    RG_CHECK(rendergraph_pass_create(&state->frame_graph, "editor", editor_pass_create, 0, &state->editor_pass));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "editor", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "editor", "depthbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "editor", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "editor", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "editor", "colourbuffer", "scene", "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "editor", "depthbuffer", "scene", "depthbuffer"));

    // UI pass
    RG_CHECK(rendergraph_pass_create(&state->frame_graph, "ui", ui_pass_create, 0, &state->ui_pass));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "ui", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&state->frame_graph, "ui", "depthbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "ui", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&state->frame_graph, "ui", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "ui", "colourbuffer", "editor", "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&state->frame_graph, "ui", "depthbuffer", 0, "depthbuffer"));

    refresh_rendergraph_pfns(app);

    // if (!rendergraph_finalize(&state->frame_graph)) {
    //     KERROR("Failed to finalize rendergraph. See log for details.");
    //     return false;
    // }

    return true;
}

/* b8 configure_render_views(application_config* config) {
    config->views = darray_create(render_view);

    // World view.
    {
        render_view world_view = {0};
        world_view.name = "world";
        world_view.renderpass_count = 2;
        world_view.passes = kallocate(sizeof(renderpass) * world_view.renderpass_count, MEMORY_TAG_ARRAY);

        // Renderpass config - SKYBOX.
        renderpass_config skybox_pass = {0};
        skybox_pass.name = "Renderpass.Builtin.Skybox";
        skybox_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
        skybox_pass.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;
        skybox_pass.depth = 1.0f;
        skybox_pass.stencil = 0;
        skybox_pass.target.attachment_count = 1;
        skybox_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * skybox_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        skybox_pass.render_target_count = renderer_window_attachment_count_get();

        // Color attachment.
        render_target_attachment_config* skybox_target_colour = &skybox_pass.target.attachments[0];
        skybox_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        skybox_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        skybox_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        skybox_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        skybox_target_colour->present_after = false;

        if (!renderer_renderpass_create(&skybox_pass, &world_view.passes[0])) {
            KERROR("Skybox view - Failed to create renderpass '%s'", world_view.passes[0].name);
            return false;
        }
        // Renderpass config - WORLD.
        renderpass_config world_pass = {0};
        world_pass.name = "Renderpass.Builtin.World";
        world_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
        world_pass.clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
        world_pass.depth = 1.0f;
        world_pass.stencil = 0;
        world_pass.target.attachment_count = 2;
        world_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * world_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        world_pass.render_target_count = renderer_window_attachment_count_get();

        // Colour attachment
        render_target_attachment_config* world_target_colour = &world_pass.target.attachments[0];
        world_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        world_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        world_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
        world_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        world_target_colour->present_after = false;

        // Depth attachment
        render_target_attachment_config* world_target_depth = &world_pass.target.attachments[1];
        world_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
        world_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        world_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        world_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        world_target_depth->present_after = false;

        if (!renderer_renderpass_create(&world_pass, &world_view.passes[1])) {
            KERROR("World view - Failed to create renderpass '%s'", world_view.passes[1].name);
            return false;
        }

        // Assign function pointers.
        world_view.on_packet_build = render_view_world_on_packet_build;
        world_view.on_packet_destroy = render_view_world_on_packet_destroy;
        world_view.on_render = render_view_world_on_render;
        world_view.on_registered = render_view_world_on_registered;
        world_view.on_destroy = render_view_world_on_destroy;
        world_view.on_resize = render_view_world_on_resize;
        world_view.attachment_target_regenerate = 0;

        darray_push(config->views, world_view);
    }

    // TODO: Editor temp
    // Editor World view.
    {
        render_view editor_world_view = {0};
        editor_world_view.name = "editor_world";
        editor_world_view.renderpass_count = 1;
        editor_world_view.passes = kallocate(sizeof(renderpass) * editor_world_view.renderpass_count, MEMORY_TAG_ARRAY);

        // Renderpass config.
        renderpass_config editor_world_pass = {0};
        editor_world_pass.name = "Renderpass.Testbed.EditorWorld";
        editor_world_pass.clear_colour = (vec4){0.0f, 0.0f, 0.0f, 1.0f};
        editor_world_pass.clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
        editor_world_pass.depth = 1.0f;
        editor_world_pass.stencil = 0;
        editor_world_pass.target.attachment_count = 2;
        editor_world_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * editor_world_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        editor_world_pass.render_target_count = renderer_window_attachment_count_get();

        // Colour attachment
        render_target_attachment_config* editor_world_target_colour = &editor_world_pass.target.attachments[0];
        editor_world_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        editor_world_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        editor_world_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
        editor_world_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        editor_world_target_colour->present_after = false;

        // Depth attachment
        render_target_attachment_config* editor_world_target_depth = &editor_world_pass.target.attachments[1];
        editor_world_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
        editor_world_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        editor_world_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        editor_world_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        editor_world_target_depth->present_after = false;

        if (!renderer_renderpass_create(&editor_world_pass, &editor_world_view.passes[0])) {
            KERROR("World view - Failed to create renderpass '%s'", editor_world_view.passes[0].name);
            return false;
        }

        // Assign function pointers.
        editor_world_view.on_packet_build = render_view_editor_world_on_packet_build;
        editor_world_view.on_packet_destroy = render_view_editor_world_on_packet_destroy;
        editor_world_view.on_render = render_view_editor_world_on_render;
        editor_world_view.on_registered = render_view_editor_world_on_registered;
        editor_world_view.on_destroy = render_view_editor_world_on_destroy;
        editor_world_view.on_resize = render_view_editor_world_on_resize;
        editor_world_view.attachment_target_regenerate = 0;

        darray_push(config->views, editor_world_view);
    }

    // Wireframe view.
    {
        render_view wireframe_view = {0};
        wireframe_view.name = "wireframe";
        wireframe_view.renderpass_count = 1;
        wireframe_view.passes = kallocate(sizeof(renderpass) * wireframe_view.renderpass_count, MEMORY_TAG_ARRAY);

        // Renderpass config.
        renderpass_config wireframe_pass = {0};
        wireframe_pass.name = "Renderpass.Testbed.Wireframe";
        wireframe_pass.clear_colour = (vec4){0.2f, 0.2f, 0.2f, 1.0f};
        wireframe_pass.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG | RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
        wireframe_pass.depth = 1.0f;
        wireframe_pass.stencil = 0;
        wireframe_pass.target.attachment_count = 2;
        wireframe_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * wireframe_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        wireframe_pass.render_target_count = renderer_window_attachment_count_get();

        // Colour attachment
        render_target_attachment_config* wireframe_target_colour = &wireframe_pass.target.attachments[0];
        wireframe_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        wireframe_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        wireframe_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
        wireframe_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        wireframe_target_colour->present_after = false;

        // Depth attachment
        render_target_attachment_config* wireframe_target_depth = &wireframe_pass.target.attachments[1];
        wireframe_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
        wireframe_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        wireframe_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        wireframe_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        wireframe_target_depth->present_after = false;

        if (!renderer_renderpass_create(&wireframe_pass, &wireframe_view.passes[0])) {
            KERROR("World view - Failed to create renderpass '%s'", wireframe_view.passes[0].name);
            return false;
        }

        // Assign function pointers.
        wireframe_view.on_packet_build = render_view_wireframe_on_packet_build;
        wireframe_view.on_packet_destroy = render_view_wireframe_on_packet_destroy;
        wireframe_view.on_render = render_view_wireframe_on_render;
        wireframe_view.on_registered = render_view_wireframe_on_registered;
        wireframe_view.on_destroy = render_view_wireframe_on_destroy;
        wireframe_view.on_resize = render_view_wireframe_on_resize;
        wireframe_view.attachment_target_regenerate = 0;

        darray_push(config->views, wireframe_view);
    }

    // UI view
    {
        render_view ui_view = {0};
        ui_view.name = "ui";
        ui_view.renderpass_count = 1;
        ui_view.passes = kallocate(sizeof(renderpass) * ui_view.renderpass_count, MEMORY_TAG_ARRAY);

        // Renderpass config
        renderpass_config ui_pass;
        ui_pass.name = "Renderpass.Builtin.UI";
        ui_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
        ui_pass.clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
        ui_pass.depth = 1.0f;
        ui_pass.stencil = 0;
        ui_pass.target.attachment_count = 1;
        ui_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * ui_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        ui_pass.render_target_count = renderer_window_attachment_count_get();

        render_target_attachment_config* ui_target_attachment = &ui_pass.target.attachments[0];
        // Colour attachment.
        ui_target_attachment->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        ui_target_attachment->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
        ui_target_attachment->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
        ui_target_attachment->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        ui_target_attachment->present_after = true;

        if (!renderer_renderpass_create(&ui_pass, &ui_view.passes[0])) {
            KERROR("UI view - Failed to create renderpass '%s'", ui_view.passes[0].name);
            return false;
        }

        // Assign function pointers.
        ui_view.on_packet_build = render_view_ui_on_packet_build;
        ui_view.on_packet_destroy = render_view_ui_on_packet_destroy;
        ui_view.on_render = render_view_ui_on_render;
        ui_view.on_registered = render_view_ui_on_registered;
        ui_view.on_destroy = render_view_ui_on_destroy;
        ui_view.on_resize = render_view_ui_on_resize;
        ui_view.attachment_target_regenerate = 0;

        darray_push(config->views, ui_view);
    }

    // Pick pass.
    // TODO: Split this into 2 views and re-enable.
    {
        render_view pick_view = {};
        pick_view.name = "pick";
        pick_view.renderpass_count = 2;
        pick_view.passes = kallocate(sizeof(renderpass) * pick_view.renderpass_count, MEMORY_TAG_ARRAY);

        // World pick pass
        renderpass_config world_pick_pass = {0};
        world_pick_pass.name = "Renderpass.Builtin.WorldPick";
        world_pick_pass.clear_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};  // HACK: clearing to white for better visibility// TODO: Clear to black, as 0 is invalid id.
        world_pick_pass.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG | RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG;
        world_pick_pass.depth = 1.0f;
        world_pick_pass.stencil = 0;
        world_pick_pass.render_target_count = 1;  // NOTE: Not triple-buffering this.
        world_pick_pass.target.attachment_count = 2;
        world_pick_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * world_pick_pass.target.attachment_count, MEMORY_TAG_ARRAY);

        // Colour attachment
        render_target_attachment_config* world_pick_pass_colour = &world_pick_pass.target.attachments[0];
        world_pick_pass_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        world_pick_pass_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;  // Obtain the attachment from the view.
        world_pick_pass_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        world_pick_pass_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        world_pick_pass_colour->present_after = false;

        // Depth attachment
        render_target_attachment_config* world_pick_pass_depth = &world_pick_pass.target.attachments[1];
        world_pick_pass_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
        world_pick_pass_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;  // Obtain the attachment from the view.
        world_pick_pass_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        world_pick_pass_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        world_pick_pass_depth->present_after = false;

        if (!renderer_renderpass_create(&world_pick_pass, &pick_view.passes[0])) {
            KERROR("Pick view - Failed to create renderpass '%s'", pick_view.passes[0].name);
            return false;
        }

        // UI pick pass
        renderpass_config ui_pick_pass = {0};
        ui_pick_pass.name = "Renderpass.Builtin.UIPick";
        ui_pick_pass.clear_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
        ui_pick_pass.clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
        ui_pick_pass.depth = 1.0f;
        ui_pick_pass.stencil = 0;
        ui_pick_pass.target.attachment_count = 1;
        ui_pick_pass.target.attachments = kallocate(sizeof(render_target_attachment_config) * ui_pick_pass.target.attachment_count, MEMORY_TAG_ARRAY);
        ui_pick_pass.render_target_count = 1;  // NOTE: Not triple-buffering this.

        render_target_attachment_config* ui_pick_pass_attachment = &ui_pick_pass.target.attachments[0];
        ui_pick_pass_attachment->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
        // Obtain the attachment from the view.
        ui_pick_pass_attachment->source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;
        ui_pick_pass_attachment->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
        // Need to store it so it can be sampled afterward.
        ui_pick_pass_attachment->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        ui_pick_pass_attachment->present_after = false;

        if (!renderer_renderpass_create(&ui_pick_pass, &pick_view.passes[1])) {
            KERROR("Pick view - Failed to create renderpass '%s'", pick_view.passes[1].name);
            return false;
        }

        // Assign function pointers.
        pick_view.on_packet_build = render_view_pick_on_packet_build;
        pick_view.on_packet_destroy = render_view_pick_on_packet_destroy;
        pick_view.on_render = render_view_pick_on_render;
        pick_view.on_registered = render_view_pick_on_registered;
        pick_view.on_destroy = render_view_pick_on_destroy;
        pick_view.on_resize = render_view_pick_on_resize;
        pick_view.attachment_target_regenerate = render_view_pick_attachment_target_regenerate;

        darray_push(config->views, pick_view);
    }

    return true;
} */

static b8 load_main_scene(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    // Load up config file
    // TODO: clean up resource.
    resource simple_scene_resource;
    if (!resource_system_load("test_scene", RESOURCE_TYPE_SIMPLE_SCENE, 0, &simple_scene_resource)) {
        KERROR("Failed to load scene file, check above logs.");
        return false;
    }

    simple_scene_config* scene_config = (simple_scene_config*)simple_scene_resource.data;

    // TODO: temp load/prepare stuff
    if (!simple_scene_create(scene_config, &state->main_scene)) {
        KERROR("Failed to create main scene");
        return false;
    }

    // Add objects to scene

    // // Load up a cube configuration, and load geometry from it.
    // mesh_config cube_0_config = {0};
    // cube_0_config.geometry_count = 1;
    // cube_0_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    // cube_0_config.g_configs[0] = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, "test_cube", "test_material");

    // if (!mesh_create(cube_0_config, &state->meshes[0])) {
    //     KERROR("Failed to create mesh for cube 0");
    //     return false;
    // }
    // state->meshes[0].transform = transform_create();
    // simple_scene_add_mesh(&state->main_scene, "test_cube_0", &state->meshes[0]);

    // // Second cube
    // mesh_config cube_1_config = {0};
    // cube_1_config.geometry_count = 1;
    // cube_1_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    // cube_1_config.g_configs[0] = geometry_system_generate_cube_config(5.0f, 5.0f, 5.0f, 1.0f, 1.0f, "test_cube_2", "test_material");

    // if (!mesh_create(cube_1_config, &state->meshes[1])) {
    //     KERROR("Failed to create mesh for cube 0");
    //     return false;
    // }
    // state->meshes[1].transform = transform_from_position((vec3){10.0f, 0.0f, 1.0f});
    // transform_set_parent(&state->meshes[1].transform, &state->meshes[0].transform);

    // simple_scene_add_mesh(&state->main_scene, "test_cube_1", &state->meshes[1]);

    // // Third cube!
    // mesh_config cube_2_config = {0};
    // cube_2_config.geometry_count = 1;
    // cube_2_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    // cube_2_config.g_configs[0] = geometry_system_generate_cube_config(2.0f, 2.0f, 2.0f, 1.0f, 1.0f, "test_cube_2", "test_material");

    // if (!mesh_create(cube_2_config, &state->meshes[2])) {
    //     KERROR("Failed to create mesh for cube 0");
    //     return false;
    // }
    // state->meshes[2].transform = transform_from_position((vec3){5.0f, 0.0f, 1.0f});
    // transform_set_parent(&state->meshes[2].transform, &state->meshes[1].transform);

    // simple_scene_add_mesh(&state->main_scene, "test_cube_2", &state->meshes[2]);

    // Initialize
    if (!simple_scene_initialize(&state->main_scene)) {
        KERROR("Failed initialize main scene, aborting game.");
        return false;
    }

    state->p_light_1 = simple_scene_point_light_get(&state->main_scene, "point_light_1");

    // Actually load the scene.
    return simple_scene_load(&state->main_scene);
}
