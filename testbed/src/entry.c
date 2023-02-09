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

    // assign function pointers
    app->boot = app->game_library.functions[0].pfn;
    app->initialize = app->game_library.functions[1].pfn;
    app->update = app->game_library.functions[2].pfn;
    app->render = app->game_library.functions[3].pfn;
    app->on_resize = app->game_library.functions[4].pfn;
    app->shutdown = app->game_library.functions[5].pfn;

    return true;
}

b8 watched_file_updated(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_WATCHED_FILE_WRITTEN) {
        application* app = (application*)listener_inst;
        if (context.data.u32[0] == app->game_library.watch_id) {
            KINFO("Hot-Reloading game dll.");
        }

        if (!platform_dynamic_library_unload(&app->game_library)) {
            KERROR("Failed to unload game library");
            return false;
        }
        if (!platform_copy_file("testbed_lib.dll", "testbed_lib_loaded.dll", true)) {
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
    // if (!platform_dynamic_library_load("testbed_lib_loaded", &out_application->game_library)) {
    //     return false;
    // }

    if (!platform_copy_file("testbed_lib.dll", "testbed_lib_loaded.dll", true)) {
        KERROR("File copy failed!");
        return false;
    }
    if (!load_game_lib(out_application)) {
        KERROR("Initial game lib load failed!");
    }

    PFN_application_state_size get_state_size = 0;
    if (!platform_dynamic_library_load_function("application_state_size", &out_application->game_library)) {
        return false;
    }
    get_state_size = out_application->game_library.functions[6].pfn;

    // if (!platform_dynamic_library_load_function("application_boot", &out_application->game_library)) {
    //     return false;
    // }
    // if (!platform_dynamic_library_load_function("application_initialize", &out_application->game_library)) {
    //     return false;
    // }
    // if (!platform_dynamic_library_load_function("application_update", &out_application->game_library)) {
    //     return false;
    // }
    // if (!platform_dynamic_library_load_function("application_render", &out_application->game_library)) {
    //     return false;
    // }
    // if (!platform_dynamic_library_load_function("application_on_resize", &out_application->game_library)) {
    //     return false;
    // }
    // if (!platform_dynamic_library_load_function("application_shutdown", &out_application->game_library)) {
    //     return false;
    // }

    // // assign function pointers
    // out_application->boot = out_application->game_library.functions[1].pfn;
    // out_application->initialize = out_application->game_library.functions[2].pfn;
    // out_application->update = out_application->game_library.functions[3].pfn;
    // out_application->render = out_application->game_library.functions[4].pfn;
    // out_application->on_resize = out_application->game_library.functions[5].pfn;
    // out_application->shutdown = out_application->game_library.functions[6].pfn;

    // Create the game state.
    out_application->state_memory_requirement = get_state_size();
    out_application->state = 0;

    out_application->engine_state = 0;

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

    const char* extension = platform_dynamic_library_extension();
    char path[260];
    kzero_memory(path, sizeof(char) * 260);
    string_format(path, "%s%s", "testbed_lib", extension);

    if (!platform_watch_file(path, &app->game_library.watch_id)) {
        return false;
    }

    return true;
}
