#include <containers/darray.h>
#include <core/event.h>
#include <entry.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <strings/kstring.h>

typedef u64 (*PFN_application_state_size)(void);

b8 load_game_lib(application* app) {
    // Dynamically load game library
    if (!platform_dynamic_library_load("testbed.klib_loaded", &app->game_library)) {
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
    if (!platform_dynamic_library_load_function("application_prepare_frame", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_render_frame", &app->game_library)) {
        return false;
    }
    if (!platform_dynamic_library_load_function("application_on_window_resize", &app->game_library)) {
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
    app->prepare_frame = app->game_library.functions[3].pfn;
    app->render_frame = app->game_library.functions[4].pfn;
    app->on_window_resize = app->game_library.functions[5].pfn;
    app->shutdown = app->game_library.functions[6].pfn;
    app->lib_on_load = app->game_library.functions[7].pfn;
    app->lib_on_unload = app->game_library.functions[8].pfn;

    // Invoke the onload.
    app->lib_on_load(app);

    return true;
}

static void file_deleted(u32 watcher_id, void* context) {
    KFATAL("Testbed: Game code library file deleted.");
}

static void file_written(u32 watcher_id, const char* file_path, b8 is_binary, void* context) {
    KFATAL("Testbed: Game code library file updated, hot-reloading.");

    application* app = (application*)context;
    if (watcher_id == app->game_library.watch_id) {
        KINFO("Hot-Reloading game library.");

        // Tell the app it is about to be unloaded.
        app->lib_on_unload(app);

        // Actually unload the app's lib.
        if (!platform_dynamic_library_unload(&app->game_library)) {
            KFATAL("Failed to unload game library");
            return;
        }

        // Wait a bit before trying to copy the file.
        platform_sleep(100);

        const char* prefix = platform_dynamic_library_prefix();
        const char* extension = platform_dynamic_library_extension();
        char source_file[260];
        char target_file[260];
        string_format_unsafe(source_file, "%stestbed.klib%s", prefix, extension);
        string_format_unsafe(target_file, "%stestbed.klib_loaded%s", prefix, extension);

        platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
        while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
            err_code = platform_copy_file(source_file, target_file, true);
            if (err_code == PLATFORM_ERROR_FILE_LOCKED) {
                platform_sleep(100);
            }
        }
        if (err_code != PLATFORM_ERROR_SUCCESS) {
            KFATAL("File copy failed!");
            return;
        }

        if (!load_game_lib(app)) {
            KFATAL("Game lib reload failed.");
            return;
        }
    }
}

// Define the function to create a game
b8 create_application(application* out_application) {
    // Application configuration.
    platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
    while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
        const char* prefix = platform_dynamic_library_prefix();
        const char* extension = platform_dynamic_library_extension();
        char* source_file = string_format("%stestbed.klib%s", prefix, extension);
        char* target_file = string_format("%stestbed.klib_loaded%s", prefix, extension);
        err_code = platform_copy_file(source_file, target_file, true);
        string_free(source_file);
        string_free(target_file);
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
        return false;
    }

    out_application->engine_state = 0;
    out_application->state = 0;

    return true;
}

const char* application_config_path_get(void) {
    return "../testbed.kapp/app_config.kson";
}

b8 initialize_application(application* app) {
    const char* prefix = platform_dynamic_library_prefix();
    const char* extension = platform_dynamic_library_extension();

    b8 success = false;
    char* path = string_format("%s%s%s", prefix, "testbed.klib", extension);
    if (!platform_watch_file(path, true, file_written, app, file_deleted, app, &app->game_library.watch_id)) {
        KERROR("Failed to watch the testbed library!");
        goto initialize_application_cleanup;
    }

    success = true;
initialize_application_cleanup:
    if (path) {
        string_free(path);
    }
    return success;
}
