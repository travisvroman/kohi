#include "game.h"

#include <entry.h>

#include <core/kmemory.h>
#include <core/kstring.h>
#include <containers/darray.h>
#include <platform/platform.h>

typedef b8 (*PFN_plugin_create)(renderer_plugin* out_plugin);

// Define the function to create a game
b8 create_application(application* out_application) {
    // Application configuration.
    out_application->app_config.start_pos_x = 100;
    out_application->app_config.start_pos_y = 100;
    out_application->app_config.start_width = 1280;
    out_application->app_config.start_height = 720;
    out_application->app_config.name = "Kohi Engine Testbed";
    out_application->boot = game_boot;
    out_application->initialize = game_initialize;
    out_application->update = game_update;
    out_application->render = game_render;
    out_application->on_resize = game_on_resize;
    out_application->shutdown = game_shutdown;

    // Create the game state.
    out_application->state_memory_requirement = sizeof(game_state);
    out_application->state = 0;

    out_application->engine_state = 0;

    if (!platform_dynamic_library_load("vulkan_renderer", &out_application->renderer_library)) {
        return false;
    }

    if (!platform_dynamic_library_load_function("plugin_create", &out_application->renderer_library)) {
        return false;
    }

    u32 count = darray_length(out_application->renderer_library.functions);
    for (u32 i = 0; i < count; ++i) {
        dynamic_library_function* f = &out_application->renderer_library.functions[i];
        if (strings_equal("plugin_create", f->name)) {
            if (!((PFN_plugin_create)f->pfn)(&out_application->render_plugin)) {
                return false;
            }
        }
    }

    // if (!vulkan_renderer_plugin_create(&out_game->render_plugin)) {
    //     return false;
    // }

    return true;
}