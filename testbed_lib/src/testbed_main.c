#include "testbed_main.h"
#include "game_state.h"

#include <core/logger.h>
#include <core/kmemory.h>

#include <core/kstring.h>
#include <core/input.h>
#include <core/event.h>
#include <core/metrics.h>
#include <core/clock.h>
#include <core/console.h>
#include <core/frame_data.h>

#include <containers/darray.h>
#include <memory/linear_allocator.h>

#include <math/kmath.h>
#include <renderer/renderer_types.inl>
#include <renderer/renderer_frontend.h>

// TODO: temp
#include <core/identifier.h>
#include <math/transform.h>
#include <resources/skybox.h>
#include <resources/ui_text.h>
#include <resources/mesh.h>
#include <systems/geometry_system.h>
#include <systems/material_system.h>
#include <systems/render_view_system.h>
#include <systems/light_system.h>
#include <resources/simple_scene.h>
#include "debug_console.h"
#include "game_commands.h"
#include "game_keybinds.h"
// TODO: end temp

b8 configure_render_views(application_config* config);
void application_register_events(struct application* game_inst);
void application_unregister_events(struct application* game_inst);
static b8 load_main_scene(struct application* game_inst);

b8 game_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    application* game_inst = (application*)listener_inst;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    switch (code) {
        case EVENT_CODE_OBJECT_HOVER_ID_CHANGED: {
            state->hovered_object_id = context.data.u32[0];
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

            simple_scene_unload(&state->main_scene);

            KDEBUG("Done.");
        }
        return true;
    }

    return false;
}

b8 game_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    // if (code == EVENT_CODE_KEY_PRESSED) {
    //     u16 key_code = context.data.u16[0];
    //     if (key_code == KEY_A) {
    //         // Example on checking for a key
    //         KDEBUG("Explicit - A key pressed!");
    //     } else {
    //         // KTRACE("'%s' key pressed in window.", input_keycode_str(key_code));
    //     }
    // } else if (code == EVENT_CODE_KEY_RELEASED) {
    //     u16 key_code = context.data.u16[0];
    //     if (key_code == KEY_B) {
    //         // Example on checking for a key
    //         KDEBUG("Explicit - B key released!");
    //     } else {
    //         // KTRACE("'%s' key released in window.", input_keycode_str(key_code));
    //     }
    // }
    return false;
}

u64 application_state_size(void) {
    return sizeof(testbed_game_state);
}

b8 application_boot(struct application* game_inst) {
    KINFO("Booting testbed...");

    // Allocate the game state.
    game_inst->state = kallocate(sizeof(testbed_game_state), MEMORY_TAG_GAME);

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

    // Configure render views. TODO: read from file?
    if (!configure_render_views(config)) {
        KERROR("Failed to configure renderer views. Aborting application.");
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

    application_register_events(game_inst);

    debug_console_load(&((testbed_game_state*)game_inst->state)->debug_console);

    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    // World meshes
    // Invalidate all meshes.
    for (u32 i = 0; i < 10; ++i) {
        state->meshes[i].generation = INVALID_ID_U8;
        state->ui_meshes[i].generation = INVALID_ID_U8;
    }

    // Create test ui text objects
    if (!ui_text_create(UI_TEXT_TYPE_BITMAP, "Ubuntu Mono 21px", 21, "Some test text 123,\n\tyo!", &state->test_text)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    }
    // Move debug text to new bottom of screen.
    ui_text_set_position(&state->test_text, vec3_create(20, game_inst->app_config.start_height - 75, 0));

    if (!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "Some system text 123, \n\tyo!\n\n\tこんにちは 한", &state->test_sys_text)) {
        KERROR("Failed to load basic ui system text.");
        return false;
    }
    ui_text_set_position(&state->test_sys_text, vec3_create(500, 550, 0));

    // Load up some test UI geometry.
    geometry_config ui_config;
    ui_config.vertex_size = sizeof(vertex_2d);
    ui_config.vertex_count = 4;
    ui_config.index_size = sizeof(u32);
    ui_config.index_count = 6;
    string_ncopy(ui_config.material_name, "test_ui_material", MATERIAL_NAME_MAX_LENGTH);
    string_ncopy(ui_config.name, "test_ui_geometry", GEOMETRY_NAME_MAX_LENGTH);

    const f32 w = 128.0f;
    const f32 h = 32.0f;
    vertex_2d uiverts[4];
    uiverts[0].position.x = 0.0f;  // 0    3
    uiverts[0].position.y = 0.0f;  //
    uiverts[0].texcoord.x = 0.0f;  //
    uiverts[0].texcoord.y = 0.0f;  // 2    1

    uiverts[1].position.y = h;
    uiverts[1].position.x = w;
    uiverts[1].texcoord.x = 1.0f;
    uiverts[1].texcoord.y = 1.0f;

    uiverts[2].position.x = 0.0f;
    uiverts[2].position.y = h;
    uiverts[2].texcoord.x = 0.0f;
    uiverts[2].texcoord.y = 1.0f;

    uiverts[3].position.x = w;
    uiverts[3].position.y = 0.0;
    uiverts[3].texcoord.x = 1.0f;
    uiverts[3].texcoord.y = 0.0f;
    ui_config.vertices = uiverts;

    // Indices - counter-clockwise
    u32 uiindices[6] = {2, 1, 0, 3, 0, 1};
    ui_config.indices = uiindices;

    // Get UI geometry from config.
    state->ui_meshes[0].unique_id = identifier_aquire_new_id(&state->ui_meshes[0]);
    state->ui_meshes[0].geometry_count = 1;
    state->ui_meshes[0].geometries = kallocate(sizeof(geometry*), MEMORY_TAG_ARRAY);
    state->ui_meshes[0].geometries[0] = geometry_system_acquire_from_config(ui_config, true);
    state->ui_meshes[0].transform = transform_create();
    state->ui_meshes[0].generation = 0;

    // Move and rotate it some.
    // quat rotation = quat_from_axis_angle((vec3){0, 0, 1}, deg_to_rad(-45.0f), false);
    // transform_translate_rotate(&state->ui_meshes[0].transform, (vec3){5, 5, 0}, rotation);
    transform_translate(&state->ui_meshes[0].transform, (vec3){650, 5, 0});

    // TODO: end temp load/prepare stuff

    state->world_camera = camera_system_get_default();
    camera_position_set(state->world_camera, (vec3){10.5f, 5.0f, 9.5f});

    // kzero_memory(&game_inst->frame_data, sizeof(app_frame_data));

    kzero_memory(&state->update_clock, sizeof(clock));
    kzero_memory(&state->render_clock, sizeof(clock));

    kzero_memory(&state->update_clock, sizeof(clock));
    kzero_memory(&state->render_clock, sizeof(clock));

    return true;
}

b8 application_update(struct application* game_inst, struct frame_data* p_frame_data) {
    testbed_application_frame_data* app_frame_data = (testbed_application_frame_data*)p_frame_data->application_frame_data;
    if (!app_frame_data) {
        return true;
    }

    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    clock_start(&state->update_clock);

    if (state->main_scene.state >= SIMPLE_SCENE_STATE_LOADED) {
        if (!simple_scene_update(&state->main_scene, p_frame_data)) {
            KWARN("Failed to update main scene.");
        }

        // Perform a small rotation on the first mesh.
        quat rotation = quat_from_axis_angle((vec3){0, 1, 0}, -0.5f * p_frame_data->delta_time, false);
        transform_rotate(&state->meshes[0].transform, rotation);

        // Perform a similar rotation on the second mesh, if it exists.
        transform_rotate(&state->meshes[1].transform, rotation);

        // Perform a similar rotation on the third mesh, if it exists.
        transform_rotate(&state->meshes[2].transform, rotation);

        state->p_lights[1].colour = (vec4){0.0f, 1.0f, 1.0f, 1.0f};
        state->p_lights[1].position.x -= 0.005f;
    }

    // Track allocation differences.
    state->prev_alloc_count = state->alloc_count;
    state->alloc_count = get_memory_alloc_count();

    // Update the bitmap text with camera position. NOTE: just using the default camera for now.
    camera* world_camera = camera_system_get_default();
    vec3 pos = camera_position_get(world_camera);
    vec3 rot = camera_rotation_euler_get(world_camera);

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

    char* vsync_text = renderer_flag_enabled(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? "YES" : " NO";
    char text_buffer[2048];
    string_format(
        text_buffer,
        "\
FPS: %5.1f(%4.1fms)        Pos=[%7.3f %7.3f %7.3f] Rot=[%7.3f, %7.3f, %7.3f]\n\
Upd: %8.3fus, Rend: %8.3fus Mouse: X=%-5d Y=%-5d   L=%s R=%s   NDC: X=%.6f, Y=%.6f\n\
VSync: %s Drawn: %-5u Hovered: %s%u",
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
    ui_text_set_text(&state->test_text, text_buffer);

    debug_console_update(&((testbed_game_state*)game_inst->state)->debug_console);

    clock_update(&state->update_clock);
    state->last_update_elapsed = state->update_clock.elapsed;

    return true;
}

b8 application_render(struct application* game_inst, struct render_packet* packet, struct frame_data* p_frame_data) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    // testbed_application_frame_data* app_frame_data = (testbed_application_frame_data*)p_frame_data->application_frame_data;

    clock_start(&state->render_clock);

    // TODO: temp

    // TODO: Read from frame config.
    packet->view_count = 4;
    packet->views = linear_allocator_allocate(p_frame_data->frame_allocator, sizeof(render_view_packet) * packet->view_count);

    // FIXME: Read this from config
    packet->views[0].view = render_view_system_get("skybox");
    packet->views[1].view = render_view_system_get("world");
    packet->views[2].view = render_view_system_get("ui");
    packet->views[3].view = render_view_system_get("pick");

    // Tell our scene to generate relevant packet data.
    if (state->main_scene.state == SIMPLE_SCENE_STATE_LOADED) {
        if (!simple_scene_populate_render_packet(&state->main_scene, state->world_camera, (f32)state->width / state->height, p_frame_data, packet)) {
            KERROR("Failed populare render packet for main scene.");
            return false;
        }
    }

    // ui
    ui_packet_data ui_packet = {};

    u32 ui_mesh_count = 0;
    u32 max_ui_meshes = 10;
    mesh** ui_meshes = linear_allocator_allocate(p_frame_data->frame_allocator, sizeof(mesh*) * max_ui_meshes);

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
    ui_text** texts = linear_allocator_allocate(p_frame_data->frame_allocator, sizeof(ui_text*) * ui_packet.text_count);
    texts[0] = &state->test_text;
    texts[1] = &state->test_sys_text;
    if (render_debug_conole) {
        texts[2] = debug_console_text;
        texts[3] = debug_console_get_entry_text(&state->debug_console);
    }

    ui_packet.texts = texts;
    if (!render_view_system_build_packet(render_view_system_get("ui"), p_frame_data->frame_allocator, &ui_packet, &packet->views[2])) {
        KERROR("Failed to build packet for view 'ui'.");
        return false;
    }

    // Pick uses both world and ui packet data.
    pick_packet_data pick_packet = {};
    pick_packet.ui_mesh_data = ui_packet.mesh_data;
    pick_packet.world_mesh_data = packet->views[1].geometries;  // TODO: non-hardcoded index?
    pick_packet.texts = ui_packet.texts;
    pick_packet.text_count = ui_packet.text_count;

    if (!render_view_system_build_packet(render_view_system_get("pick"), p_frame_data->frame_allocator, &pick_packet, &packet->views[3])) {
        KERROR("Failed to build packet for view 'ui'.");
        return false;
    }
    // TODO: end temp

    clock_update(&state->render_clock);

    return true;
}

void application_on_resize(struct application* game_inst, u32 width, u32 height) {
    if (!game_inst->state) {
        return;
    }

    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    state->width = width;
    state->height = height;

    // TODO: temp
    // Move debug text to new bottom of screen.
    ui_text_set_position(&state->test_text, vec3_create(20, state->height - 75, 0));
    // TODO: end temp
}

void application_shutdown(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    // TODO: Temp
    skybox_destroy(&state->sb);

    // Destroy ui texts
    ui_text_destroy(&state->test_text);
    ui_text_destroy(&state->test_sys_text);

    debug_console_unload(&state->debug_console);
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
    b8 vsync_enabled = renderer_flag_enabled(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT);
    vsync_enabled = !vsync_enabled;
    renderer_flag_set_enabled(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT, vsync_enabled);
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
    // TODO: end temp

    event_unregister(EVENT_CODE_KEY_PRESSED, game_inst, game_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, game_inst, game_on_key);

    event_unregister(EVENT_CODE_KVAR_CHANGED, 0, game_on_kvar_changed);
}

b8 configure_render_views(application_config* config) {
    config->render_views = darray_create(render_view_config);

    // Skybox view
    render_view_config skybox_config = {};
    skybox_config.type = RENDERER_VIEW_KNOWN_TYPE_SKYBOX;
    skybox_config.width = 0;
    skybox_config.height = 0;
    skybox_config.name = "skybox";
    skybox_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;

    // Renderpass config.
    skybox_config.passes = darray_create(renderpass_config);
    renderpass_config skybox_pass = {0};
    skybox_pass.name = "Renderpass.Builtin.Skybox";
    skybox_pass.render_area = (vec4){0, 0, (f32)config->start_width, (f32)config->start_height};  // Default render area resolution.
    skybox_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    skybox_pass.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;
    skybox_pass.depth = 1.0f;
    skybox_pass.stencil = 0;
    skybox_pass.target.attachments = darray_create(render_target_attachment_config);

    // Color attachment.
    render_target_attachment_config skybox_target_colour = {0};
    skybox_target_colour.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    skybox_target_colour.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    skybox_target_colour.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    skybox_target_colour.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    skybox_target_colour.present_after = false;
    darray_push(skybox_pass.target.attachments, skybox_target_colour);

    skybox_pass.target.attachment_count = darray_length(skybox_pass.target.attachments);

    skybox_pass.render_target_count = renderer_window_attachment_count_get();

    darray_push(skybox_config.passes, skybox_pass);
    skybox_config.pass_count = darray_length(skybox_config.passes);

    darray_push(config->render_views, skybox_config);

    // World view.
    render_view_config world_config = {};
    world_config.type = RENDERER_VIEW_KNOWN_TYPE_WORLD;
    world_config.width = 0;
    world_config.height = 0;
    world_config.name = "world";
    world_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;
    world_config.passes = darray_create(renderpass_config);

    // Renderpass config.
    renderpass_config world_pass = {0};
    world_pass.name = "Renderpass.Builtin.World";
    world_pass.render_area = (vec4){0, 0, (f32)config->start_width, (f32)config->start_height};  // Default render area resolution.
    world_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    world_pass.clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
    world_pass.depth = 1.0f;
    world_pass.stencil = 0;
    world_pass.target.attachments = darray_create(render_target_attachment_config);

    // Colour attachment
    render_target_attachment_config world_target_colour = {0};
    world_target_colour.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    world_target_colour.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    world_target_colour.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    world_target_colour.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_target_colour.present_after = false;
    darray_push(world_pass.target.attachments, world_target_colour);

    // Depth attachment
    render_target_attachment_config world_target_depth = {0};
    world_target_depth.type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    world_target_depth.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    world_target_depth.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    world_target_depth.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_target_depth.present_after = false;
    darray_push(world_pass.target.attachments, world_target_depth);

    world_pass.target.attachment_count = darray_length(world_pass.target.attachments);
    world_pass.render_target_count = renderer_window_attachment_count_get();
    darray_push(world_config.passes, world_pass);

    world_config.pass_count = darray_length(world_config.passes);

    darray_push(config->render_views, world_config);

    // UI view
    render_view_config ui_view_config = {};
    ui_view_config.type = RENDERER_VIEW_KNOWN_TYPE_UI;
    ui_view_config.width = 0;
    ui_view_config.height = 0;
    ui_view_config.name = "ui";
    ui_view_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;
    ui_view_config.passes = darray_create(renderpass_config);

    // Renderpass config
    renderpass_config ui_pass;
    ui_pass.name = "Renderpass.Builtin.UI";
    ui_pass.render_area = (vec4){0, 0, (f32)config->start_width, (f32)config->start_height};
    ui_pass.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    ui_pass.clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
    ui_pass.depth = 1.0f;
    ui_pass.stencil = 0;
    ui_pass.target.attachments = darray_create(render_target_attachment_config);

    render_target_attachment_config ui_target_attachment = {};
    // Colour attachment.
    ui_target_attachment.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    ui_target_attachment.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    ui_target_attachment.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    ui_target_attachment.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    ui_target_attachment.present_after = true;

    darray_push(ui_pass.target.attachments, ui_target_attachment);
    ui_pass.target.attachment_count = darray_length(ui_pass.target.attachments);
    ui_pass.render_target_count = renderer_window_attachment_count_get();

    darray_push(ui_view_config.passes, ui_pass);
    ui_view_config.pass_count = darray_length(ui_view_config.passes);

    darray_push(config->render_views, ui_view_config);

    // Pick pass.
    render_view_config pick_view_config = {};
    pick_view_config.type = RENDERER_VIEW_KNOWN_TYPE_PICK;
    pick_view_config.width = 0;
    pick_view_config.height = 0;
    pick_view_config.name = "pick";
    pick_view_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;

    pick_view_config.passes = darray_create(renderpass_config);

    // World pick pass
    renderpass_config world_pick_pass = {0};
    world_pick_pass.name = "Renderpass.Builtin.WorldPick";
    world_pick_pass.render_area = (vec4){0, 0, (f32)config->start_width, (f32)config->start_height};
    world_pick_pass.clear_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};  // HACK: clearing to white for better visibility// TODO: Clear to black, as 0 is invalid id.
    world_pick_pass.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG | RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG;
    world_pick_pass.depth = 1.0f;
    world_pick_pass.stencil = 0;
    world_pick_pass.target.attachments = darray_create(render_target_attachment_config);

    // Colour attachment
    render_target_attachment_config world_pick_pass_colour = {0};
    world_pick_pass_colour.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    world_pick_pass_colour.source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;  // Obtain the attachment from the view.
    world_pick_pass_colour.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    world_pick_pass_colour.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_pick_pass_colour.present_after = false;
    darray_push(world_pick_pass.target.attachments, world_pick_pass_colour);

    // Depth attachment
    render_target_attachment_config world_pick_pass_depth = {0};
    world_pick_pass_depth.type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    world_pick_pass_depth.source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;  // Obtain the attachment from the view.
    world_pick_pass_depth.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    world_pick_pass_depth.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_pick_pass_depth.present_after = false;
    darray_push(world_pick_pass.target.attachments, world_pick_pass_depth);

    world_pick_pass.target.attachment_count = darray_length(world_pick_pass.target.attachments);
    world_pick_pass.render_target_count = 1;  // Not triple-buffering this.
    darray_push(pick_view_config.passes, world_pick_pass);

    // UI pick pass
    renderpass_config ui_pick_pass = {0};
    ui_pick_pass.name = "Renderpass.Builtin.UIPick";
    ui_pick_pass.render_area = (vec4){0, 0, (f32)config->start_width, (f32)config->start_height};
    ui_pick_pass.clear_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
    ui_pick_pass.clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
    ui_pick_pass.depth = 1.0f;
    ui_pick_pass.stencil = 0;
    ui_pick_pass.target.attachments = darray_create(render_target_attachment_config);

    render_target_attachment_config ui_pick_pass_attachment = {0};
    ui_pick_pass_attachment.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    // Obtain the attachment from the view.
    ui_pick_pass_attachment.source = RENDER_TARGET_ATTACHMENT_SOURCE_VIEW;
    ui_pick_pass_attachment.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    // Need to store it so it can be sampled afterward.
    ui_pick_pass_attachment.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    ui_pick_pass_attachment.present_after = false;
    darray_push(ui_pick_pass.target.attachments, ui_pick_pass_attachment);

    ui_pick_pass.target.attachment_count = darray_length(ui_pick_pass.target.attachments);
    ui_pick_pass.render_target_count = 1;  // Not triple-buffering this.
    darray_push(pick_view_config.passes, ui_pick_pass);

    pick_view_config.pass_count = darray_length(pick_view_config.passes);

    darray_push(config->render_views, pick_view_config);

    return true;
}

static b8 load_main_scene(struct application* game_inst) {
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    // TODO: temp load/prepare stuff
    if (!simple_scene_create(0, &state->main_scene)) {
        KERROR("Failed to create main scene");
        return false;
    }

    // Add objects to scene
    skybox_config sb_config = {0};
    sb_config.cubemap_name = "skybox";
    if (!skybox_create(sb_config, &state->sb)) {
        KERROR("Failed to create skybox, aborting game.");
        return false;
    }

    if (!simple_scene_add_skybox(&state->main_scene, &state->sb)) {
        KERROR("Failed to add skybox to main scene, aborting game.");
        return false;
    }

    // Load up a cube configuration, and load geometry from it.
    mesh_config cube_0_config = {0};
    cube_0_config.geometry_count = 1;
    cube_0_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    cube_0_config.g_configs[0] = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, "test_cube", "test_material");

    if (!mesh_create(cube_0_config, &state->meshes[0])) {
        KERROR("Failed to create mesh for cube 0");
        return false;
    }
    state->meshes[0].transform = transform_create();
    simple_scene_add_mesh(&state->main_scene, &state->meshes[0]);

    // Second cube
    mesh_config cube_1_config = {0};
    cube_1_config.geometry_count = 1;
    cube_1_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    cube_1_config.g_configs[0] = geometry_system_generate_cube_config(5.0f, 5.0f, 5.0f, 1.0f, 1.0f, "test_cube_2", "test_material");

    if (!mesh_create(cube_1_config, &state->meshes[1])) {
        KERROR("Failed to create mesh for cube 0");
        return false;
    }
    state->meshes[1].transform = transform_from_position((vec3){10.0f, 0.0f, 1.0f});
    transform_set_parent(&state->meshes[1].transform, &state->meshes[0].transform);

    simple_scene_add_mesh(&state->main_scene, &state->meshes[1]);

    // Third cube!
    mesh_config cube_2_config = {0};
    cube_2_config.geometry_count = 1;
    cube_2_config.g_configs = kallocate(sizeof(geometry_config), MEMORY_TAG_ARRAY);
    cube_2_config.g_configs[0] = geometry_system_generate_cube_config(2.0f, 2.0f, 2.0f, 1.0f, 1.0f, "test_cube_2", "test_material");

    if (!mesh_create(cube_2_config, &state->meshes[2])) {
        KERROR("Failed to create mesh for cube 0");
        return false;
    }
    state->meshes[2].transform = transform_from_position((vec3){5.0f, 0.0f, 1.0f});
    transform_set_parent(&state->meshes[2].transform, &state->meshes[1].transform);

    simple_scene_add_mesh(&state->main_scene, &state->meshes[2]);

    // Falcon
    mesh_config falcon_config = {0};
    falcon_config.resource_name = "falcon";
    if (!mesh_create(falcon_config, &state->meshes[3])) {
        KERROR("Failed to create falcon mesh.");
    } else {
        if (!simple_scene_add_mesh(&state->main_scene, &state->meshes[3])) {
            KERROR("Failed to load falcon mesh.");
        }

        state->meshes[3].transform = transform_from_position((vec3){15.0f, 0.0f, 1.0f});
    }

    // Sponza
    mesh_config sponza_config = {0};
    sponza_config.resource_name = "sponza";
    if (!mesh_create(sponza_config, &state->meshes[4])) {
        KERROR("Failed to create sponza mesh.");
    } else {
        if (!simple_scene_add_mesh(&state->main_scene, &state->meshes[4])) {
            KERROR("Failed to load sponza mesh.");
        }
        state->meshes[4].transform = transform_from_position_rotation_scale((vec3){15.0f, 0.0f, 1.0f}, quat_identity(), (vec3){0.05f, 0.05f, 0.05f});
    }

    // Lights
    state->dir_light = (directional_light){
        (vec4){0.4f, 0.4f, 0.2f, 1.0f},
        (vec4){-0.57735f, -0.57735f, -0.57735f, 0.0f}};

    if (!simple_scene_add_directional_light(&state->main_scene, &state->dir_light)) {
        KERROR("Failed to add directional light to main scene.");
    }

    state->p_lights[0].colour = (vec4){1.0f, 0.0f, 0.0f, 1.0f};
    state->p_lights[0].position = (vec4){-5.5f, 0.0f, -5.5f, 0.0f};
    state->p_lights[0].constant_f = 1.0f;
    state->p_lights[0].linear = 0.35f;
    state->p_lights[0].quadratic = 0.44f;
    state->p_lights[0].padding = 0;
    if (!simple_scene_add_point_light(&state->main_scene, &state->p_lights[0])) {
        KERROR("Failed to add point light to main scene.");
    }

    state->p_lights[1].colour = (vec4){0.0f, 1.0f, 0.0f, 1.0f};
    state->p_lights[1].position = (vec4){5.5f, 0.0f, -5.5f, 0.0f};
    state->p_lights[1].constant_f = 1.0f;
    state->p_lights[1].linear = 0.35f;
    state->p_lights[1].quadratic = 0.44f;
    state->p_lights[1].padding = 0;
    if (!simple_scene_add_point_light(&state->main_scene, &state->p_lights[1])) {
        KERROR("Failed to add point light to main scene.");
    }

    state->p_lights[2].colour = (vec4){0.0f, 0.0f, 1.0f, 1.0f};
    state->p_lights[2].position = (vec4){5.5f, 0.0f, 5.5f, 0.0f};
    state->p_lights[2].constant_f = 1.0f;
    state->p_lights[2].linear = 0.35f;
    state->p_lights[2].quadratic = 0.44f;
    state->p_lights[2].padding = 0;
    if (!simple_scene_add_point_light(&state->main_scene, &state->p_lights[2])) {
        KERROR("Failed to add point light to main scene.");
    }

    // Initialize
    if (!simple_scene_initialize(&state->main_scene)) {
        KERROR("Failed initialize main scene, aborting game.");
        return false;
    }

    // Actually load the scene.
    return simple_scene_load(&state->main_scene);
}
