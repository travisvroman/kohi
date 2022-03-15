#include "application.h"
#include "game_types.h"

#include "logger.h"

#include "platform/platform.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "core/input.h"
#include "core/clock.h"
#include "core/kstring.h"

#include "memory/linear_allocator.h"

#include "renderer/renderer_frontend.h"

// systems
#include "systems/texture_system.h"
#include "systems/material_system.h"
#include "systems/geometry_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

// TODO: temp
#include "math/kmath.h"
// TODO: end temp

typedef struct application_state {
    game* game_inst;
    b8 is_running;
    b8 is_suspended;
    i16 width;
    i16 height;
    clock clock;
    f64 last_time;
    linear_allocator systems_allocator;

    u64 event_system_memory_requirement;
    void* event_system_state;

    u64 logging_system_memory_requirement;
    void* logging_system_state;

    u64 input_system_memory_requirement;
    void* input_system_state;

    u64 platform_system_memory_requirement;
    void* platform_system_state;

    u64 resource_system_memory_requirement;
    void* resource_system_state;

    u64 shader_system_memory_requirement;
    void* shader_system_state;

    u64 renderer_system_memory_requirement;
    void* renderer_system_state;

    u64 texture_system_memory_requirement;
    void* texture_system_state;

    u64 material_system_memory_requirement;
    void* material_system_state;

    u64 geometry_system_memory_requirement;
    void* geometry_system_state;

    // TODO: temp
    geometry* test_geometry;
    geometry* test_ui_geometry;
    // TODO: end temp

} application_state;

static application_state* app_state;

// Event handlers
b8 application_on_event(u16 code, void* sender, void* listener_inst, event_context context);
b8 application_on_key(u16 code, void* sender, void* listener_inst, event_context context);
b8 application_on_resized(u16 code, void* sender, void* listener_inst, event_context context);

// TODO: temp
b8 event_on_debug_event(u16 code, void* sender, void* listener_inst, event_context data) {
    const char* names[3] = {
        "cobblestone",
        "paving",
        "paving2"};
    const char* spec_names[3] = {
        "cobblestone_SPEC",
        "paving_SPEC",
        "paving2_SPEC"};
    static i8 choice = 2;

    // Save off the old names.
    const char* old_name = names[choice];
    const char* old_spec_name = names[choice];

    choice++;
    choice %= 3;

    if (app_state->test_geometry) {
        // Acquire the new diffuse texture.
        app_state->test_geometry->material->diffuse_map.texture = texture_system_acquire(names[choice], true);
        if (!app_state->test_geometry->material->diffuse_map.texture) {
            KWARN("event_on_debug_event no diffuse texture! using default");
            app_state->test_geometry->material->diffuse_map.texture = texture_system_get_default_texture();
        }

        // Release the old diffuse texture.
        texture_system_release(old_name);

        // Acquire the new spec texture.
        app_state->test_geometry->material->specular_map.texture = texture_system_acquire(spec_names[choice], true);
        if (!app_state->test_geometry->material->specular_map.texture) {
            KWARN("event_on_debug_event no spec texture! using default");
            app_state->test_geometry->material->specular_map.texture = texture_system_get_default_specular_texture();
        }

        // Release the old spec texture.
        texture_system_release(old_spec_name);
    }

    return true;
}
// TODO: end temp

b8 application_create(game* game_inst) {
    if (game_inst->application_state) {
        KERROR("application_create called more than once.");
        return false;
    }

    // Memory system must be the first thing to be stood up.
    memory_system_configuration memory_system_config = {};
    memory_system_config.total_alloc_size = GIBIBYTES(1);
    if (!memory_system_initialize(memory_system_config)) {
        KERROR("Failed to initialize memory system; shutting down.");
        return false;
    }

    // Allocate the game state.
    game_inst->state = kallocate(game_inst->state_memory_requirement, MEMORY_TAG_GAME);

    // Stand up the application state.
    game_inst->application_state = kallocate(sizeof(application_state), MEMORY_TAG_APPLICATION);
    app_state = game_inst->application_state;
    app_state->game_inst = game_inst;
    app_state->is_running = false;
    app_state->is_suspended = false;

    // Create a linear allocator for all systems (except memory) to use.
    u64 systems_allocator_total_size = 64 * 1024 * 1024;  // 64 mb
    linear_allocator_create(systems_allocator_total_size, 0, &app_state->systems_allocator);

    // Initialize other subsystems.

    // Events
    event_system_initialize(&app_state->event_system_memory_requirement, 0);
    app_state->event_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->event_system_memory_requirement);
    event_system_initialize(&app_state->event_system_memory_requirement, app_state->event_system_state);

    // Logging
    initialize_logging(&app_state->logging_system_memory_requirement, 0);
    app_state->logging_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->logging_system_memory_requirement);
    if (!initialize_logging(&app_state->logging_system_memory_requirement, app_state->logging_system_state)) {
        KERROR("Failed to initialize logging system; shutting down.");
        return false;
    }

    // Input
    input_system_initialize(&app_state->input_system_memory_requirement, 0);
    app_state->input_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->input_system_memory_requirement);
    input_system_initialize(&app_state->input_system_memory_requirement, app_state->input_system_state);

    // Register for engine-level events.
    event_register(EVENT_CODE_APPLICATION_QUIT, 0, application_on_event);
    event_register(EVENT_CODE_KEY_PRESSED, 0, application_on_key);
    event_register(EVENT_CODE_KEY_RELEASED, 0, application_on_key);
    event_register(EVENT_CODE_RESIZED, 0, application_on_resized);
    // TODO: temp
    event_register(EVENT_CODE_DEBUG0, 0, event_on_debug_event);
    // TODO: end temp

    // Platform
    platform_system_startup(&app_state->platform_system_memory_requirement, 0, 0, 0, 0, 0, 0);
    app_state->platform_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->platform_system_memory_requirement);
    if (!platform_system_startup(
            &app_state->platform_system_memory_requirement,
            app_state->platform_system_state,
            game_inst->app_config.name,
            game_inst->app_config.start_pos_x,
            game_inst->app_config.start_pos_y,
            game_inst->app_config.start_width,
            game_inst->app_config.start_height)) {
        return false;
    }

    // Resource system.
    resource_system_config resource_sys_config;
    resource_sys_config.asset_base_path = "../assets";
    resource_sys_config.max_loader_count = 32;
    resource_system_initialize(&app_state->resource_system_memory_requirement, 0, resource_sys_config);
    app_state->resource_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->resource_system_memory_requirement);
    if (!resource_system_initialize(&app_state->resource_system_memory_requirement, app_state->resource_system_state, resource_sys_config)) {
        KFATAL("Failed to initialize resource system. Aborting application.");
        return false;
    }

    // Shader system
    shader_system_config shader_sys_config;
    shader_sys_config.max_shader_count = 1024;
    shader_sys_config.max_uniform_count = 128;
    shader_sys_config.max_global_textures = 31;
    shader_sys_config.max_instance_textures = 31;
    shader_system_initialize(&app_state->shader_system_memory_requirement, 0, shader_sys_config);
    app_state->shader_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->shader_system_memory_requirement);
    if (!shader_system_initialize(&app_state->shader_system_memory_requirement, app_state->shader_system_state, shader_sys_config)) {
        KFATAL("Failed to initialize shader system. Aborting application.");
        return false;
    }

    // Renderer system
    renderer_system_initialize(&app_state->renderer_system_memory_requirement, 0, 0);
    app_state->renderer_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->renderer_system_memory_requirement);
    if (!renderer_system_initialize(&app_state->renderer_system_memory_requirement, app_state->renderer_system_state, game_inst->app_config.name)) {
        KFATAL("Failed to initialize renderer. Aborting application.");
        return false;
    }

    // Texture system.
    texture_system_config texture_sys_config;
    texture_sys_config.max_texture_count = 65536;
    texture_system_initialize(&app_state->texture_system_memory_requirement, 0, texture_sys_config);
    app_state->texture_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->texture_system_memory_requirement);
    if (!texture_system_initialize(&app_state->texture_system_memory_requirement, app_state->texture_system_state, texture_sys_config)) {
        KFATAL("Failed to initialize texture system. Application cannot continue.");
        return false;
    }

    // Material system.
    material_system_config material_sys_config;
    material_sys_config.max_material_count = 4096;
    material_system_initialize(&app_state->material_system_memory_requirement, 0, material_sys_config);
    app_state->material_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->material_system_memory_requirement);
    if (!material_system_initialize(&app_state->material_system_memory_requirement, app_state->material_system_state, material_sys_config)) {
        KFATAL("Failed to initialize material system. Application cannot continue.");
        return false;
    }

    // Geometry system.
    geometry_system_config geometry_sys_config;
    geometry_sys_config.max_geometry_count = 4096;
    geometry_system_initialize(&app_state->geometry_system_memory_requirement, 0, geometry_sys_config);
    app_state->geometry_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->geometry_system_memory_requirement);
    if (!geometry_system_initialize(&app_state->geometry_system_memory_requirement, app_state->geometry_system_state, geometry_sys_config)) {
        KFATAL("Failed to initialize geometry system. Application cannot continue.");
        return false;
    }

    // TODO: temp

    // Load up a plane configuration, and load geometry from it.
    geometry_config g_config = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, "test_cube", "test_material");
    app_state->test_geometry = geometry_system_acquire_from_config(g_config, true);

    // Clean up the allocations for the geometry config.
    kfree(g_config.vertices, sizeof(vertex_3d) * g_config.vertex_count, MEMORY_TAG_ARRAY);
    kfree(g_config.indices, sizeof(u32) * g_config.index_count, MEMORY_TAG_ARRAY);

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
    app_state->test_ui_geometry = geometry_system_acquire_from_config(ui_config, true);

    // Load up default geometry.
    // app_state->test_geometry = geometry_system_get_default();
    // TODO: end temp

    // Initialize the game.
    if (!app_state->game_inst->initialize(app_state->game_inst)) {
        KFATAL("Game failed to initialize.");
        return false;
    }

    // Call resize once to ensure the proper size has been set.
    app_state->game_inst->on_resize(app_state->game_inst, app_state->width, app_state->height);

    return true;
}

b8 application_run() {
    app_state->is_running = true;
    clock_start(&app_state->clock);
    clock_update(&app_state->clock);
    app_state->last_time = app_state->clock.elapsed;
    // f64 running_time = 0;
    u8 frame_count = 0;
    f64 target_frame_seconds = 1.0f / 60;

    KINFO(get_memory_usage_str());

    while (app_state->is_running) {
        if (!platform_pump_messages()) {
            app_state->is_running = false;
        }

        if (!app_state->is_suspended) {
            // Update clock and get delta time.
            clock_update(&app_state->clock);
            f64 current_time = app_state->clock.elapsed;
            f64 delta = (current_time - app_state->last_time);
            f64 frame_start_time = platform_get_absolute_time();

            if (!app_state->game_inst->update(app_state->game_inst, (f32)delta)) {
                KFATAL("Game update failed, shutting down.");
                app_state->is_running = false;
                break;
            }

            // Call the game's render routine.
            if (!app_state->game_inst->render(app_state->game_inst, (f32)delta)) {
                KFATAL("Game render failed, shutting down.");
                app_state->is_running = false;
                break;
            }

            // TODO: refactor packet creation
            render_packet packet;
            packet.delta_time = delta;

            // TODO: temp
            geometry_render_data test_render;
            test_render.geometry = app_state->test_geometry;
            // test_render.model = mat4_identity();
            static f32 angle = 0;
            // angle = deg_to_rad(45.0f);
            angle += (.5f * delta);
            // TODO: Something with rotation matrices is messing up directional lighting,
            // in particular on the x-axis it seems. It's fine before rotation.
            quat rotation = quat_from_axis_angle((vec3){0, 1, 0}, angle, false);
            mat4 t = mat4_translation(vec3_zero());
            mat4 r = quat_to_mat4(rotation);  //  quat_to_rotation_matrix(rotation, vec3_zero());
            mat4 s = mat4_scale(vec3_one());
            t = mat4_mul(r, t);
            t = mat4_mul(s, t);
            test_render.model = t;

            packet.geometry_count = 1;
            packet.geometries = &test_render;

            geometry_render_data test_ui_render;
            test_ui_render.geometry = app_state->test_ui_geometry;
            test_ui_render.model = mat4_translation((vec3){0, 0, 0});
            packet.ui_geometry_count = 1;
            packet.ui_geometries = &test_ui_render;
            // TODO: end temp

            renderer_draw_frame(&packet);

            // Figure out how long the frame took and, if below
            f64 frame_end_time = platform_get_absolute_time();
            f64 frame_elapsed_time = frame_end_time - frame_start_time;
            // running_time += frame_elapsed_time;
            f64 remaining_seconds = target_frame_seconds - frame_elapsed_time;

            if (remaining_seconds > 0) {
                u64 remaining_ms = (remaining_seconds * 1000);

                // If there is time left, give it back to the OS.
                b8 limit_frames = false;
                if (remaining_ms > 0 && limit_frames) {
                    platform_sleep(remaining_ms - 1);
                }

                frame_count++;
            }

            // NOTE: Input update/state copying should always be handled
            // after any input should be recorded; I.E. before this line.
            // As a safety, input is the last thing to be updated before
            // this frame ends.
            input_update(delta);

            // Update last time
            app_state->last_time = current_time;
        }
    }

    app_state->is_running = false;

    // Shutdown event system.
    event_unregister(EVENT_CODE_APPLICATION_QUIT, 0, application_on_event);
    event_unregister(EVENT_CODE_KEY_PRESSED, 0, application_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, 0, application_on_key);
    // TODO: temp
    event_unregister(EVENT_CODE_DEBUG0, 0, event_on_debug_event);
    // TODO: end temp

    input_system_shutdown(app_state->input_system_state);

    geometry_system_shutdown(app_state->geometry_system_state);

    material_system_shutdown(app_state->material_system_state);

    texture_system_shutdown(app_state->texture_system_state);

    shader_system_shutdown(app_state->shader_system_state);

    renderer_system_shutdown(app_state->renderer_system_state);

    resource_system_shutdown(app_state->resource_system_state);

    platform_system_shutdown(app_state->platform_system_state);

    event_system_shutdown(app_state->event_system_state);

    memory_system_shutdown();

    return true;
}

void application_get_framebuffer_size(u32* width, u32* height) {
    *width = app_state->width;
    *height = app_state->height;
}

b8 application_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    switch (code) {
        case EVENT_CODE_APPLICATION_QUIT: {
            KINFO("EVENT_CODE_APPLICATION_QUIT recieved, shutting down.\n");
            app_state->is_running = false;
            return true;
        }
    }

    return false;
}

b8 application_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_KEY_PRESSED) {
        u16 key_code = context.data.u16[0];
        if (key_code == KEY_ESCAPE) {
            // NOTE: Technically firing an event to itself, but there may be other listeners.
            event_context data = {};
            event_fire(EVENT_CODE_APPLICATION_QUIT, 0, data);

            // Block anything else from processing this.
            return true;
        } else if (key_code == KEY_A) {
            // Example on checking for a key
            KDEBUG("Explicit - A key pressed!");
        } else {
            KDEBUG("'%c' key pressed in window.", key_code);
        }
    } else if (code == EVENT_CODE_KEY_RELEASED) {
        u16 key_code = context.data.u16[0];
        if (key_code == KEY_B) {
            // Example on checking for a key
            KDEBUG("Explicit - B key released!");
        } else {
            KDEBUG("'%c' key released in window.", key_code);
        }
    }
    return false;
}

b8 application_on_resized(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_RESIZED) {
        u16 width = context.data.u16[0];
        u16 height = context.data.u16[1];

        // Check if different. If so, trigger a resize event.
        if (width != app_state->width || height != app_state->height) {
            app_state->width = width;
            app_state->height = height;

            KDEBUG("Window resize: %i, %i", width, height);

            // Handle minimization
            if (width == 0 || height == 0) {
                KINFO("Window minimized, suspending application.");
                app_state->is_suspended = true;
                return true;
            } else {
                if (app_state->is_suspended) {
                    KINFO("Window restored, resuming application.");
                    app_state->is_suspended = false;
                }
                app_state->game_inst->on_resize(app_state->game_inst, width, height);
                renderer_on_resized(width, height);
            }
        }
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}