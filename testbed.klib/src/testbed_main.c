#include "testbed_main.h"

#include <containers/darray.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/input.h>
#include <core/kvar.h>
#include <core/metrics.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <input_types.h>
#include <logger.h>
#include <math/geometry_2d.h>
#include <math/geometry_3d.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/camera.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <renderer/rendergraph.h>
#include <renderer/viewport.h>
#include <resources/terrain.h>
#include <strings/kstring.h>
#include <systems/camera_system.h>
#include <systems/texture_system.h>
#include <time/kclock.h>

#include "application/application_config.h"
#include "assets/kasset_types.h"
#include "core_audio_types.h"
#include "game_state.h"

// Standard UI.
#include <controls/sui_button.h>
#include <controls/sui_label.h>
#include <controls/sui_panel.h>
#include <standard_ui_plugin_main.h>
#include <standard_ui_system.h>

// Audio
#include <audio/audio_frontend.h>

// TODO: Editor temp
#include "editor/editor_gizmo.h"
#include "editor/editor_gizmo_rendergraph_node.h"
#include <resources/debug/debug_box3d.h>
#include <resources/debug/debug_line3d.h>
#include <resources/water_plane.h>

// TODO: temp
#include <identifiers/identifier.h>
#include <resources/scene.h>
#include <resources/skybox.h>
#include <systems/light_system.h>
#include <systems/material_system.h>
#include <systems/shader_system.h>
// Standard ui
#include <core/systems_manager.h>
#include <standard_ui_system.h>

#ifdef KOHI_DEBUG
#    include "debug_console.h"
#endif
// Game code.
#include "game_commands.h"
#include "game_keybinds.h"
// TODO: end temp

#include "kresources/kresource_types.h"
#include "platform/platform.h"
#include "plugins/plugin_types.h"
#include "renderer/rendergraph_nodes/debug_rendergraph_node.h"
#include "renderer/rendergraph_nodes/forward_rendergraph_node.h"
#include "renderer/rendergraph_nodes/shadow_rendergraph_node.h"
#include "rendergraph_nodes/ui_rendergraph_node.h"
#include "strings/kname.h"
#include "systems/kresource_system.h"
#include "systems/plugin_system.h"
#include "systems/timeline_system.h"
#include "testbed.klib_version.h"

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
static b8 save_main_scene(struct application* game_inst);

static f32 get_engine_delta_time(void) {
    khandle engine = timeline_system_get_engine();
    return timeline_system_delta_get(engine);
}

static void clear_debug_objects(struct application* game_inst) {
    application_state* state = (application_state*)game_inst->state;

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
    application_state* state = (application_state*)game_inst->state;

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
    application_state* state = (application_state*)game_inst->state;

    if (code == EVENT_CODE_DEBUG0) {
        // Does nothing for now.
        return true;
    } else if (code == EVENT_CODE_DEBUG1) {
        if (state->main_scene.state == SCENE_STATE_UNINITIALIZED) {
            KDEBUG("Loading main scene...");
            if (!load_main_scene(game_inst)) {
                KERROR("Error loading main scene");
            }
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG5) {
        if (state->main_scene.state == SCENE_STATE_LOADED) {
            KDEBUG("Saving main scene...");
            if (!save_main_scene(game_inst)) {
                KERROR("Error saving main scene");
            }
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG2) {
        if (state->main_scene.state == SCENE_STATE_LOADED) {
            KDEBUG("Unloading scene...");

            scene_unload(&state->main_scene, false);
            clear_debug_objects(game_inst);
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG3) {
        if (kaudio_is_valid(state->audio_system, state->test_sound)) {
            // Cycle between first 5 channels.
            static i8 channel_id = -1;
            channel_id++;
            channel_id = channel_id % 5;
            KTRACE("Playing sound on channel %u", channel_id);
            kaudio_play(state->audio_system, state->test_sound, channel_id);
        }
    } else if (code == EVENT_CODE_DEBUG4) {
        /* if (kaudio_is_valid(state->audio_system, state->test_loop_sound)) {
            static b8 playing = true;
            playing = !playing;
            if (playing) {
                // Play on channel 6
                // TODO: pipe this through an emitter node in the scene.
                kaudio_play(state->audio_system, state->test_loop_sound, 6);
                // Set this to loop.
                kaudio_looping_set(state->audio_system, state->test_loop_sound, true);
            } else {
                // Stop channel 6.
                kaudio_channel_stop(state->audio_system, 6);
            }
        } */
    }

    return false;
}

static b8 game_on_drag(u16 code, void* sender, void* listener_inst, event_context context) {
    i16 x = context.data.i16[0];
    i16 y = context.data.i16[1];
    u16 drag_button = context.data.u16[2];
    application_state* state = (application_state*)listener_inst;

    // Only care about left button drags.
    if (drag_button == MOUSE_BUTTON_LEFT) {
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

    return false; // Let other handlers handle.
}

b8 game_on_button(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_BUTTON_PRESSED) {
        //
    } else if (code == EVENT_CODE_BUTTON_RELEASED) {
        u16 button = context.data.u16[0];
        switch (button) {
        case MOUSE_BUTTON_LEFT: {
            i16 x = context.data.i16[1];
            i16 y = context.data.i16[2];
            application_state* state = (application_state*)listener_inst;

            // If the scene isn't loaded, don't do anything else.
            if (state->main_scene.state != SCENE_STATE_LOADED) {
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
                        // TODO: Use handle index to identify?
                        KINFO("Hit! id: %u, dist: %f", hit->node_handle.handle_index, hit->distance);

                        // Create a debug line where the ray cast starts and ends (at the intersection).
                        debug_line3d test_line;
                        debug_line3d_create(r.origin, hit->position, khandle_invalid(), &test_line);
                        debug_line3d_initialize(&test_line);
                        debug_line3d_load(&test_line);
                        // Yellow for hits.
                        debug_line3d_colour_set(&test_line, (vec4){1.0f, 1.0f, 0.0f, 1.0f});

                        darray_push(state->test_lines, test_line);

                        // Create a debug box to show the intersection point.
                        debug_box3d test_box;

                        debug_box3d_create((vec3){0.1f, 0.1f, 0.1f}, khandle_invalid(), &test_box);
                        debug_box3d_initialize(&test_box);
                        debug_box3d_load(&test_box);

                        // These aren't parented to anything, so the local transform _is_ the world transform.
                        // TODO: Need to think of a way to make this more automatic.
                        test_box.xform = xform_from_position(hit->position);
                        test_box.parent_xform = khandle_invalid();
                        xform_calculate_local(test_box.xform);
                        xform_world_set(test_box.xform, xform_local_get(test_box.xform));

                        darray_push(state->test_boxes, test_box);

                        // Object selection
                        if (i == 0) {
                            state->selection.node_handle = hit->node_handle;
                            state->selection.xform_handle = hit->xform_handle; //  scene_transform_get_by_id(&state->main_scene, hit->unique_id);
                            state->selection.xform_parent_handle = hit->xform_parent_handle;
                            if (!khandle_is_invalid(state->selection.xform_handle)) {
                                // NOTE: is handle index what we should identify by?
                                KINFO("Selected object id %u", hit->node_handle.handle_index);
                                // state->gizmo.selected_xform = state->selection.xform;
                                editor_gizmo_selected_transform_set(&state->gizmo, state->selection.xform_handle, state->selection.xform_parent_handle);
                                // transform_parent_set(&state->gizmo.xform, state->selection.xform);
                            }
                        }
                    }
                } else {
                    KINFO("No hit");

                    // Create a debug line where the ray cast starts and continues to.
                    debug_line3d test_line;
                    debug_line3d_create(r.origin, vec3_add(r.origin, vec3_mul_scalar(r.direction, 100.0f)), khandle_invalid(), &test_line);
                    debug_line3d_initialize(&test_line);
                    debug_line3d_load(&test_line);
                    // Magenta for non-hits.
                    debug_line3d_colour_set(&test_line, (vec4){1.0f, 0.0f, 1.0f, 1.0f});

                    darray_push(state->test_lines, test_line);

                    if (khandle_is_invalid(state->selection.xform_handle)) {
                        KINFO("Object deselected.");
                        state->selection.xform_handle = khandle_invalid();
                        state->selection.node_handle = khandle_invalid();
                        state->selection.xform_parent_handle = khandle_invalid();

                        editor_gizmo_selected_transform_set(&state->gizmo, state->selection.xform_handle, state->selection.xform_parent_handle);
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
    if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(MOUSE_BUTTON_LEFT)) {
        i16 x = context.data.i16[0];
        i16 y = context.data.i16[1];

        application_state* state = (application_state*)listener_inst;

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
    return false; // Allow other event handlers to recieve this event.
}

static void sui_test_button_on_click(struct standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        KDEBUG("Clicked '%s'!", self->name);
    }
}

u64 application_state_size(void) {
    return sizeof(application_state);
}

b8 application_boot(struct application* game_inst) {
    KINFO("Booting testbed (%s)...", KVERSION);

    // Allocate the game state.
    game_inst->state = kallocate(sizeof(application_state), MEMORY_TAG_GAME);
    application_state* state = game_inst->state;
    state->running = false;

    application_config* config = &game_inst->app_config;

    config->frame_allocator_size = MEBIBYTES(64);
    config->app_frame_data_size = sizeof(testbed_application_frame_data);

    // Register custom rendergraph nodes, systems, etc.
    if (!editor_gizmo_rendergraph_node_register_factory()) {
        KERROR("Failed to register editor_gizmo rendergraph node.");
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

    application_state* state = (application_state*)game_inst->state;
    state->audio_system = engine_systems_get()->audio_system;

    // Get the standard ui plugin.
    state->sui_plugin = plugin_system_get(engine_systems_get()->plugin_system, "kohi.plugin.ui.standard");
    state->sui_plugin_state = state->sui_plugin->plugin_state;
    state->sui_state = state->sui_plugin_state->state;
    standard_ui_state* sui_state = state->sui_state;

#ifdef KOHI_DEBUG
    if (!debug_console_create(state->sui_state, &((application_state*)game_inst->state)->debug_console)) {
        KERROR("Failed to create debug console.");
    }
#endif

    application_register_events(game_inst);

    // Register resource loaders.
    // FIXME: Audio loader via plugin.
    /* resource_system_loader_register(audio_resource_loader_create()); */

    // Pick out rendergraph(s) config from app config, create/init them
    // from here, save off to state.
    application_config* config = &game_inst->app_config;
    u32 rendergraph_count = darray_length(config->rendergraphs);
    if (rendergraph_count < 1) {
        KERROR("At least one rendergraph is required in order to run this application.");
        return false;
    }

    b8 rendergraph_found = false;
    for (u32 i = 0; i < rendergraph_count; ++i) {
        application_rendergraph_config* rg_config = &config->rendergraphs[i];
        if (strings_equali("forward_graph", rg_config->name)) {
            // Get colourbuffer and depthbuffer from the currently active window.
            kwindow* current_window = engine_active_window_get();
            kresource_texture* global_colourbuffer = current_window->renderer_state->colourbuffer;
            kresource_texture* global_depthbuffer = current_window->renderer_state->depthbuffer;

            // Create the rendergraph.
            if (!rendergraph_create(rg_config->configuration_str, global_colourbuffer, global_depthbuffer, &state->forward_graph)) {
                KERROR("Failed to create forward_graph. See logs for details.");
                return false;
            }
            rendergraph_found = true;
            break;
        }
    }
    if (!rendergraph_found) {
        KERROR("No rendergraph config named 'forward_graph' was found, but is required for this application.");
        return false;
    }

    // TODO: Internalize this step?
    // Might need to happen after the rg acquires its resources.
    if (!rendergraph_finalize(&state->forward_graph)) {
        KERROR("Failed to finalize rendergraph. See logs for details");
        return false;
    }

    // Invalid handle = no selection.
    state->selection.xform_handle = khandle_invalid();

#ifdef KOHI_DEBUG
    debug_console_load(&state->debug_console);
#endif

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
    if (!viewport_create(ui_vp_rect, 0.0f, 0.0f, 100.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC, &state->ui_viewport)) {
        KERROR("Failed to create UI viewport. Cannot start application.");
        return false;
    }

    // TODO: test
    rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, 1280.0f - 40.0f, 720.0f - 40.0f);
    if (!viewport_create(world_vp_rect2, deg_to_rad(45.0f), 0.01f, 10.0f, RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE, &state->world_viewport2)) {
        KERROR("Failed to create world viewport 2. Cannot start application.");
        return false;
    }

    // Setup the clear colour.
    renderer_clear_colour_set(engine_systems_get()->renderer_system, (vec4){0.0f, 0.0f, 0.2f, 1.0f});

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

    // FIXME: set in debug3d rg node. Might want a way to reference just the geometry,
    // and not have to maintain a pointer in this way.
    /* editor_rendergraph_gizmo_set(&state->editor_graph, &state->gizmo); */
    // World meshes

    // Create test ui text objects
    // black background text
    if (!sui_label_control_create(sui_state, "testbed_mono_test_text_black", FONT_TYPE_BITMAP, kname_create("Ubuntu Mono 21px"), 21, "test text 123,\n\tyo!", &state->test_text_black)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    } else {
        sui_label_colour_set(sui_state, &state->test_text_black, (vec4){0, 0, 0, 1});
        if (!sui_label_control_load(sui_state, &state->test_text_black)) {
            KERROR("Failed to load test text.");
        } else {
            if (!standard_ui_system_register_control(sui_state, &state->test_text_black)) {
                KERROR("Unable to register control.");
            } else {
                if (!standard_ui_system_control_add_child(sui_state, 0, &state->test_text_black)) {
                    KERROR("Failed to parent test text.");
                } else {
                    state->test_text_black.is_active = true;
                    if (!standard_ui_system_update_active(sui_state, &state->test_text_black)) {
                        KERROR("Unable to update active state.");
                    }
                }
            }
        }
    }
    if (!sui_label_control_create(sui_state, "testbed_mono_test_text", FONT_TYPE_BITMAP, kname_create("Ubuntu Mono 21px"), 21, "test text 123,\n\tyo!", &state->test_text)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    } else {
        if (!sui_label_control_load(sui_state, &state->test_text)) {
            KERROR("Failed to load test text.");
        } else {
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
    sui_control_position_set(sui_state, &state->test_text, vec3_create(20, state->height - 75, 0));
    sui_control_position_set(sui_state, &state->test_text, vec3_create(21, state->height - 74, 0));

    // Standard ui stuff.
    if (!sui_panel_control_create(sui_state, "test_panel", (vec2){300.0f, 300.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.5f}, &state->test_panel)) {
        KERROR("Failed to create test panel.");
    } else {
        if (!sui_panel_control_load(sui_state, &state->test_panel)) {
            KERROR("Failed to load test panel.");
        } else {
            xform_translate(state->test_panel.xform, (vec3){950, 350});
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

    if (!sui_button_control_create(sui_state, "test_button", &state->test_button)) {
        KERROR("Failed to create test button.");
    } else {
        // Assign a click handler.
        state->test_button.on_click = sui_test_button_on_click;

        // Move and rotate it some.
        // quat rotation = quat_from_axis_angle((vec3){0, 0, 1}, deg_to_rad(-45.0f), false);
        // transform_translate_rotate(&state->test_button.xform, (vec3){50, 50, 0}, rotation);

        if (!sui_button_control_load(sui_state, &state->test_button)) {
            KERROR("Failed to load test button.");
        } else {
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

    if (!sui_label_control_create(sui_state, "testbed_UTF_test_sys_text", FONT_TYPE_SYSTEM, kname_create("Noto Sans CJK JP"), 31, "Press 'L' to load a \n\tscene!\n\n\tこんにちは 한", &state->test_sys_text)) {
        KERROR("Failed to load basic ui system text.");
        return false;
    } else {
        if (!sui_label_control_load(sui_state, &state->test_sys_text)) {
            KERROR("Failed to load test system text.");
        } else {
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
    sui_control_position_set(sui_state, &state->test_sys_text, vec3_create(950, 450, 0));
    // TODO: end temp load/prepare stuff

    state->world_camera = camera_system_acquire("world");
    camera_position_set(state->world_camera, (vec3){-3.94f, 4.26f, 15.79f});
    camera_rotation_euler_set(state->world_camera, (vec3){-11.505f, -74.994f, 0.0f});

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

    // Audio tests

    // Load up a test audio file.
    if (!kaudio_acquire(state->audio_system, kname_create("Test_Audio"), kname_create("Testbed"), false, KAUDIO_SPACE_2D, &state->test_sound)) {
        KERROR("Failed to load test audio file.");
    }
    /* // Looping audio file.
    if (!kaudio_acquire(state->audio_system, kname_create("Fire_loop"), kname_create("Testbed"), false, &state->test_loop_sound)) {
        KERROR("Failed to load test looping audio file.");
    } */
    // Test music
    if (!kaudio_acquire(state->audio_system, kname_create("Woodland Fantasy"), kname_create("Testbed"), true, KAUDIO_SPACE_2D, &state->test_music)) {
        KERROR("Failed to load test music file.");
    }

    // Setup a test emitter.
    /* state->test_emitter.file = state->test_loop_sound;
    state->test_emitter.volume = 1.0f;
    state->test_emitter.looping = true;
    state->test_emitter.falloff = 1.0f;
    state->test_emitter.position = vec3_create(10.0f, 0.8f, 20.0f); */

    // Set some channel volumes.
    kaudio_master_volume_set(state->audio_system, 0.9f);
    kaudio_channel_volume_set(state->audio_system, 0, 1.0f);
    kaudio_channel_volume_set(state->audio_system, 1, 0.75f);
    kaudio_channel_volume_set(state->audio_system, 2, 0.50f);
    kaudio_channel_volume_set(state->audio_system, 3, 0.25);
    kaudio_channel_volume_set(state->audio_system, 4, 0.0f);
    kaudio_channel_volume_set(state->audio_system, 7, 0.9f);

    // TODO: emitters
    // Try playing the emitter.
    /* if (!audio_system_channel_emitter_play(6, &state->test_emitter)) {
        KERROR("Failed to play test emitter.");
    }*/

    // Play the test music on channel 7.
    /* kaudio_play(state->audio_system, state->test_music, 7); */

    if (!rendergraph_initialize(&state->forward_graph)) {
        KERROR("Failed to initialize rendergraph. See logs for details.");
        return false;
    }

    if (!rendergraph_load_resources(&state->forward_graph)) {
        KERROR("Failed to load resources for rendergraph. See logs for details.");
        return false;
    }

    state->running = true;

    return true;
}

b8 application_update(struct application* game_inst, struct frame_data* p_frame_data) {
    testbed_application_frame_data* app_frame_data = (testbed_application_frame_data*)p_frame_data->application_frame_data;
    if (!app_frame_data) {
        return true;
    }

    application_state* state = (application_state*)game_inst->state;
    if (!state->running) {
        return true;
    }

    kclock_start(&state->update_clock);

    // TODO: testing resize
    static f32 button_height = 50.0f;
    button_height = 50.0f + (ksin(get_engine_delta_time()) * 20.0f);
    sui_button_control_height_set(state->sui_state, &state->test_button, (i32)button_height);

    // Update the bitmap text with camera position. NOTE: just using the default camera for now.
    vec3 pos = camera_position_get(state->world_camera);
    vec3 rot = camera_rotation_euler_get(state->world_camera);

    viewport* view_viewport = &state->world_viewport;

    f32 near_clip = view_viewport->near_clip;
    f32 far_clip = view_viewport->far_clip;

    if (state->main_scene.state == SCENE_STATE_LOADED) {
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
                KCLAMP(ksin(get_engine_delta_time()) * 75.0f + 50.0f, 0.0f, 100.0f),
                KCLAMP(ksin(get_engine_delta_time() - (K_2PI / 3)) * 75.0f + 50.0f, 0.0f, 100.0f),
                KCLAMP(ksin(get_engine_delta_time() - (K_4PI / 3)) * 75.0f + 50.0f, 0.0f, 100.0f),
                1.0f};
            state->p_light_1->data.position.z = 20.0f + ksin(get_engine_delta_time());

            // Make the audio emitter follow it.
            // TODO: Get emitter from scene and change its position.
            /* state->test_emitter.position = vec3_from_vec4(state->p_light_1->data.position); */
        }
    } else if (state->main_scene.state == SCENE_STATE_UNLOADING) {
        // A final update call is required to unload the scene in this state.
        scene_update(&state->main_scene, p_frame_data);
    } else if (state->main_scene.state == SCENE_STATE_UNLOADED) {
        KTRACE("Destroying main scene.");
        // Unloading complete, destroy it.
        scene_destroy(&state->main_scene);
    }

    // Track allocation differences.
    state->prev_alloc_count = state->alloc_count;
    state->alloc_count = get_memory_alloc_count();

    // Only track these things once actually running.
    if (state->running) {
        // Also tack on current mouse state.
        b8 left_down = input_is_button_down(MOUSE_BUTTON_LEFT);
        b8 right_down = input_is_button_down(MOUSE_BUTTON_RIGHT);
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
        static f32 total_avg = 0; // total average across the frame

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
        char* text_buffer = string_format(
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

        // Update the text control.
        sui_label_text_set(state->sui_state, &state->test_text, text_buffer);
        sui_label_text_set(state->sui_state, &state->test_text_black, text_buffer);
        string_free(text_buffer);
    }

#ifdef KOHI_DEBUG
    debug_console_update(&((application_state*)game_inst->state)->debug_console);
#endif

    vec3 forward = camera_forward(state->world_camera);
    vec3 up = camera_up(state->world_camera);
    kaudio_system_listener_orientation_set(engine_systems_get()->audio_system, pos, forward, up);

    kclock_update(&state->update_clock);
    state->last_update_elapsed = state->update_clock.elapsed;

    return true;
}

b8 application_prepare_frame(struct application* app_inst, struct frame_data* p_frame_data) {
    application_state* state = (application_state*)app_inst->state;
    if (!state->running) {
        return false;
    }

    kclock_start(&state->prepare_clock);

    scene* scene = &state->main_scene;
    camera* current_camera = state->world_camera;
    viewport* current_viewport = &state->world_viewport;

    // HACK: Using the first light in the collection for now.
    // TODO: Support for multiple directional lights with priority sorting.
    directional_light* dir_light = scene->dir_lights ? &scene->dir_lights[0] : 0;

    // Global setup
    f32 near = current_viewport->near_clip;
    f32 far = dir_light ? dir_light->data.shadow_distance + dir_light->data.shadow_fade_distance : 0;
    f32 clip_range = far - near;

    f32 min_z = near;
    f32 max_z = near + clip_range;
    f32 range = max_z - min_z;
    f32 ratio = max_z / min_z;

    f32 cascade_split_multiplier = dir_light ? dir_light->data.shadow_split_mult : 0.95f;

    // Calculate splits based on view camera frustum.
    vec4 splits;
    for (u32 c = 0; c < MATERIAL_MAX_SHADOW_CASCADES; c++) {
        f32 p = (c + 1) / (f32)MATERIAL_MAX_SHADOW_CASCADES;
        f32 log = min_z * kpow(ratio, p);
        f32 uniform = min_z + range * p;
        f32 d = cascade_split_multiplier * (log - uniform) + uniform;
        splits.elements[c] = (d - near) / clip_range;
    }

    // Default values to use in the event there is no directional light.
    // These are required because the scene pass needs them.
    mat4 shadow_camera_view_projections[MATERIAL_MAX_SHADOW_CASCADES];
    for (u32 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
        shadow_camera_view_projections[i] = mat4_identity();
    }

    // TODO: Anything to do here?
    // FIXME: Cache this instead of looking up every frame.
    u32 node_count = state->forward_graph.node_count;
    for (u32 i = 0; i < node_count; ++i) {
        rendergraph_node* node = &state->forward_graph.nodes[i];
        if (strings_equali(node->name, "sui")) {
            ui_rendergraph_node_set_atlas(node, state->sui_state->atlas_texture);

            // We have the one.
            ui_rendergraph_node_set_viewport_and_matrices(
                node,
                state->ui_viewport,
                mat4_identity(),
                state->ui_viewport.projection);

            // Gather SUI render data.
            standard_ui_render_data render_data = {0};

            // Renderables.
            render_data.renderables = darray_create_with_allocator(standard_ui_renderable, &p_frame_data->allocator);
            if (!standard_ui_system_render(state->sui_state, 0, p_frame_data, &render_data)) {
                KERROR("The standard ui system failed to render.");
            }
            ui_rendergraph_node_set_render_data(node, render_data);
        } else if (strings_equali(node->name, "forward")) {
            // Ensure internal lists, etc. are reset.
            forward_rendergraph_node_reset(node);
            forward_rendergraph_node_viewport_set(node, state->world_viewport);
            forward_rendergraph_node_camera_projection_set(
                node,
                current_camera,
                current_viewport->projection);

            // Tell our scene to generate relevant render data if it is loaded.
            if (scene->state == SCENE_STATE_LOADED) {
                // Only render if the scene is loaded.

                // SKYBOX
                // HACK: Just use the first one for now.
                // TODO: Support for multiple skyboxes, possibly transition between them.
                u32 skybox_count = darray_length(scene->skyboxes);
                forward_rendergraph_node_set_skybox(node, skybox_count ? &scene->skyboxes[0] : 0);

                // SCENE
                scene_render_frame_prepare(scene, p_frame_data);

                // Pass over shadow map "camera" view and projection matrices (one per cascade).
                for (u32 c = 0; c < MATERIAL_MAX_SHADOW_CASCADES; c++) {
                    forward_rendergraph_node_cascade_data_set(
                        node,
                        (near + splits.elements[c] * clip_range) * 1.0f, // splits.elements[c]
                        shadow_camera_view_projections[c],
                        c);
                }
                // Ensure the render mode is set.
                forward_rendergraph_node_render_mode_set(node, state->render_mode);

                // Tell it about the directional light.
                forward_rendergraph_node_directional_light_set(node, dir_light);

                // HACK: use the skybox cubemap as the irradiance texture for now.
                // HACK: #2 Support for multiple skyboxes, but using the first one for now.
                // DOUBLE HACK!!!
                // TODO: Support multiple skyboxes/irradiance maps.
                forward_rendergraph_node_irradiance_texture_set(node, p_frame_data, scene->skyboxes ? scene->skyboxes[0].cubemap : texture_system_request(kname_create(DEFAULT_CUBE_TEXTURE_NAME), INVALID_KNAME, 0, 0));

                // Camera frustum culling and count
                viewport* v = current_viewport;
                vec3 forward = camera_forward(current_camera);
                vec3 target = vec3_add(current_camera->position, vec3_mul_scalar(forward, far));
                vec3 up = camera_up(current_camera);
                // TODO: move frustum to be managed by camera it is attached to.
                frustum camera_frustum = frustum_create(&current_camera->position, &target,
                                                        &up, v->rect.width / v->rect.height, v->fov, v->near_clip, v->far_clip);

                p_frame_data->drawn_mesh_count = 0;

                u32 geometry_count = 0;
                geometry_render_data* geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);

                // Query the scene for static meshes using the camera frustum.
                if (!scene_mesh_render_data_query(
                        scene,
                        0, //&camera_frustum, // HACK: disabling frustum culling for now.
                        current_camera->position,
                        p_frame_data,
                        &geometry_count, &geometries)) {
                    KERROR("Failed to query scene pass meshes.");
                }

                // Track the number of meshes drawn in the forward pass.
                p_frame_data->drawn_mesh_count = geometry_count;
                // Tell the node about them.
                forward_rendergraph_node_static_geometries_set(node, p_frame_data, geometry_count, geometries);

                // Add terrain(s)
                u32 terrain_geometry_count = 0;
                geometry_render_data* terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);

                // Query the scene for terrain meshes using the camera frustum.
                if (!scene_terrain_render_data_query(
                        scene,
                        0, //&camera_frustum, // HACK: disabling frustum culling for now.
                        current_camera->position,
                        p_frame_data,
                        &terrain_geometry_count, &terrain_geometries)) {
                    KERROR("Failed to query scene pass terrain geometries.");
                }

                // TODO: Separate counter for terrain geometries.
                p_frame_data->drawn_mesh_count += terrain_geometry_count;
                // Tell the node about them.
                forward_rendergraph_node_terrain_geometries_set(node, p_frame_data, terrain_geometry_count, terrain_geometries);

                // Get the count of planes, then the planes themselves.
                u32 water_plane_count = 0;
                if (!scene_water_plane_query(scene, &camera_frustum, current_camera->position, p_frame_data, &water_plane_count, 0)) {
                    KERROR("Failed to query scene for water planes.");
                }
                water_plane** planes = water_plane_count ? darray_reserve_with_allocator(water_plane*, water_plane_count, &p_frame_data->allocator) : 0;
                if (!scene_water_plane_query(scene, &camera_frustum, current_camera->position, p_frame_data, &water_plane_count, &planes)) {
                    KERROR("Failed to query scene for water planes.");
                }

                // Pass the planes to the node.
                if (!forward_rendergraph_node_water_planes_set(node, p_frame_data, water_plane_count, planes)) {
                    // NOTE: Not going to abort the whole graph for this failure, but will bleat about it loudly.
                    KERROR("Failed to set water planes for water_plane rendergraph node.");
                }

            } else {
                // Scene not loaded.
                forward_rendergraph_node_set_skybox(node, 0);
                forward_rendergraph_node_irradiance_texture_set(node, p_frame_data, 0);

                // Do not run these passes if the scene is not loaded.
                // graph->scene_pass.pass_data.do_execute = false;
                // graph->shadowmap_pass.pass_data.do_execute = false;
                forward_rendergraph_node_water_planes_set(node, p_frame_data, 0, 0);
                forward_rendergraph_node_static_geometries_set(node, p_frame_data, 0, 0);
                forward_rendergraph_node_terrain_geometries_set(node, p_frame_data, 0, 0);
            }
        } else if (strings_equali(node->name, "shadow")) {
            // Shadowmap pass - only runs if there is a directional light.
            // TODO: Will also need to run for point lights when implemented.
            if (dir_light) {
                f32 last_split_dist = 0.0f;

                // Obtain the light direction.
                vec3 light_dir = vec3_normalized(vec3_from_vec4(dir_light->data.direction));

                // Tell it about the directional light.
                shadow_rendergraph_node_directional_light_set(node, dir_light);

                // frustum culling_frustum;
                vec3 culling_center;
                f32 culling_radius;

                // Get the view-projection matrix
                mat4 shadow_dist_projection = mat4_perspective(
                    current_viewport->fov,
                    current_viewport->rect.width / current_viewport->rect.height,
                    near,
                    far);
                mat4 cam_view_proj = mat4_transposed(mat4_mul(camera_view_get(current_camera), shadow_dist_projection));

                // Pass over shadow map "camera" view and projection matrices (one per cascade).
                for (u32 c = 0; c < MATERIAL_MAX_SHADOW_CASCADES; c++) {

                    // Get the world-space corners of the view frustum.
                    vec4 corners[8] = {
                        {-1.0f, +1.0f, 0.0f, 1.0f},
                        {+1.0f, +1.0f, 0.0f, 1.0f},
                        {+1.0f, -1.0f, 0.0f, 1.0f},
                        {-1.0f, -1.0f, 0.0f, 1.0f},

                        {-1.0f, +1.0f, 1.0f, 1.0f},
                        {+1.0f, +1.0f, 1.0f, 1.0f},
                        {+1.0f, -1.0f, 1.0f, 1.0f},
                        {-1.0f, -1.0f, 1.0f, 1.0f}};

                    mat4 inv_cam = mat4_inverse(cam_view_proj);
                    for (u32 j = 0; j < 8; ++j) {
                        vec4 inv_corner = mat4_mul_vec4(inv_cam, corners[j]);
                        corners[j] = (vec4_div_scalar(inv_corner, inv_corner.w));
                    }

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
                    center = vec3_div_scalar(center, 8.0f); // size
                    if (c == MATERIAL_MAX_SHADOW_CASCADES - 1) {
                        culling_center = center;
                    }

                    // Get the furthest-out point from the center and use that as the extents.
                    f32 radius = 0.0f;
                    for (u32 i = 0; i < 8; ++i) {
                        f32 distance = vec3_distance(vec3_from_vec4(corners[i]), center);
                        radius = KMAX(radius, distance);
                    }
                    radius = kceil(radius * 16.0f) / 16.0f;

                    if (c == MATERIAL_MAX_SHADOW_CASCADES - 1) {
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
                    vec3 shadow_camera_position = vec3_sub(center, vec3_mul_scalar(light_dir, -extents.min.z));
                    mat4 light_view = mat4_look_at(shadow_camera_position, center, vec3_up());

                    // Generate ortho projection based on extents.
                    mat4 light_ortho = mat4_orthographic(extents.min.x, extents.max.x, extents.min.y, extents.max.y, 0.0f, extents.max.z - extents.min.z);

                    // combined view/projection
                    shadow_camera_view_projections[c] = (mat4_mul(light_view, light_ortho));

                    // Build out cascade data to set in shadow rg node.
                    shadow_cascade_data cdata = {0};
                    cdata.cascade_index = c;
                    cdata.split_depth = (near + split_dist * clip_range) * -1.0f;
                    cdata.view_projection = shadow_camera_view_projections[c];
                    shadow_rendergraph_node_cascade_data_set(node, cdata, c);

                    last_split_dist = split_dist;
                }

                // Gather the geometries to be rendered.
                // Note that this only needs to happen once, since all geometries visible by the furthest-out cascase
                // must also be drawn on the nearest cascade to ensure objects outside the view cast shadows into the
                // view properly.
                u32 geometry_count = 0;
                geometry_render_data* geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);
                if (!scene_mesh_render_data_query_from_line(
                        scene,
                        light_dir,
                        culling_center,
                        culling_radius,
                        p_frame_data,
                        &geometry_count, &geometries)) {
                    KERROR("Failed to query shadow map pass meshes.");
                }
                // Track the number of meshes drawn in the shadow pass.
                p_frame_data->drawn_shadow_mesh_count = geometry_count;
                // Tell the node about them.
                shadow_rendergraph_node_static_geometries_set(node, p_frame_data, geometry_count, geometries);

                // Gather terrain geometries.
                u32 terrain_geometry_count = 0;
                geometry_render_data* terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);
                if (!scene_terrain_render_data_query_from_line(
                        scene,
                        light_dir,
                        culling_center,
                        culling_radius,
                        p_frame_data,
                        &terrain_geometry_count, &terrain_geometries)) {
                    KERROR("Failed to query shadow map pass terrain geometries.");
                }

                // TODO: Counter for terrain geometries.
                p_frame_data->drawn_shadow_mesh_count += terrain_geometry_count;
                // Tell the node about them.
                shadow_rendergraph_node_terrain_geometries_set(node, p_frame_data, terrain_geometry_count, terrain_geometries);
            }
        } else if (strings_equali(node->name, "debug")) {

            debug_rendergraph_node_viewport_set(node, state->world_viewport);
            debug_rendergraph_node_view_projection_set(
                node,
                camera_view_get(current_camera),
                camera_position_get(current_camera),
                current_viewport->projection);

            u32 debug_geometry_count = 0;
            if (!scene_debug_render_data_query(scene, &debug_geometry_count, 0)) {
                KERROR("Failed to obtain count of debug render objects.");
                return false;
            }
            geometry_render_data* debug_geometries = 0;
            if (debug_geometry_count) {
                debug_geometries = darray_reserve_with_allocator(geometry_render_data, debug_geometry_count, &p_frame_data->allocator);

                if (!scene_debug_render_data_query(scene, &debug_geometry_count, &debug_geometries)) {
                    KERROR("Failed to obtain debug render objects.");
                    return false;
                }

                // Make sure the count is correct before pushing.
                darray_length_set(debug_geometries, debug_geometry_count);
            } else {
                debug_geometries = darray_create_with_allocator(geometry_render_data, &p_frame_data->allocator);
            }

            // TODO: Move this to the scene.
            u32 line_count = darray_length(state->test_lines);
            for (u32 i = 0; i < line_count; ++i) {
                debug_line3d* line = &state->test_lines[i];
                debug_line3d_render_frame_prepare(line, p_frame_data);
                geometry_render_data rd = {0};
                rd.model = xform_world_get(line->xform);
                kgeometry* g = &line->geometry;
                rd.vertex_count = g->vertex_count;
                rd.vertex_buffer_offset = g->vertex_buffer_offset;
                rd.vertex_element_size = g->vertex_element_size;
                rd.index_count = g->index_count;
                rd.index_buffer_offset = g->index_buffer_offset;
                rd.index_element_size = g->index_element_size;
                rd.unique_id = INVALID_ID_U16;
                darray_push(debug_geometries, rd);
                debug_geometry_count++;
            }
            u32 box_count = darray_length(state->test_boxes);
            for (u32 i = 0; i < box_count; ++i) {
                debug_box3d* box = &state->test_boxes[i];
                debug_box3d_render_frame_prepare(box, p_frame_data);
                geometry_render_data rd = {0};
                rd.model = xform_world_get(box->xform);
                kgeometry* g = &box->geometry;
                rd.vertex_count = g->vertex_count;
                rd.vertex_buffer_offset = g->vertex_buffer_offset;
                rd.vertex_element_size = g->vertex_element_size;
                rd.index_count = g->index_count;
                rd.index_buffer_offset = g->index_buffer_offset;
                rd.index_element_size = g->index_element_size;
                rd.unique_id = INVALID_ID_U16;
                darray_push(debug_geometries, rd);
                debug_geometry_count++;
            }

            // Set geometries in the debug rg node.
            if (!debug_rendergraph_node_debug_geometries_set(node, p_frame_data, debug_geometry_count, debug_geometries)) {
                // NOTE: Not going to abort the whole graph for this failure, but will bleat about it loudly.
                KERROR("Failed to set geometries for debug rendergraph node.");
            }
        } else if (strings_equali(node->name, "editor_gizmo")) {
            editor_gizmo_rendergraph_node_viewport_set(node, state->world_viewport);
            editor_gizmo_rendergraph_node_view_projection_set(
                node,
                camera_view_get(current_camera),
                camera_position_get(current_camera),
                current_viewport->projection);
            if (!editor_gizmo_rendergraph_node_gizmo_set(node, &state->gizmo)) {
                // NOTE: Not going to abort the whole graph for this failure, but will bleat about it loudly.
                KERROR("Failed to set gizmo for editor_gizmo rendergraph node.");
            }

            // Only draw if loaded.
            editor_gizmo_rendergraph_node_enabled_set(node, scene->state == SCENE_STATE_LOADED);
        }
    }

    kclock_update(&state->prepare_clock);
    return true;
}

b8 application_render_frame(struct application* game_inst, struct frame_data* p_frame_data) {
    // Start the frame
    application_state* state = (application_state*)game_inst->state;
    if (!state->running) {
        return true;
    }

    kclock_start(&state->render_clock);

    // Execute the rendergraph.
    if (!rendergraph_execute_frame(&state->forward_graph, p_frame_data)) {
        KERROR("Rendergraph failed to execute frame, see logs for details.");
        return false;
    }

    kclock_update(&state->render_clock);

    return true;
}

void application_on_window_resize(struct application* game_inst, const struct kwindow* window) {
    if (!game_inst->state) {
        return;
    }

    application_state* state = (application_state*)game_inst->state;

    state->width = window->width;
    state->height = window->height;
    if (!window->width || !window->height) {
        return;
    }

    /* f32 half_width = state->width * 0.5f; */

    // Resize viewports.
    // World Viewport - right side
    rect_2d world_vp_rect = vec4_create(0.0f, 0.0f, state->width, state->height); // vec4_create(half_width + 20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport, world_vp_rect);

    // UI Viewport
    rect_2d ui_vp_rect = vec4_create(0.0f, 0.0f, state->width, state->height);
    viewport_resize(&state->ui_viewport, ui_vp_rect);

    // World viewport 2
    /* rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f); */
    rect_2d world_vp_rect2 = vec4_create(0.0f, 0.0f, state->width, state->height); // vec4_create(half_width + 20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport2, world_vp_rect2);

    // TODO: temp
    // Move debug text to new bottom of screen.
    // FIXME: This should be handled by the standard ui system resize event handler (that doesn't exist yet).
    sui_control_position_set(state->sui_state, &state->test_text, vec3_create(20, state->height - 95, 0));
    sui_control_position_set(state->sui_state, &state->test_text_black, vec3_create(21, state->height - 94, 0));
    // TODO: end temp
}

void application_shutdown(struct application* game_inst) {
    application_state* state = (application_state*)game_inst->state;
    state->running = false;

    if (state->main_scene.state == SCENE_STATE_LOADED) {
        KDEBUG("Unloading scene...");

        scene_unload(&state->main_scene, true);
        clear_debug_objects(game_inst);
        scene_destroy(&state->main_scene);

        KDEBUG("Done.");
    }

    rendergraph_destroy(&state->forward_graph);

#ifdef KOHI_DEBUG
    debug_console_unload(&state->debug_console);
#endif
}

void application_lib_on_unload(struct application* game_inst) {
    application_unregister_events(game_inst);
#ifdef KOHI_DEBUG
    debug_console_on_lib_unload(&((application_state*)game_inst->state)->debug_console);
#endif
    game_remove_commands(game_inst);
    game_remove_keymaps(game_inst);
}

void application_lib_on_load(struct application* game_inst) {
    application_register_events(game_inst);
#ifdef KOHI_DEBUG
    debug_console_on_lib_load(&((application_state*)game_inst->state)->debug_console, game_inst->stage >= APPLICATION_STAGE_BOOT_COMPLETE);
#endif
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

static b8 game_on_kvar_changed(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_KVAR_CHANGED) {
        kvar_change* change = context.data.custom_data.data;
        if (strings_equali("vsync", change->name)) {
            toggle_vsync();
            return true;
        }
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
        event_register(EVENT_CODE_DEBUG5, game_inst, game_on_debug_event);
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

static b8 load_main_scene(struct application* game_inst) {
    application_state* state = (application_state*)game_inst->state;

    kresource_scene_request_info request_info = {0};
    request_info.base.type = KRESOURCE_TYPE_SCENE;
    request_info.base.synchronous = true; // HACK: use a callback instead.
    request_info.base.assets = array_kresource_asset_info_create(1);
    kresource_asset_info* asset = &request_info.base.assets.data[0];
    asset->type = KASSET_TYPE_SCENE;
    asset->asset_name = kname_create("test_scene");
    asset->package_name = kname_create("Testbed");

    kresource_scene* scene_resource = (kresource_scene*)kresource_system_request(
        engine_systems_get()->kresource_state,
        kname_create("test_scene"),
        (kresource_request_info*)&request_info);
    if (!scene_resource) {
        KERROR("Failed to request scene resource. See logs for details.");
        return false;
    }

    /* // Load up config file
    // TODO: clean up resource.
    resource scene_resource;
    if (!resource_system_load("test_scene", RESOURCE_TYPE_scene, 0, &scene_resource)) {
        KERROR("Failed to load scene file, check above logs.");
        return false;
    }

    scene_config* scene_cfg = (scene_config*)scene_resource.data;
    scene_cfg->resource_name = string_duplicate(scene_resource.name);
    scene_cfg->resource_full_path = string_duplicate(scene_resource.full_path); */

    // Create the scene.
    scene_flags scene_load_flags = 0;
    /* scene_load_flags |= SCENE_FLAG_READONLY;  // NOTE: to enable "editor mode", turn this flag off. */
    if (!scene_create(scene_resource, scene_load_flags, &state->main_scene)) {
        KERROR("Failed to create main scene");
        return false;
    }

    // Initialize
    if (!scene_initialize(&state->main_scene)) {
        KERROR("Failed initialize main scene, aborting game.");
        return false;
    }

    // TODO: fix once scene loading works again.
    state->p_light_1 = 0; // scene_point_light_get(&state->main_scene, "point_light_1");

    // Actually load the scene.
    return scene_load(&state->main_scene);
}

static b8 save_main_scene(struct application* game_inst) {
    if (!game_inst) {
        return false;
    }
    application_state* state = (application_state*)game_inst->state;

    return scene_save(&state->main_scene);
}
