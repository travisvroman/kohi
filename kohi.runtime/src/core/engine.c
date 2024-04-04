#include "engine.h"

// Version reporting
#include "kohi.runtime_version.h"

#include "application_types.h"
#include "console.h"
#include "containers/darray.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kvar.h"
#include "core/metrics.h"
#include "frame_data.h"
#include "kclock.h"
#include "khandle.h"
#include "kmemory.h"
#include "kstring.h"
#include "logger.h"
#include "memory/linear_allocator.h"
#include "platform/platform.h"
#include "renderer/renderer_frontend.h"
#include "uuid.h"

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

typedef struct engine_state_t {
    application* game_inst;
    b8 is_running;
    b8 is_suspended;
    i16 width;
    i16 height;
    kclock clock;
    f64 last_time;

    // Indicates if the window is currently being resized.
    b8 resizing;
    // The current number of frames since the last resize operation.
    // Only set if resizing = true. Otherwise 0.
    u8 frames_since_resize;

    // An allocator used for per-frame allocations, that is reset every frame.
    linear_allocator frame_allocator;

    frame_data p_frame_data;

    u64 platform_memory_requirement;
    struct platform_state* platform;

    u64 console_memory_requirement;
    struct console_state* console;
    u8 platform_consumer_id;

    // Log file.
    file_handle log_file_handle;
    u8 logfile_consumer_id;

    u64 kvar_system_memory_requirement;
    struct kvar_state* kvar;

    u64 event_system_memory_requirement;
    struct event_state* event;

    u64 input_system_memory_requirement;
    struct input_state* input;

    u64 timeline_system_memory_requirement;
    struct timeline_state* timeline;

    u64 resource_system_memory_requirement;
    struct resource_state* resource;

    u64 shader_system_memory_requirement;
    struct shader_system_state* shader;

    u64 renderer_system_memory_requirement;
    struct renderer_system_state* renderer;

    u64 job_system_memory_requirement;
    struct job_system_state* job;

    u64 audio_system_memory_requirement;
    struct audio_system_state* audio;

    u64 xform_system_memory_requirement;
    struct xform_system_state* xform_system;

    u64 texture_system_memory_requirement;
    struct texture_system_state* texture_system;

    u64 font_system_memory_requirement;
    struct font_system_state* font_system;

    u64 material_system_memory_requirement;
    struct material_system_state* material_system;

    u64 geometry_system_memory_requirement;
    struct geometry_system_state* geometry_system;

    u64 light_system_memory_requirement;
    struct light_system_state* light_system;

    u64 camera_system_memory_requirement;
    struct camera_system_state* camera_system;
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
static b8 engine_on_resized(u16 code, void* sender, void* listener_inst, event_context context);
static void engine_on_filewatcher_file_deleted(u32 watcher_id);
static void engine_on_filewatcher_file_written(u32 watcher_id);
static void engine_on_window_closed(const struct k_window* window);
static void engine_on_window_resized(const struct k_window* window, u16 width, u16 height);
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
    engine_state->resizing = false;
    engine_state->frames_since_resize = 0;

    game_inst->app_config.renderer_plugin = game_inst->render_plugin;
    game_inst->app_config.audio_plugin = game_inst->audio_plugin;

    // TODO: Reworking the systems manager to be single-phase. This means _all_ systems should be initialized
    // and ready before game boot. Any systems requiring game config for boot (i.e. renderer) should probably
    // be refactored or have a separate "post-boot" interface entry point.

    // Thinking this order should be something of the following:
    // Platform initialization first (NOTE: NOT window creation - that should happen much later).
    {
        platform_system_config plat_config = {0};
        plat_config.application_name = game_inst->app_config.name;
        engine_state->platform_memory_requirement = 0;
        platform_system_startup(&engine_state->platform_memory_requirement, 0, &plat_config);
        engine_state->platform = kallocate(engine_state->platform_memory_requirement, MEMORY_TAG_ENGINE);
        if (!platform_system_startup(&engine_state->platform_memory_requirement, engine_state->platform, &plat_config)) {
            KERROR("Failed to initialize platform layer.");
            return false;
        }
    }

    // Console system
    {
        console_initialize(&engine_state->console_memory_requirement, 0, 0);
        engine_state->console = kallocate(engine_state->console_memory_requirement, MEMORY_TAG_ENGINE);
        if (!console_initialize(&engine_state->console_memory_requirement, engine_state->console, 0)) {
            KERROR("Failed to initialize console.");
            return false;
        }

        // Platform should then register as a console consumer.
        console_consumer_register(engine_state->platform, engine_platform_console_write, &engine_state->platform_consumer_id);
        // Setup the engine as another console consumer, which now owns the "console.log" file.
        // Create new/wipe existing log file, then open it.
        if (!filesystem_open("console.log", FILE_MODE_WRITE, false, &engine_state->log_file_handle)) {
            KFATAL("Unable to open console.log for writing.");
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

    // KVar system
    {
        kvar_system_initialize(&engine_state->kvar_system_memory_requirement, 0, 0);
        engine_state->kvar = kallocate(engine_state->kvar_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!kvar_system_initialize(&engine_state->kvar_system_memory_requirement, engine_state->kvar, 0)) {
            KERROR("Failed to initialize KVar system.");
            return false;
        }
    }

    // Event system.
    {
        event_system_initialize(&engine_state->event_system_memory_requirement, 0, 0);
        engine_state->event = kallocate(engine_state->event_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!event_system_initialize(&engine_state->event_system_memory_requirement, engine_state->event, 0)) {
            KERROR("Failed to initialize event system.");
            return false;
        }

        // TODO: After event system, register watcher and input callbacks.
        platform_register_watcher_deleted_callback(engine_on_filewatcher_file_deleted);
        platform_register_watcher_written_callback(engine_on_filewatcher_file_written);
        platform_register_window_closed_callback(engine_on_window_closed);
        platform_register_window_resized_callback(engine_on_window_resized);
    }

    // Input system.
    {
        input_system_initialize(&engine_state->input_system_memory_requirement, 0, 0);
        engine_state->input = kallocate(engine_state->input_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!input_system_initialize(&engine_state->input_system_memory_requirement, engine_state->input, 0)) {
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
        resource_sys_config.asset_base_path = "../assets"; // TODO: The application should probably configure this.
        resource_sys_config.max_loader_count = 32;
        resource_system_initialize(&engine_state->resource_system_memory_requirement, 0, &resource_sys_config);
        engine_state->resource = kallocate(engine_state->resource_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!resource_system_initialize(&engine_state->resource_system_memory_requirement, engine_state->resource, &resource_sys_config)) {
            KERROR("Failed to iniitialize resource system.");
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
        shader_system_initialize(&engine_state->shader_system_memory_requirement, 0, &shader_sys_config);
        engine_state->shader = kallocate(engine_state->shader_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!shader_system_initialize(&engine_state->shader_system_memory_requirement, engine_state->shader, &shader_sys_config)) {
            KERROR("Failed to initialize shader system.");
            return false;
        }
    }

    // Renderer system
    {
        renderer_system_config renderer_sys_config = {0};
        renderer_sys_config.application_name = game_inst->app_config.name;
        renderer_sys_config.plugin = game_inst->app_config.renderer_plugin;
        renderer_system_initialize(&engine_state->renderer_system_memory_requirement, 0, &renderer_sys_config);
        engine_state->renderer = kallocate(engine_state->renderer_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!renderer_system_initialize(&engine_state->renderer_system_memory_requirement, engine_state->renderer, &renderer_sys_config)) {
            KERROR("Failed to initialize renderer system.");
            return false;
        }
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
        job_system_initialize(&engine_state->job_system_memory_requirement, 0, &job_sys_config);
        engine_state->job = kallocate(engine_state->job_system_memory_requirement, MEMORY_TAG_ENGINE);

        if (!job_system_initialize(&engine_state->job_system_memory_requirement, engine_state->job, &job_sys_config)) {
            KERROR("Failed to initialize job system.");
            return false;
        }
    }

    // Audio system
    {
        audio_system_config audio_sys_config = {0};
        audio_sys_config.plugin = game_inst->app_config.audio_plugin;
        audio_sys_config.audio_channel_count = 8;
        audio_system_initialize(&engine_state->audio_system_memory_requirement, 0, &audio_sys_config);
        engine_state->audio = kallocate(engine_state->audio_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!audio_system_initialize(&engine_state->audio_system_memory_requirement, engine_state->audio, &audio_sys_config)) {
            KERROR("Failed to initialize audio system.");
            return false;
        }
    }

    // xform
    {
        xform_system_config xform_sys_config = {0};
        xform_sys_config.initial_slot_count = 128;
        xform_system_initialize(&engine_state->xform_system_memory_requirement, 0, &xform_sys_config);
        engine_state->xform_system = kallocate(engine_state->xform_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!xform_system_initialize(&engine_state->xform_system_memory_requirement, engine_state->xform_system, &xform_sys_config)) {
            KERROR("Failed to intialize xform system.");
            return false;
        }
    }

    // Timeline
    {
        timeline_system_config timeline_config = {0};
        timeline_config.dummy = 1;
        timeline_system_initialize(&engine_state->timeline_system_memory_requirement, 0, 0);
        engine_state->timeline = kallocate(engine_state->timeline_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!timeline_system_initialize(&engine_state->timeline_system_memory_requirement, engine_state->timeline, &timeline_config)) {
            KERROR("Failed to initialize timeline system.");
            return false;
        }
    }

    // Texture system
    {
        texture_system_config texture_sys_config;
        texture_sys_config.max_texture_count = 65536;
        texture_system_initialize(&engine_state->texture_system_memory_requirement, 0, &texture_sys_config);
        engine_state->texture_system = kallocate(engine_state->texture_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!texture_system_initialize(&engine_state->texture_system_memory_requirement, engine_state->texture_system, &texture_sys_config)) {
            KERROR("Failed to initialize texture system.");
            return false;
        }
    }

    // Font system
    {
        font_system_initialize(&engine_state->font_system_memory_requirement, 0, 0);
        engine_state->font_system = kallocate(engine_state->font_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!font_system_initialize(&engine_state->font_system_memory_requirement, engine_state->font_system, 0)) {
            KERROR("Failed to initialize font system.");
            return false;
        }
    }

    // Material system
    {
        material_system_config material_sys_config = {0};
        material_sys_config.max_material_count = 4096;
        material_system_initialize(&engine_state->material_system_memory_requirement, 0, &material_sys_config);
        engine_state->material_system = kallocate(engine_state->material_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!material_system_initialize(&engine_state->material_system_memory_requirement, engine_state->material_system, &material_sys_config)) {
            KERROR("Failed to initialize material system.");
            return false;
        }
    }

    // Geometry system
    {
        geometry_system_config geometry_sys_config = {0};
        geometry_sys_config.max_geometry_count = 4096;
        geometry_system_initialize(&engine_state->geometry_system_memory_requirement, 0, &geometry_sys_config);
        engine_state->geometry_system = kallocate(engine_state->geometry_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!geometry_system_initialize(&engine_state->geometry_system_memory_requirement, engine_state->geometry_system, &geometry_sys_config)) {
            KERROR("Failed to initialize geometry system.");
            return false;
        }
    }

    // Light system
    {
        light_system_initialize(&engine_state->light_system_memory_requirement, 0, 0);
        engine_state->light_system = kallocate(engine_state->light_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!light_system_initialize(&engine_state->light_system_memory_requirement, engine_state->light_system, 0)) {
            KERROR("Failed to initialize light system.");
            return false;
        }
    }

    // Camera system
    {
        camera_system_config camera_sys_config = {0};
        camera_sys_config.max_camera_count = 61;
        camera_system_initialize(&engine_state->camera_system_memory_requirement, 0, &camera_sys_config);
        engine_state->camera_system = kallocate(engine_state->camera_system_memory_requirement, MEMORY_TAG_ENGINE);
        if (!camera_system_initialize(&engine_state->camera_system_memory_requirement, engine_state->camera_system, &camera_sys_config)) {
            KERROR("Failed to initialize camera system.");
            return false;
        }
    }

    // NOTE: Boot sequence =======================================================================================================
    // Perform the game's boot sequence.
    game_inst->stage = APPLICATION_STAGE_BOOTING;
    if (!game_inst->boot(game_inst)) {
        KFATAL("Game boot sequence failed; aborting application.");
        return false;
    }

    // TODO: Reach into platform and open new window(s) in accordance with app config.
    // Notify renderer of window(s)/setup surface(s), etc.

    // TODO: Handle post-boot items in systems that require app config.
    //
    // TODO: font system
    // TODO: Load fonts as configured in app config. in post-boot
    // &game_inst->app_config.font_config

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

    KINFO("stuff");
    char* mem_usage = get_memory_usage_str();
    KINFO(mem_usage);
    string_free(mem_usage);

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
            //
            // Update timelines. Note that this is not done by the systems manager
            // because we don't want or have timeline data in the frame_data struct any longer.
            timeline_system_update(engine_state->timeline, delta);

            // update metrics
            metrics_update(frame_elapsed_time);

            // Make sure the window is not currently being resized by waiting a designated
            // number of frames after the last resize operation before performing the backend updates.
            if (engine_state->resizing) {
                engine_state->frames_since_resize++;

                // If the required number of frames have passed since the resize, go ahead and perform the actual updates.
                if (engine_state->frames_since_resize >= 30) {
                    renderer_on_resized(engine_state->width, engine_state->height);

                    // NOTE: Don't bother checking the result of this, since this will likely
                    // recreate the swapchain and boot to the next frame anyway.
                    renderer_frame_prepare(&engine_state->p_frame_data);

                    // Notify the application of the resize.
                    engine_state->game_inst->on_resize(engine_state->game_inst, engine_state->width, engine_state->height);

                    engine_state->frames_since_resize = 0;
                    engine_state->resizing = false;
                } else {
                    // Skip rendering the frame and try again next time.
                    // NOTE: Simulate a frame being "drawn" at 60 FPS.
                    platform_sleep(16);
                }

                // Either way, don't process this frame any further while resizing.
                // Try again next frame.
                continue;
            }
            if (!renderer_frame_prepare(&engine_state->p_frame_data)) {
                // This can also happen not just from a resize above, but also if a renderer flag
                // (such as VSync) changed, which may also require resource recreation. To handle this,
                // Notify the application of a resize event, which it can then pass on to its rendergraph(s)
                // as needed.
                engine_state->game_inst->on_resize(engine_state->game_inst, engine_state->width, engine_state->height);
                continue;
            }

            if (!engine_state->game_inst->update(engine_state->game_inst, &engine_state->p_frame_data)) {
                KFATAL("Game update failed, shutting down.");
                engine_state->is_running = false;
                break;
            }

            if (!renderer_begin(&engine_state->p_frame_data)) {
                KFATAL("Failed to begin renderer. Shutting down.");
                engine_state->is_running = false;
                break;
            }

            // Begin "prepare_frame" render event grouping.
            renderer_begin_debug_label("prepare_frame", (vec3){1.0f, 1.0f, 0.0f});

            // TODO: frame prepare for systems that need it.

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

            // End the frame.
            renderer_end(&engine_state->p_frame_data);

            // Present the frame.
            if (!renderer_present(&engine_state->p_frame_data)) {
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
        }
    }

    engine_state->is_running = false;
    game_inst->stage = APPLICATION_STAGE_SHUTTING_DOWN;

    // Shut down the game.
    engine_state->game_inst->shutdown(engine_state->game_inst);

    // Unregister from events.
    event_unregister(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);

    // Shut down all systems.
    {
        camera_system_shutdown(engine_state->camera_system);
        light_system_shutdown(engine_state->light_system);
        geometry_system_shutdown(engine_state->geometry_system);
        material_system_shutdown(engine_state->material_system);
        font_system_shutdown(engine_state->font_system);
        texture_system_shutdown(engine_state->texture_system);
        timeline_system_shutdown(engine_state->timeline);
        xform_system_shutdown(engine_state->xform_system);
        audio_system_shutdown(engine_state->audio);
        job_system_shutdown(engine_state->job);
        renderer_system_shutdown(engine_state->renderer);
        shader_system_shutdown(engine_state->shader);
        resource_system_shutdown(engine_state->resource);
        input_system_shutdown(engine_state->input);
        event_system_shutdown(engine_state->event);
        kvar_system_shutdown(engine_state->kvar);
        console_shutdown(engine_state->console);
        platform_system_shutdown(engine_state->platform);
        memory_system_shutdown();
    }

    game_inst->stage = APPLICATION_STAGE_UNINITIALIZED;

    return true;
}

void engine_on_event_system_initialized(void) {
    // Register for engine-level events.
    event_register(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);
    event_register(EVENT_CODE_RESIZED, 0, engine_on_resized);
}

const struct frame_data* engine_frame_data_get(struct application* game_inst) {
    return &((engine_state_t*)game_inst->engine_state)->p_frame_data;
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

static b8 engine_on_resized(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_RESIZED) {
        // Flag as resizing and store the change, but wait to regenerate.
        engine_state->resizing = true;
        // Also reset the frame count since the last  resize operation.
        engine_state->frames_since_resize = 0;

        u16 width = context.data.u16[0];
        u16 height = context.data.u16[1];

        // Check if different. If so, trigger a resize event.
        if (width != engine_state->width || height != engine_state->height) {
            engine_state->width = width;
            engine_state->height = height;

            KDEBUG("Window resize: %i, %i", width, height);

            // Handle minimization
            if (width == 0 || height == 0) {
                KINFO("Window minimized, suspending application.");
                engine_state->is_suspended = true;
                return true;
            } else {
                if (engine_state->is_suspended) {
                    KINFO("Window restored, resuming application.");
                    engine_state->is_suspended = false;
                }
            }
        }
    }

    // Event purposely not handled to allow other listeners to get this.
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

static void engine_on_window_closed(const struct k_window* window) {
    if (window) {
        // TODO: handle window closes independently.
        event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
    }
}

static void engine_on_window_resized(const struct k_window* window, u16 width, u16 height) {
    event_context context;
    context.data.u16[0] = width;
    context.data.u16[1] = height;
    event_fire(EVENT_CODE_RESIZED, 0, context);
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
