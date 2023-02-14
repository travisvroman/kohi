#include <entry.h>

#include <core/event.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <containers/darray.h>
#include <platform/platform.h>

typedef b8 (*PFN_plugin_create)(renderer_plugin* out_plugin);
typedef u64 (*PFN_application_state_size)();

b8 load_game_lib(application* app) {
    // Dynamically load game library
    if (!platform_dynamic_library_load("testbed_lib_loaded", &app->game_library)) {
        return false;
    }

    if (!platform_dynamic_library_load_function("application_boot", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_initialize", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_update", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_render", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_on_resize", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_shutdown", &app->game_library)) {
        return false;
    }

    if (!platform_dynamic_library_load_function("application_lib_on_load", &app->game_library)) {
        return false;
    }

    if (!platform_dynamic_library_load_function("application_lib_on_unload", &app->game_library)) {
        return false;
    }

    // assign function pointers
    app->boot = app->game_library.functions[0].pfn;
    app->initialize = app->game_library.functions[1].pfn;
    app->update = app->game_library.functions[2].pfn;
    app->render = app->game_library.functions[3].pfn;
    app->on_resize = app->game_library.functions[4].pfn;
    app->shutdown = app->game_library.functions[5].pfn;
    app->lib_on_load = app->game_library.functions[6].pfn;
    app->lib_on_unload = app->game_library.functions[7].pfn;

    // Invoke the onload.
    app->lib_on_load(app);

    return true;
}

b8 watched_file_updated(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_WATCHED_FILE_WRITTEN) {
        application* app = (application*)listener_inst;
        if (context.data.u32[0] == app->game_library.watch_id) {
            KINFO("Hot-Reloading game library.");
        }

        // Tell the app it is about to be unloaded.
        app->lib_on_unload(app);

        // Actually unload the app's lib.
        if (!platform_dynamic_library_unload(&app->game_library)) {
            KERROR("Failed to unload game library");
            return false;
        }

        const char* prefix = platform_dynamic_library_prefix();
        const char* extension = platform_dynamic_library_extension();
        char source_file[260];
        char target_file[260];
        string_format(source_file, "%stestbed_lib%s", prefix, extension);
        string_format(target_file, "%stestbed_lib_loaded%s", prefix, extension);

        platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
        while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
            err_code = platform_copy_file(source_file, target_file, true);
            if (err_code == PLATFORM_ERROR_FILE_LOCKED) {
                platform_sleep(100);
            }
        }
        if (err_code != PLATFORM_ERROR_SUCCESS) {
            KERROR("File copy failed!");
            return false;
        }

        if (!load_game_lib(app)) {
            KERROR("Game lib reload failed.");
            return false;
        }
    }
    return false;
}

// Define the function to create a game
b8 create_application(application* out_application) {
    // Application configuration.
    out_application->app_config.start_pos_x = 100;
    out_application->app_config.start_pos_y = 100;
    out_application->app_config.start_width = 1280;
    out_application->app_config.start_height = 720;
    out_application->app_config.name = "Kohi Engine Testbed";

    // Dynamically load game library
    // if (!platform_dynamic_library_load("libtestbed_lib_loaded", &out_application->game_library)) {
    //     return false;
    // }

    platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
    while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
        const char* prefix = platform_dynamic_library_prefix();
        const char* extension = platform_dynamic_library_extension();
        char source_file[260];
        char target_file[260];
        string_format(source_file, "%stestbed_lib%s", prefix, extension);
        string_format(target_file, "%stestbed_lib_loaded%s", prefix, extension);

        err_code = platform_copy_file(source_file, target_file, true);
        if (err_code == PLATFORM_ERROR_FILE_LOCKED) {
            platform_sleep(100);
        }
    }
    if (err_code != PLATFORM_ERROR_SUCCESS) {
        KERROR("File copy failed!");
        return false;
    }

    if (!load_game_lib(out_application)) {
        KERROR("Initial game lib load failed!");
    }

    out_application->engine_state = 0;
    out_application->state = 0;

    if (!platform_dynamic_library_load("vulkan_renderer", &out_application->renderer_library)) {
        return false;
    }

    if (!platform_dynamic_library_load_function("plugin_create", &out_application->renderer_library)) {
        return false;
    }

    // Create the renderer plugin.
    PFN_plugin_create plugin_create = out_application->renderer_library.functions[0].pfn;
    if (!plugin_create(&out_application->render_plugin)) {
        return false;
    }

    return true;
}

b8 initialize_application(application* app) {
    if (!event_register(EVENT_CODE_WATCHED_FILE_WRITTEN, app, watched_file_updated)) {
        return false;
    }

    const char* prefix = platform_dynamic_library_prefix();
    const char* extension = platform_dynamic_library_extension();
    char path[260];
    kzero_memory(path, sizeof(char) * 260);
    string_format(path, "%s%s%s", prefix, "testbed_lib", extension);

    if (!platform_watch_file(path, &app->game_library.watch_id)) {
        KERROR("Failed to watch the testbed library!");
        return false;
    }

    return true;
}
