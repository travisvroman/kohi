#include "editor_main.h"

#include <containers/darray.h>
#include <core/clock.h>
#include <core/console.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/input.h>
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

#include "defines.h"
#include "editor_types.h"
#include "game_state.h"
#include "math/math_types.h"
#include "renderer/viewport.h"
#include "systems/camera_system.h"

// Views
#include "editor/render_view_wireframe.h"
#include "resources/loaders/simple_scene_loader.h"
#include "views/render_view_pick.h"
#include "views/render_view_ui.h"
#include "views/render_view_world.h"

// TODO: Editor temp
#include <resources/debug/debug_box3d.h>
#include <resources/debug/debug_line3d.h>

#include "editor/editor_gizmo.h"
#include "editor/render_view_editor_world.h"

// TODO: temp
#include <core/identifier.h>
#include <math/transform.h>
#include <resources/mesh.h>
#include <resources/simple_scene.h>
#include <resources/skybox.h>
#include <resources/ui_text.h>
#include <systems/geometry_system.h>
#include <systems/light_system.h>
#include <systems/material_system.h>
#include <systems/render_view_system.h>
#include <systems/resource_system.h>

// TODO: end temp

b8 configure_render_views(application_config* config);
void application_register_events(struct application* game_inst);
void application_unregister_events(struct application* game_inst);
static b8 load_main_scene(struct application* game_inst);

b8 game_on_debug_event(u16 code, void* sender, void* listener_inst, event_context data) {
    application* game_inst = (application*)listener_inst;
    editor_game_state* state = (editor_game_state*)game_inst->state;

    if (code == EVENT_CODE_DEBUG1) {
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
    }

    return false;
}

b8 game_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    application* game_inst = (application*)listener_inst;
    editor_game_state* state = (editor_game_state*)game_inst->state;
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
    editor_game_state* state = (editor_game_state*)listener_inst;

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
                editor_game_state* state = (editor_game_state*)listener_inst;

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

        editor_game_state* state = (editor_game_state*)listener_inst;

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

u64 application_state_size(void) {
    return sizeof(editor_game_state);
}

b8 application_boot(struct application* game_inst) {
    KINFO("Booting editor...");

    // Allocate the game state.
    game_inst->state = kallocate(sizeof(editor_game_state), MEMORY_TAG_GAME);
    ((editor_game_state*)game_inst->state)->running = false;

    application_config* config = &game_inst->app_config;

    config->frame_allocator_size = MEBIBYTES(64);
    config->app_frame_data_size = sizeof(editor_application_frame_data);

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

    // Configure render views.
    if (!configure_render_views(config)) {
        KERROR("Failed to configure renderer views. Aborting application.");
        return false;
    }

    return true;
}

b8 application_initialize(struct application* game_inst) {
    KDEBUG("game_initialize() called!");

    application_register_events(game_inst);

    // Register resource loaders.
    resource_system_loader_register(simple_scene_resource_loader_create());

    editor_game_state* state = (editor_game_state*)game_inst->state;
    state->selection.unique_id = INVALID_ID;
    state->selection.xform = 0;

    state->test_lines = darray_create(debug_line3d);
    state->test_boxes = darray_create(debug_box3d);

    // Viewport setup.
    // World Viewport
    rect_2d world_vp_rect = vec4_create(20.0f, 20.0f, 1280.0f - 40.0f, 720.0f - 40.0f);
    if (!viewport_create(world_vp_rect, deg_to_rad(45.0f), 0.1f, 4000.0f, RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE, &state->world_viewport)) {
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
    rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, 128.8f, 72.0f);
    if (!viewport_create(world_vp_rect2, 0.015f, -4000.0f, 4000.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED, &state->world_viewport2)) {
        KERROR("Failed to create wireframe viewport. Cannot start application.");
        return false;
    }

    state->forward_move_speed = 5.0f;
    state->backward_move_speed = 2.5f;

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
    if (!ui_text_create("editor_mono_test_text", UI_TEXT_TYPE_BITMAP, "Ubuntu Mono 21px", 21, "Some test text 123,\n\tyo!", &state->test_text)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    }
    // Move debug text to new bottom of screen.
    ui_text_position_set(&state->test_text, vec3_create(20, game_inst->app_config.start_height - 75, 0));

    if (!ui_text_create("editor_UTF_test_text", UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "Some system text 123, \n\tyo!\n\n\tこんにちは 한", &state->test_sys_text)) {
        KERROR("Failed to load basic ui system text.");
        return false;
    }
    ui_text_position_set(&state->test_sys_text, vec3_create(500, 550, 0));

    // TODO: end temp load/prepare stuff

    state->world_camera = camera_system_acquire("world");
    camera_position_set(state->world_camera, (vec3){16.07f, 4.5f, 25.0f});
    camera_rotation_euler_set(state->world_camera, (vec3){-20.0f, 51.0f, 0.0f});

    // TODO: temp test
    state->world_camera_2 = camera_system_acquire("world_2");
    camera_position_set(state->world_camera_2, (vec3){8.0f, 0.0f, 10.0f});
    camera_rotation_euler_set(state->world_camera_2, (vec3){0.0f, -90.0f, 0.0f});

    kzero_memory(&state->update_clock, sizeof(clock));
    kzero_memory(&state->render_clock, sizeof(clock));

    kzero_memory(&state->update_clock, sizeof(clock));
    kzero_memory(&state->render_clock, sizeof(clock));

    state->running = true;

    return true;
}

b8 application_update(struct application* game_inst, struct frame_data* p_frame_data) {
    editor_application_frame_data* app_frame_data = (editor_application_frame_data*)p_frame_data->application_frame_data;
    if (!app_frame_data) {
        return true;
    }

    editor_game_state* state = (editor_game_state*)game_inst->state;
    if (!state->running) {
        return true;
    }

    clock_start(&state->update_clock);

    if (state->main_scene.state >= SIMPLE_SCENE_STATE_LOADED) {
        if (!simple_scene_update(&state->main_scene, p_frame_data)) {
            KWARN("Failed to update main scene.");
        }

        editor_gizmo_update(&state->gizmo);
    }

    // Track allocation differences.
    state->prev_alloc_count = state->alloc_count;
    state->alloc_count = get_memory_alloc_count();

    // Update the bitmap text with camera position. NOTE: just using the default camera for now.
    vec3 pos = camera_position_get(state->world_camera);
    vec3 rot = camera_rotation_euler_get(state->world_camera);

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

    char* vsync_text = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? "YES" : " NO";
    char text_buffer[2048];
    string_format(
        text_buffer,
        "\
FPS: %5.1f(%4.1fms)        Pos=[%7.3f %7.3f %7.3f] Rot=[%7.3f, %7.3f, %7.3f]\n\
Upd: %8.3fus, Rend: %8.3fus Mouse: X=%-5d Y=%-5d   L=%s R=%s   NDC: X=%.6f, Y=%.6f\n\
VSync: %s Drawn: %-5u Hovered: %s%u\n\
Text",
        fps,
        frame_time,
        pos.x, pos.y, pos.z,
        rad_to_deg(rot.x), rad_to_deg(rot.y), rad_to_deg(rot.z),
        state->last_update_elapsed * K_SEC_TO_US_MULTIPLIER,
        state->render_clock.elapsed * K_SEC_TO_US_MULTIPLIER,
        mouse_x, mouse_y,
        left_down ? "Y" : "N",
        right_down ? "Y" : "N",
        mouse_x_ndc,
        mouse_y_ndc,
        vsync_text,
        p_frame_data->drawn_mesh_count,
        state->hovered_object_id == INVALID_ID ? "none" : "",
        state->hovered_object_id == INVALID_ID ? 0 : state->hovered_object_id);
    if (state->running) {
        ui_text_text_set(&state->test_text, text_buffer);
    }

    clock_update(&state->update_clock);
    state->last_update_elapsed = state->update_clock.elapsed;

    return true;
}

b8 application_prepare_render_packet(struct application* app_inst, struct render_packet* packet, struct frame_data* p_frame_data) {
    editor_game_state* state = (editor_game_state*)app_inst->state;
    if (!state->running) {
        return true;
    }

    packet->view_count = 4;
    packet->views = p_frame_data->allocator.allocate(sizeof(render_view_packet) * packet->view_count);

    // TODO: Cache these instead of lookups every frame.
    // packet->views[editor_PACKET_VIEW_SKYBOX].view = render_view_system_get("skybox");
    packet->views[EDITOR_PACKET_VIEW_WORLD].view = render_view_system_get("world");
    packet->views[EDITOR_PACKET_VIEW_EDITOR_WORLD].view = render_view_system_get("editor_world");
    packet->views[EDITOR_PACKET_VIEW_WIREFRAME].view = render_view_system_get("wireframe");
    packet->views[EDITOR_PACKET_VIEW_UI].view = render_view_system_get("ui");
    // packet->views[EDITOR_PACKET_VIEW_PICK].view = render_view_system_get("pick");

    // Tell our scene to generate relevant packet data. NOTE: Generates skybox and world packets.
    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        if (!simple_scene_populate_render_packet(&state->main_scene, state->world_camera, &state->world_viewport, p_frame_data, packet)) {
            KERROR("Failed populare render packet for main scene.");
            return false;
        }
    } else {
        // Make sure they at least have a viewport.
        // packet->views[editor_PACKET_VIEW_SKYBOX].vp = &state->world_viewport;
        packet->views[editor_PACKET_VIEW_WORLD].vp = &state->world_viewport;
    }

    // HACK: Inject debug geometries into world packet.
    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        u32 line_count = darray_length(state->test_lines);
        for (u32 i = 0; i < line_count; ++i) {
            geometry_render_data rd = {0};
            rd.model = transform_world_get(&state->test_lines[i].xform);
            rd.geometry = &state->test_lines[i].geo;
            rd.unique_id = INVALID_ID_U16;
            darray_push(packet->views[editor_PACKET_VIEW_WORLD].debug_geometries, rd);
            packet->views[editor_PACKET_VIEW_WORLD].debug_geometry_count++;
        }
        u32 box_count = darray_length(state->test_boxes);
        for (u32 i = 0; i < box_count; ++i) {
            geometry_render_data rd = {0};
            rd.model = transform_world_get(&state->test_boxes[i].xform);
            rd.geometry = &state->test_boxes[i].geo;
            rd.unique_id = INVALID_ID_U16;
            darray_push(packet->views[editor_PACKET_VIEW_WORLD].debug_geometries, rd);
            packet->views[editor_PACKET_VIEW_WORLD].debug_geometry_count++;
        }
    }

    // Editor world
    {
        render_view_packet* view_packet = &packet->views[editor_PACKET_VIEW_EDITOR_WORLD];
        const render_view* view = view_packet->view;

        editor_world_packet_data editor_world_data = {0};
        editor_world_data.gizmo = &state->gizmo;
        if (!render_view_system_packet_build(view, p_frame_data, &state->world_viewport, state->world_camera, &editor_world_data, view_packet)) {
            KERROR("Failed to build packet for view 'editor_world'.");
            return false;
        }
    }

    // Wireframe
    {
        render_view_packet* view_packet = &packet->views[editor_PACKET_VIEW_WIREFRAME];
        const render_view* view = view_packet->view;

        render_view_wireframe_data wireframe_data = {0};
        // TODO: Get a list of geometries not culled for the current camera.
        wireframe_data.selected_id = state->selection.unique_id;
        wireframe_data.world_geometries = packet->views[editor_PACKET_VIEW_WORLD].geometries;
        wireframe_data.terrain_geometries = packet->views[editor_PACKET_VIEW_WORLD].terrain_geometries;
        if (!render_view_system_packet_build(view, p_frame_data, &state->world_viewport2, state->world_camera_2, &wireframe_data, view_packet)) {
            KERROR("Failed to build packet for view 'wireframe'");
            return false;
        }
    }

    // UI
    ui_packet_data ui_packet = {0};
    {
        render_view_packet* view_packet = &packet->views[editor_PACKET_VIEW_UI];
        const render_view* view = view_packet->view;

        u32 ui_mesh_count = 0;
        u32 max_ui_meshes = 10;
        mesh** ui_meshes = p_frame_data->allocator.allocate(sizeof(mesh*) * max_ui_meshes);

        for (u32 i = 0; i < max_ui_meshes; ++i) {
            if (state->ui_meshes[i].generation != INVALID_ID_U8) {
                ui_meshes[ui_mesh_count] = &state->ui_meshes[i];
                ui_mesh_count++;
            }
        }

        ui_packet.mesh_data.mesh_count = ui_mesh_count;
        ui_packet.mesh_data.meshes = ui_meshes;
        ui_packet.text_count = 2;
        ui_text* debug_console_text = debug_console_get_text(&state->debug_console);
        b8 render_debug_conole = debug_console_text && debug_console_visible(&state->debug_console);
        if (render_debug_conole) {
            ui_packet.text_count += 2;
        }
        ui_text** texts = p_frame_data->allocator.allocate(sizeof(ui_text*) * ui_packet.text_count);
        texts[0] = &state->test_text;
        texts[1] = &state->test_sys_text;
        if (render_debug_conole) {
            texts[2] = debug_console_text;
            texts[3] = debug_console_get_entry_text(&state->debug_console);
        }

        ui_packet.texts = texts;
        if (!render_view_system_packet_build(view, p_frame_data, &state->ui_viewport, 0, &ui_packet, view_packet)) {
            KERROR("Failed to build packet for view 'ui'.");
            return false;
        }
    }

    // TODO: end temp
    return true;
}

b8 application_render(struct application* game_inst, struct render_packet* packet, struct frame_data* p_frame_data) {
    // Start the frame
    if (!renderer_frame_prepare(p_frame_data)) {
        return true;
    }

    if (!renderer_begin(p_frame_data)) {
        //
    }

    editor_game_state* state = (editor_game_state*)game_inst->state;
    if (!state->running) {
        return true;
    }
    // editor_application_frame_data* app_frame_data = (editor_application_frame_data*)p_frame_data->application_frame_data;

    clock_start(&state->render_clock);

    // World
    render_view_packet* view_packet = &packet->views[editor_PACKET_VIEW_WORLD];
    view_packet->view->on_render(view_packet->view, view_packet, p_frame_data);

    // Editor world
    view_packet = &packet->views[editor_PACKET_VIEW_EDITOR_WORLD];
    view_packet->view->on_render(view_packet->view, view_packet, p_frame_data);

    // Render the wireframe view
    view_packet = &packet->views[editor_PACKET_VIEW_WIREFRAME];
    view_packet->view->on_render(view_packet->view, view_packet, p_frame_data);

    // UI
    view_packet = &packet->views[editor_PACKET_VIEW_UI];
    view_packet->view->on_render(view_packet->view, view_packet, p_frame_data);

    clock_update(&state->render_clock);

    renderer_end(p_frame_data);

    if (!renderer_present(p_frame_data)) {
        KERROR("The call to renderer_present failed. This is likely unrecoverable. Shutting down.");
        return false;
    }

    return true;
}

void application_on_resize(struct application* game_inst, u32 width, u32 height) {
    if (!game_inst->state) {
        return;
    }

    editor_game_state* state = (editor_game_state*)game_inst->state;

    state->width = width;
    state->height = height;
    if (!width || !height) {
        return;
    }

    f32 half_width = state->width * 0.5f;

    // Resize viewports.
    // World Viewport - right side
    rect_2d world_vp_rect = vec4_create(half_width + 20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport, world_vp_rect);

    // UI Viewport
    rect_2d ui_vp_rect = vec4_create(0.0f, 0.0f, state->width, state->height);
    viewport_resize(&state->ui_viewport, ui_vp_rect);

    // World viewport 2
    rect_2d world_vp_rect2 = vec4_create(20.0f, 20.0f, half_width - 40.0f, state->height - 40.0f);
    viewport_resize(&state->world_viewport2, world_vp_rect2);

    // TODO: temp
    // Move debug text to new bottom of screen.
    ui_text_position_set(&state->test_text, vec3_create(20, state->height - 75, 0));
    // TODO: end temp
}

void application_shutdown(struct application* game_inst) {
    editor_game_state* state = (editor_game_state*)game_inst->state;
    state->running = false;

    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        KDEBUG("Unloading scene...");

        simple_scene_unload(&state->main_scene, true);
        clear_debug_objects(game_inst);

        KDEBUG("Done.");
    }

    // TODO: Temp

    // Destroy ui texts
    ui_text_destroy(&state->test_text);
    ui_text_destroy(&state->test_sys_text);
}

void application_lib_on_unload(struct application* game_inst) {
    application_unregister_events(game_inst);
    debug_console_on_lib_unload(&((editor_game_state*)game_inst->state)->debug_console);
    game_remove_commands(game_inst);
    game_remove_keymaps(game_inst);
}

void application_lib_on_load(struct application* game_inst) {
    application_register_events(game_inst);
    debug_console_on_lib_load(&((editor_game_state*)game_inst->state)->debug_console, game_inst->stage >= APPLICATION_STAGE_BOOT_COMPLETE);
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
        event_register(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, game_inst, game_on_event);
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
    event_unregister(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, game_inst, game_on_event);
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

b8 configure_render_views(application_config* config) {
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
        editor_world_pass.name = "Renderpass.editor.EditorWorld";
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
        wireframe_pass.name = "Renderpass.editor.Wireframe";
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

    return true;
}

static b8 load_main_scene(struct application* game_inst) {
    editor_game_state* state = (editor_game_state*)game_inst->state;

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

    // Initialize
    if (!simple_scene_initialize(&state->main_scene)) {
        KERROR("Failed initialize main scene, aborting game.");
        return false;
    }

    // Actually load the scene.
    return simple_scene_load(&state->main_scene);
}
