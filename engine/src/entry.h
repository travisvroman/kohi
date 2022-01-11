/**
 * @file entry.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the main entry point to the application. 
 * It also contains a reference to an externally defined create_game
 * method, which should create and set a custom game object to the
 * location pointed to by out_game. This would be provided by the
 * consuming application, which is then hooked into the engine itself
 * during the bootstrapping phase.
 * @version 1.0
 * @date 2022-01-10
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "core/application.h"
#include "core/logger.h"
#include "game_types.h"

/** @brief Externally-defined function to create a game, provided by the consumer
 * of this library.
 * @param out_game A pointer which holds the created game object as provided by the consumer.
 * @returns True on successful creation; otherwise false.
 */
extern b8 create_game(game* out_game);

/**
 * @brief The main entry point of the application.
 * @returns 0 on successful execution; nonzero on error.
 */
int main(void) {
    // Request the game instance from the application.
    game game_inst;
    if (!create_game(&game_inst)) {
        KFATAL("Could not create game!");
        return -1;
    }

    // Ensure the function pointers exist.
    if (!game_inst.render || !game_inst.update || !game_inst.initialize || !game_inst.on_resize) {
        KFATAL("The game's function pointers must be assigned!");
        return -2;
    }

    // Initialization.
    if (!application_create(&game_inst)) {
        KFATAL("Application failed to create!.");
        return 1;
    }

    // Begin the game loop.
    if (!application_run()) {
        KINFO("Application did not shutdown gracefully.");
        return 2;
    }

    return 0;
}