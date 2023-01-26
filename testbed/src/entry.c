#include "game.h"

#include <entry.h>

#include <core/kmemory.h>

#include "vulkan_renderer_plugin_main.h"

// Define the function to create a game
b8 create_application(application* out_game) {
    // Application configuration.
    out_game->app_config.start_pos_x = 100;
    out_game->app_config.start_pos_y = 100;
    out_game->app_config.start_width = 1280;
    out_game->app_config.start_height = 720;
    out_game->app_config.name = "Kohi Engine Testbed";
    out_game->boot = game_boot;
    out_game->initialize = game_initialize;
    out_game->update = game_update;
    out_game->render = game_render;
    out_game->on_resize = game_on_resize;
    out_game->shutdown = game_shutdown;

    // Create the game state.
    out_game->state_memory_requirement = sizeof(game_state);
    out_game->state = 0;

    out_game->engine_state = 0;

    if (!vulkan_renderer_plugin_create(&out_game->render_plugin)) {
        return false;
    }

    return true;
}