#include "engine.h"

// Version reporting
#include "kohi.runtime_version.h"

#include "application/application_config.h"
#include "application/application_types.h"
#include "console.h"
#include "containers/darray.h"
#include "containers/registry.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kvar.h"
#include "core/metrics.h"
#include "frame_data.h"
#include "identifiers/khandle.h"
#include "identifiers/uuid.h"
#include "logger.h"
#include "memory/allocators/linear_allocator.h"
#include "memory/kmemory.h"
#include "platform/platform.h"
#include "plugins/plugin_types.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "strings/kstring.h"
#include "systems/plugin_system.h"
#include "time/kclock.h"

// systems
#include "systems/audio_system.h"
#include "systems/camera_system.h"
#include "systems/font_system.h"
#include "systems/geometry_system.h"
#include "systems/job_system.h"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include "systems/timeline_system.h"
#include "systems/xform_system.h"

struct kwindow;

typedef struct engine_state_t {
    application* game_inst;
    b8 is_running;
    b8 is_suspended;
    kclock clock;
    f64 last_time;

    // An allocator used for per-frame allocations, that is reset every frame.
    linear_allocator frame_allocator;

    frame_data p_frame_data;

    // Platform console consumer.
    u8 platform_consumer_id;
    // Log file console consumer.
    u8 logfile_consumer_id;
    // Log file handle.
    file_handle log_file_handle;

    // Engine system states.
    engine_system_states systems;

    // External system state registry.
    kregistry external_systems_registry;

    kruntime_plugin* renderer_plugin;
    kruntime_plugin* audio_plugin;

    // darray List of created windows.
    kwindow* windows;

} engine_state_t;

static engine_state_t* engine_state;

// frame allocator functions.
static void* frame_allocator_allocate(u64 size) {
    if (!engine_state) {
        return 0;
    }

    return linear_allocator_allocate(&engine_state->frame_allocator, size);
}
static void frame_allocator_free(void* block, u64 size) {
    // NOTE: Linear allocator doesn't free, so this is a no-op
    /* if (engine_state) {
    } */
}
static void frame_allocator_free_all(void) {
    if (engine_state) {
        // Don't wipe the memory each time, to save on performance.
        linear_allocator_free_all(&engine_state->frame_allocator, false);
    }
}

// Event handlers
static b8 engine_on_event(u16 code, void* sender, void* listener_inst, event_context context);
static void engine_on_filewatcher_file_deleted(u32 watcher_id);
static void engine_on_filewatcher_file_written(u32 watcher_id);
static void engine_on_window_closed(const struct kwindow* window);
static void engine_on_window_resized(const struct kwindow* window);
static void engine_on_process_key(keys key, b8 pressed);
static void engine_on_process_mouse_button(mouse_buttons button, b8 pressed);
static void engine_on_process_mouse_move(i16 x, i16 y);
static void engine_on_process_mouse_wheel(i8 z_delta);
static b8 engine_log_file_write(void* engine, log_level level, const char* message);
static b8 engine_platform_console_write(void* platform, log_level level, const char* message);

b8 engine_create(application* game_inst) {
    if (game_inst->engine_state) {
        KERROR("engine_create called more than once.");
        return false;
    }

    // Memory system must be the first thing to be stood up.
    memory_system_configuration memory_system_config = {};
    memory_system_config.total_alloc_size = GIBIBYTES(2);
    if (!memory_system_initialize(memory_system_config)) {
        KERROR("Failed to initialize memory system; shutting down.");
        return false;
    }

    // Seed the uuid generator.
    // TODO: A better seed here.
    uuid_seed(101);

    // Metrics
    metrics_initialize();

    // Stand up the engine state.
    game_inst->engine_state = kallocate(sizeof(engine_state_t), MEMORY_TAG_ENGINE);
    engine_state = game_inst->engine_state;
    engine_state->game_inst = game_inst;
    engine_state->is_running = false;
    engine_state->is_suspended = false;

    // Setup a registry for external systems to register themselves to.
    kregistry_create(&engine_state->external_systems_registry);

    // Engine systems
    engine_system_states* systems = &engine_state->systems;

    // Platform initialization first. NOTE: NOT window creation - that should happen much later.
    {
        platform_system_config plat_config = {0};
        plat_config.application_name = game_inst->app_config.name;
        systems->platform_memory_requirement = 0;
        platform_system_startup(&systems->platform_memory_requirement, 0, &plat_config);
        systems->platform_system = kallocate(systems->platform_memory_requirement, MEMORY_TAG_ENGINE);
        if (!platform_system_startup(&systems->platform_memory_requirement, systems->platform_system, &plat_config)) {
            KERROR("Failed to initialize platform layer.");
            return false;
        }
    }

    // Console system
    {
        console_initialize(&systems->console_memory_requirement, 0, 0);
        systems->console_system = kallocate(systems->console_memory_requirement, MEMORY_TAG_ENGINE);
        if (!console_initialize(&systems->console_memory_requirement, systems->console_system, 0)) {
            KERROR("Failed to initialize console.");
            return false;
        }

        // Platform should then register as a console consumer.
        console_consumer_register(systems->platform_system, engine_platform_console_write, &engine_state->platform_consumer_id);
        // Setup the engine as another console consumer, which now owns the "console.log" file.
        // Create new/wipe existing log file, then open it.
        const char* log_filename = "console.log";
        if (!filesystem_open(log_filename, FILE_MODE_WRITE, false, &engine_state->log_file_handle)) {
            KFATAL("Unable to open '%s' for writing.", log_filename);
            return false;
        }
        console_consumer_register(engine_state, engine_log_file_write, &engine_state->logfile_consumer_id);
    }

    // Report runtime version
#if KRELEASE
    const char* build_type = "Release";
#else
    const char* build_type = "Debug";
#endif
    KINFO("Kohi Runtime v. %s (%s)", KVERSION, build_type);

    // Plugin system
    {
        // Get the generic config from application config first.
        application_system_config generic_sys_config = {0};
        if (!application_config_system_config_get(&game_inst->app_config, "plugin_system", &generic_sys_config)) {
            KERROR("No configuration exists in app config for the plugin system. This configuration is required.");
            return false;
        }

        // Parse plugin system config from app config.
        plugin_system_config plugin_sys_config = {0};
        if (!plugin_system_deserialize_config(generic_sys_config.configuration_str, &plugin_sys_config)) {
            KERROR("Failed to deserialize plugin system config, which is required.");
            return false;
        }

        plugin_system_intialize(&systems->plugin_system_memory_requirement, 0, &plugin_sys_config);
        systems->plugin_system = kallocate(systems->plugin_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!plugin_system_intialize(&systems->plugin_system_memory_requirement, systems->plugin_system, &plugin_sys_config)) {
            KERROR("Failed to initialize plugin system.");
            return false;
        }
    }

    // KVar system
    {
        kvar_system_initialize(&systems->kvar_system_memory_requirement, 0, 0);
        systems->kvar_system = kallocate(systems->kvar_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!kvar_system_initialize(&systems->kvar_system_memory_requirement, systems->kvar_system, 0)) {
            KERROR("Failed to initialize KVar system.");
            return false;
        }
    }

    // Event system.
    {
        event_system_initialize(&systems->event_system_memory_requirement, 0, 0);
        systems->event_system = kallocate(systems->event_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!event_system_initialize(&systems->event_system_memory_requirement, systems->event_system, 0)) {
            KERROR("Failed to initialize event system.");
            return false;
        }

        // After event system, register watcher and input callbacks.
        platform_register_watcher_deleted_callback(engine_on_filewatcher_file_deleted);
        platform_register_watcher_written_callback(engine_on_filewatcher_file_written);
        platform_register_window_closed_callback(engine_on_window_closed);
        platform_register_window_resized_callback(engine_on_window_resized);
    }

    // Input system.
    {
        input_system_initialize(&systems->input_system_memory_requirement, 0, 0);
        systems->input_system = kallocate(systems->input_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!input_system_initialize(&systems->input_system_memory_requirement, systems->input_system, 0)) {
            KERROR("Failed to initialize input system.");
            return false;
        }

        // Register input hooks with platform (i.e. handle_key/handle_button, etc.).
        platform_register_process_key(engine_on_process_key);
        platform_register_process_mouse_button_callback(engine_on_process_mouse_button);
        platform_register_process_mouse_move_callback(engine_on_process_mouse_move);
        platform_register_process_mouse_wheel_callback(engine_on_process_mouse_wheel);
    }

    // Resource system.
    {
        resource_system_config resource_sys_config = {0};
        resource_sys_config.asset_base_path = "../testbed.assets"; // TODO: The application should probably configure this.
        resource_sys_config.max_loader_count = 32;
        resource_system_initialize(&systems->resource_system_memory_requirement, 0, &resource_sys_config);
        systems->resource_system = kallocate(systems->resource_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!resource_system_initialize(&systems->resource_system_memory_requirement, systems->resource_system, &resource_sys_config)) {
            KERROR("Failed to iniitialize resource system.");
            return false;
        }
    }

    // Renderer system
    {
        // Get the generic config from application config first.
        application_system_config generic_sys_config = {0};
        if (!application_config_system_config_get(&game_inst->app_config, "renderer", &generic_sys_config)) {
            KERROR("No configuration exists in app config for the renderer system. This configuration is required.");
            return false;
        }

        // Parse plugin system config from app config.
        renderer_system_config renderer_sys_config = {0};
        if (!renderer_system_deserialize_config(generic_sys_config.configuration_str, &renderer_sys_config)) {
            KERROR("Failed to deserialize renderer system config, which is required.");
            return false;
        }

        renderer_system_initialize(&systems->renderer_system_memory_requirement, 0, &renderer_sys_config);
        systems->renderer_system = kallocate(systems->renderer_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!renderer_system_initialize(&systems->renderer_system_memory_requirement, systems->renderer_system, &renderer_sys_config)) {
            KERROR("Failed to initialize renderer system.");
            return false;
        }
    }

    // Reach into platform and open new window(s) in accordance with app config.
    // Notify renderer of window(s)/setup surface(s), etc.
    u32 window_count = darray_length(game_inst->app_config.windows);
    if (window_count > 1) {
        KFATAL("Multiple windows are not yet implemented at the engine level. Please just stick to one for now.");
        return false;
    }

    engine_state->windows = darray_create(kwindow);
    for (u32 i = 0; i < window_count; ++i) {
        kwindow_config* window_config = &game_inst->app_config.windows[i];
        kwindow new_window = {0};
        new_window.name = string_duplicate(window_config->name);
        // Add to tracked window list
        darray_push(engine_state->windows, new_window);

        kwindow* window = &engine_state->windows[(darray_length(engine_state->windows) - 1)];
        if (!platform_window_create(window_config, window, true)) {
            KERROR("Failed to create window '%s'.", window_config->name);
            return false;
        }

        // Tell the renderer about the window.
        if (!renderer_on_window_created(engine_state->systems.renderer_system, window)) {
            KERROR("The renderer failed to create resources for the window '%s.", window_config->name);
            return false;
        }

        // Manually call to make sure window is of the right size/viewports and such are the right size.
        renderer_on_window_resized(engine_state->systems.renderer_system, window);
    }

    // Job system
    {
        b8 renderer_multithreaded = renderer_is_multithreaded();

        // This is really a core count. Subtract 1 to account for the main thread already being in use.
        i32 thread_count = platform_get_processor_count() - 1;
        if (thread_count < 1) {
            KFATAL("Error: Platform reported processor count (minus one for main thread) as %i. Need at least one additional thread for the job system.", thread_count);
            return false;
        } else {
            KTRACE("Available threads: %i", thread_count);
        }

        // Cap the thread count.
        const i32 max_thread_count = 15;
        if (thread_count > max_thread_count) {
            KTRACE("Available threads on the system is %i, but will be capped at %i.", thread_count, max_thread_count);
            thread_count = max_thread_count;
        }

        // Initialize the job system.
        // Requires knowledge of renderer multithread support, so should be initialized here.
        u32 job_thread_types[15];
        for (u32 i = 0; i < 15; ++i) {
            job_thread_types[i] = JOB_TYPE_GENERAL;
        }

        if (max_thread_count == 1 || !renderer_multithreaded) {
            // Everything on one job thread.
            job_thread_types[0] |= (JOB_TYPE_GPU_RESOURCE | JOB_TYPE_RESOURCE_LOAD);
        } else if (max_thread_count == 2) {
            // Split things between the 2 threads
            job_thread_types[0] |= JOB_TYPE_GPU_RESOURCE;
            job_thread_types[1] |= JOB_TYPE_RESOURCE_LOAD;
        } else {
            // Dedicate the first 2 threads to these things, pass off general tasks to other threads.
            job_thread_types[0] = JOB_TYPE_GPU_RESOURCE;
            job_thread_types[1] = JOB_TYPE_RESOURCE_LOAD;
        }

        job_system_config job_sys_config = {0};
        job_sys_config.max_job_thread_count = thread_count;
        job_sys_config.type_masks = job_thread_types;
        job_system_initialize(&systems->job_system_memory_requirement, 0, &job_sys_config);
        systems->job_system = kallocate(systems->job_system_memory_requirement, MEMORY_TAG_ENGINE);

        if (!job_system_initialize(&systems->job_system_memory_requirement, systems->job_system, &job_sys_config)) {
            KERROR("Failed to initialize job system.");
            return false;
        }
    }

    // Audio system
    {

        // Get the generic config from application config first.
        application_system_config generic_sys_config = {0};
        if (!application_config_system_config_get(&game_inst->app_config, "audio", &generic_sys_config)) {
            // TODO: Maybe audio shouldn't be required?
            KERROR("No configuration exists in app config for the audio system. This configuration is required.");
            return false;
        }

        audio_system_config audio_sys_config = {0};

        // Parse system config from app config.
        if (!audio_system_deserialize_config(generic_sys_config.configuration_str, &audio_sys_config)) {
            KERROR("Failed to deserialize audio system config, which is required.");
            return false;
        }

        audio_system_initialize(&systems->audio_system_memory_requirement, 0, &audio_sys_config);
        systems->audio_system = kallocate(systems->audio_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!audio_system_initialize(&systems->audio_system_memory_requirement, systems->audio_system, &audio_sys_config)) {
            KERROR("Failed to initialize audio system.");
            return false;
        }
    }

    // xform
    {
        xform_system_config xform_sys_config = {0};
        xform_sys_config.initial_slot_count = 128;
        xform_system_initialize(&systems->xform_system_memory_requirement, 0, &xform_sys_config);
        systems->xform_system = kallocate(systems->xform_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!xform_system_initialize(&systems->xform_system_memory_requirement, systems->xform_system, &xform_sys_config)) {
            KERROR("Failed to intialize xform system.");
            return false;
        }
    }

    // Timeline
    {
        timeline_system_config timeline_config = {0};
        timeline_config.dummy = 1;
        timeline_system_initialize(&systems->timeline_system_memory_requirement, 0, 0);
        systems->timeline_system = kallocate(systems->timeline_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!timeline_system_initialize(&systems->timeline_system_memory_requirement, systems->timeline_system, &timeline_config)) {
            KERROR("Failed to initialize timeline system.");
            return false;
        }
    }

    // Shader system
    {
        shader_system_config shader_sys_config;
        shader_sys_config.max_shader_count = 1024;
        shader_sys_config.max_uniform_count = 128;
        shader_sys_config.max_global_textures = 31;
        shader_sys_config.max_instance_textures = 31;
        shader_system_initialize(&systems->shader_system_memory_requirement, 0, &shader_sys_config);
        systems->shader_system = kallocate(systems->shader_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!shader_system_initialize(&systems->shader_system_memory_requirement, systems->shader_system, &shader_sys_config)) {
            KERROR("Failed to initialize shader system.");
            return false;
        }
    }

    // Texture system
    {
        texture_system_config texture_sys_config;
        texture_sys_config.max_texture_count = 65536;
        texture_system_initialize(&systems->texture_system_memory_requirement, 0, &texture_sys_config);
        systems->texture_system = kallocate(systems->texture_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!texture_system_initialize(&systems->texture_system_memory_requirement, systems->texture_system, &texture_sys_config)) {
            KERROR("Failed to initialize texture system.");
            return false;
        }
    }

    // Material system
    {
        material_system_config material_sys_config = {0};
        material_sys_config.max_material_count = 4096;
        material_system_initialize(&systems->material_system_memory_requirement, 0, &material_sys_config);
        systems->material_system = kallocate(systems->material_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!material_system_initialize(&systems->material_system_memory_requirement, systems->material_system, &material_sys_config)) {
            KERROR("Failed to initialize material system.");
            return false;
        }
    }

    // Font system
    {
        // Get the generic config from application config first.
        application_system_config generic_sys_config = {0};
        if (!application_config_system_config_get(&game_inst->app_config, "font", &generic_sys_config)) {
            KERROR("No configuration exists in app config for the font system. This configuration is required.");
            return false;
        }

        font_system_config font_sys_config = {0};

        // Parse system config from app config.
        if (!font_system_deserialize_config(generic_sys_config.configuration_str, &font_sys_config)) {
            KERROR("Failed to deserialize font system config, which is required.");
            return false;
        }

        font_system_initialize(&systems->font_system_memory_requirement, 0, &font_sys_config);
        systems->font_system = kallocate(systems->font_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!font_system_initialize(&systems->font_system_memory_requirement, systems->font_system, &font_sys_config)) {
            KERROR("Failed to initialize font system.");
            return false;
        }
    }

    // Geometry system
    {
        geometry_system_config geometry_sys_config = {0};
        geometry_sys_config.max_geometry_count = 4096;
        geometry_system_initialize(&systems->geometry_system_memory_requirement, 0, &geometry_sys_config);
        systems->geometry_system = kallocate(systems->geometry_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!geometry_system_initialize(&systems->geometry_system_memory_requirement, systems->geometry_system, &geometry_sys_config)) {
            KERROR("Failed to initialize geometry system.");
            return false;
        }
    }

    // Light system
    {
        light_system_initialize(&systems->light_system_memory_requirement, 0, 0);
        systems->light_system = kallocate(systems->light_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!light_system_initialize(&systems->light_system_memory_requirement, systems->light_system, 0)) {
            KERROR("Failed to initialize light system.");
            return false;
        }
    }

    // Camera system
    {
        camera_system_config camera_sys_config = {0};
        camera_sys_config.max_camera_count = 61;
        camera_system_initialize(&systems->camera_system_memory_requirement, 0, &camera_sys_config);
        systems->camera_system = kallocate(systems->camera_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!camera_system_initialize(&systems->camera_system_memory_requirement, systems->camera_system, &camera_sys_config)) {
            KERROR("Failed to initialize camera system.");
            return false;
        }
    }

    // Rendergraph system
    {
        rendergraph_system_initialize(&systems->rendergraph_system_memory_requirement, 0);
        systems->rendergraph_system = kallocate(systems->rendergraph_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!rendergraph_system_initialize(&systems->rendergraph_system_memory_requirement, systems->rendergraph_system)) {
            KERROR("Failed to initialize rendergraph system.");
            return false;
        }
    }

    // NOTE: Boot sequence =======================================================================================================
    // Perform the application's boot sequence.
    game_inst->stage = APPLICATION_STAGE_BOOTING;
    if (!game_inst->boot(game_inst)) {
        KFATAL("Game boot sequence failed; aborting application.");
        return false;
    }

    // NOTE: End boot application sequence.

    // Post-boot plugin init
    if (!plugin_system_initialize_plugins(engine_state->systems.plugin_system)) {
        KERROR("Plugin(s) failed initialization. See logs for details.");
        return false;
    }

    // TODO: Handle post-boot items in systems that require app config.
    //
    // TODO: font system
    // TODO: Load fonts as configured in app config. in post-boot
    // &game_inst->app_config->font_config

    // Setup the frame allocator.
    linear_allocator_create(game_inst->app_config.frame_allocator_size, 0, &engine_state->frame_allocator);
    engine_state->p_frame_data.allocator.allocate = frame_allocator_allocate;
    engine_state->p_frame_data.allocator.free = frame_allocator_free;
    engine_state->p_frame_data.allocator.free_all = frame_allocator_free_all;

    // Allocate for the application's frame data.
    if (game_inst->app_config.app_frame_data_size > 0) {
        engine_state->p_frame_data.application_frame_data = kallocate(game_inst->app_config.app_frame_data_size, MEMORY_TAG_GAME);
    } else {
        engine_state->p_frame_data.application_frame_data = 0;
    }

    game_inst->stage = APPLICATION_STAGE_BOOT_COMPLETE;

    // Initialize the game.
    game_inst->stage = APPLICATION_STAGE_INITIALIZING;
    if (!engine_state->game_inst->initialize(engine_state->game_inst)) {
        KFATAL("Game failed to initialize.");
        return false;
    }
    game_inst->stage = APPLICATION_STAGE_INITIALIZED;

    return true;
}

b8 engine_run(application* game_inst) {
    game_inst->stage = APPLICATION_STAGE_RUNNING;
    engine_state->is_running = true;
    kclock_start(&engine_state->clock);
    kclock_update(&engine_state->clock);
    engine_state->last_time = engine_state->clock.elapsed;
    // f64 running_time = 0;
    // TODO: frame rate lock
    // u8 frame_count = 0;
    f64 target_frame_seconds = 1.0f / 60;
    f64 frame_elapsed_time = 0;

    char* mem_usage = get_memory_usage_str();
    KINFO(mem_usage);
    string_free(mem_usage);

    // FIXME: Need a better way to select the active window.
    kwindow* w = &engine_state->windows[0];

    // FIXME: The event loop in the platform layer depends on active window.
    // In theory this means there should be one of these loops per window.
    while (engine_state->is_running) {
        if (!platform_pump_messages()) {
            engine_state->is_running = false;
        }

        if (!engine_state->is_suspended) {
            // Update clock and get delta time.
            kclock_update(&engine_state->clock);
            f64 current_time = engine_state->clock.elapsed;
            f64 delta = (current_time - engine_state->last_time);
            f64 frame_start_time = platform_get_absolute_time();

            // Reset the frame allocator
            engine_state->p_frame_data.allocator.free_all();

            // TODO: Update systems here that need them.
            job_system_update(engine_state->systems.job_system, &engine_state->p_frame_data);
            plugin_system_update_plugins(engine_state->systems.plugin_system, &engine_state->p_frame_data);

            // Update timelines. Note that this is not done by the systems manager
            // because we don't want or have timeline data in the frame_data struct any longer.
            timeline_system_update(engine_state->systems.timeline_system, delta);

            // update metrics
            metrics_update(frame_elapsed_time);

            if (!renderer_frame_prepare(engine_state->systems.renderer_system, &engine_state->p_frame_data)) {
                continue;
            }

            // Make sure the window is not currently being resized by waiting a designated
            // number of frames after the last resize operation before performing the backend updates.
            if (w->resizing) {
                w->frames_since_resize++;

                // If the required number of frames have passed since the resize, go ahead and perform the actual updates.
                // FIXME: Configurable delay here instead of magic 30 frames.
                if (w->frames_since_resize >= 30) {
                    renderer_on_window_resized(engine_state->systems.renderer_system, w);

                    // NOTE: Don't bother checking the result of this, since this will likely
                    // recreate the swapchain and boot to the next frame anyway.
                    renderer_frame_prepare_window_surface(engine_state->systems.renderer_system, w, &engine_state->p_frame_data);

                    // Notify the application of the resize.
                    engine_state->game_inst->on_window_resize(engine_state->game_inst, w);

                    w->frames_since_resize = 0;
                    w->resizing = false;
                } else {
                    // Skip rendering the frame and try again next time.
                    // NOTE: Simulate a frame being "drawn" at 60 FPS.
                    platform_sleep(16);
                }

                // Either way, don't process this frame any further while resizing.
                // Try again next frame.
                continue;
            }
            if (!renderer_frame_prepare_window_surface(engine_state->systems.renderer_system, w, &engine_state->p_frame_data)) {
                // This can also happen not just from a resize above, but also if a renderer flag
                // (such as VSync) changed, which may also require resource recreation. To handle this,
                // Notify the application of a resize event, which it can then pass on to its rendergraph(s)
                // as needed.
                engine_state->game_inst->on_window_resize(engine_state->game_inst, w);
                continue;
            }

            if (!engine_state->game_inst->update(engine_state->game_inst, &engine_state->p_frame_data)) {
                KFATAL("Game update failed, shutting down.");
                engine_state->is_running = false;
                break;
            }

            // Start recording to the command list.
            if (!renderer_frame_command_list_begin(engine_state->systems.renderer_system, &engine_state->p_frame_data)) {
                KFATAL("Failed to begin renderer command list. Shutting down.");
                engine_state->is_running = false;
                break;
            }

            // Begin "prepare_frame" render event grouping.
            renderer_begin_debug_label("prepare_frame", (vec3){1.0f, 1.0f, 0.0f});

            // TODO: frame prepare for systems that need it.
            // NOTE: Frame preparation for plugins
            plugin_system_frame_prepare_plugins(engine_state->systems.plugin_system, &engine_state->p_frame_data);

            // Have the application generate the render packet.
            b8 prepare_result = engine_state->game_inst->prepare_frame(engine_state->game_inst, &engine_state->p_frame_data);
            // End "prepare_frame" render event grouping.
            renderer_end_debug_label();

            if (!prepare_result) {
                continue;
            }

            // Call the game's render routine.
            if (!engine_state->game_inst->render_frame(engine_state->game_inst, &engine_state->p_frame_data)) {
                KFATAL("Game render failed, shutting down.");
                engine_state->is_running = false;
                break;
            }

            // End the recording to the command list.
            if (!renderer_frame_command_list_end(engine_state->systems.renderer_system, &engine_state->p_frame_data)) {
                KFATAL("Failed to end renderer command list. Shutting down.");
                engine_state->is_running = false;
                break;
            }

            if (!renderer_frame_submit(engine_state->systems.renderer_system, &engine_state->p_frame_data)) {
                KFATAL("Failed to submit work to the renderer for frame rendering.");
                engine_state->is_running = false;
                break;
            }

            // Present the frame.
            if (!renderer_frame_present(engine_state->systems.renderer_system, w, &engine_state->p_frame_data)) {
                KERROR("The call to renderer_present failed. This is likely unrecoverable. Shutting down.");
                engine_state->is_running = false;
                break;
            }

            // Figure out how long the frame took and, if below
            f64 frame_end_time = platform_get_absolute_time();
            frame_elapsed_time = frame_end_time - frame_start_time;
            // running_time += frame_elapsed_time;
            f64 remaining_seconds = target_frame_seconds - frame_elapsed_time;

            if (remaining_seconds > 0) {
                u64 remaining_ms = (remaining_seconds * 1000);

                // If there is time left, give it back to the OS.
                b8 limit_frames = false;
                if (remaining_ms > 0 && limit_frames) {
                    platform_sleep(remaining_ms - 1);
                }

                // TODO: frame rate lock
                // frame_count++;
            }

            // NOTE: Input update/state copying should always be handled
            // after any input should be recorded; I.E. before this line.
            // As a safety, input is the last thing to be updated before
            // this frame ends.
            input_update(&engine_state->p_frame_data);

            // Update last time
            engine_state->last_time = current_time;
        } else {
            KDEBUG("suspended...");
        }
    }

    engine_state->is_running = false;
    game_inst->stage = APPLICATION_STAGE_SHUTTING_DOWN;

    // Shut down the game.
    engine_state->game_inst->shutdown(engine_state->game_inst);

    // Unregister from events.
    event_unregister(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);

    // TODO: Close/destroy any and all active windows.
    u32 window_count = darray_length(engine_state->windows);
    for (u32 i = 0; i < window_count; ++i) {
        kwindow* window = &engine_state->windows[i];

        // Tell the renderer about the window destruction.
        renderer_on_window_destroyed(engine_state->systems.renderer_system, window);

        platform_window_destroy(window);
    }

    // Shut down all systems.
    {
        // Engine systems
        engine_system_states* systems = &engine_state->systems;

        camera_system_shutdown(systems->camera_system);
        light_system_shutdown(systems->light_system);
        geometry_system_shutdown(systems->geometry_system);
        material_system_shutdown(systems->material_system);
        font_system_shutdown(systems->font_system);
        texture_system_shutdown(systems->texture_system);
        timeline_system_shutdown(systems->timeline_system);
        xform_system_shutdown(systems->xform_system);
        audio_system_shutdown(systems->audio_system);
        plugin_system_shutdown(systems->plugin_system);
        shader_system_shutdown(systems->shader_system);
        renderer_system_shutdown(systems->renderer_system);
        job_system_shutdown(systems->job_system);
        resource_system_shutdown(systems->resource_system);
        input_system_shutdown(systems->input_system);
        event_system_shutdown(systems->event_system);
        kvar_system_shutdown(systems->kvar_system);
        console_shutdown(systems->console_system);
        platform_system_shutdown(systems->platform_system);
        memory_system_shutdown();
    }

    game_inst->stage = APPLICATION_STAGE_UNINITIALIZED;

    return true;
}

void engine_on_event_system_initialized(void) {
    // Register for engine-level events.
    event_register(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);
}

const struct frame_data* engine_frame_data_get(void) {
    return &engine_state->p_frame_data;
}

const engine_system_states* engine_systems_get(void) {
    return &engine_state->systems;
}

k_handle engine_external_system_register(u64 system_state_memory_requirement) {
    // Don't pass a block of memory here since the system should call "get state" next for it.
    // This keeps memory ownership inside the engine and its registry.
    return kregistry_add_entry(&engine_state->external_systems_registry, 0, system_state_memory_requirement, true);
}

void* engine_external_system_state_get(k_handle system_handle) {
    // Acquire the system state, but without any listener/callback.
    return kregistry_entry_acquire(&engine_state->external_systems_registry, system_handle, 0, 0);
}

struct kwindow* engine_active_window_get(void) {
    // FIXME: multi-window support
    return &engine_state->windows[0];
}

static b8 engine_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    switch (code) {
    case EVENT_CODE_APPLICATION_QUIT: {
        KINFO("EVENT_CODE_APPLICATION_QUIT recieved, shutting down.\n");
        engine_state->is_running = false;
        return true;
    }
    }

    return false;
}

static void engine_on_filewatcher_file_deleted(u32 watcher_id) {
    event_context context = {0};
    context.data.u32[0] = watcher_id;
    event_fire(EVENT_CODE_WATCHED_FILE_DELETED, 0, context);
}

static void engine_on_filewatcher_file_written(u32 watcher_id) {
    event_context context = {0};
    context.data.u32[0] = watcher_id;
    event_fire(EVENT_CODE_WATCHED_FILE_WRITTEN, 0, context);
}

static void engine_on_window_closed(const struct kwindow* window) {
    if (window) {
        // TODO: handle window closes independently.
        event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
    }
}

static void engine_on_window_resized(const struct kwindow* window) {
    // Handle minimization
    if (window->width == 0 || window->height == 0) {
        KINFO("Window minimized, suspending application.");
        // FIXME: This should be per-window, not global.
        engine_state->is_suspended = true;
    } else {
        if (engine_state->is_suspended) {
            KINFO("Window restored, resuming application.");
            engine_state->is_suspended = false;
        }

        // Fire an event for anything listening for window resizes.
        event_context context = {0};
        context.data.u16[0] = window->width;
        context.data.u16[1] = window->height;
        event_fire(EVENT_CODE_WINDOW_RESIZED, (kwindow*)window, context);
    }
}

static void engine_on_process_key(keys key, b8 pressed) {
    input_process_key(key, pressed);
}

static void engine_on_process_mouse_button(mouse_buttons button, b8 pressed) {
    input_process_button(button, pressed);
}

static void engine_on_process_mouse_move(i16 x, i16 y) {
    input_process_mouse_move(x, y);
}

static void engine_on_process_mouse_wheel(i8 z_delta) {
    input_process_mouse_wheel(z_delta);
}

static b8 engine_log_file_write(void* engine_state, log_level level, const char* message) {
    engine_state_t* engine = engine_state;
    // Append to log file
    if (engine && engine->log_file_handle.is_valid) {
        // Since the message already contains a '\n', just write the bytes directly.
        u64 length = string_length(message);
        u64 written = 0;
        if (!filesystem_write(&engine->log_file_handle, length, message, &written)) {
            platform_console_write(0, LOG_LEVEL_ERROR, "ERROR writing to console.log.");
            return false;
        }
        return true;
    }
    return false;
}

static b8 engine_platform_console_write(void* platform, log_level level, const char* message) {
    // Just pass it on to the platform layer.
    platform_console_write(platform, level, message);
    return true;
}
