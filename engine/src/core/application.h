/**
 * @file
 * @brief This file contains structures and logic pertaining to the
 * overall engine application itself.
 * 
 * The application is responsible for managing both the platform layers
 * as well as all systems within the engine.
 */

#pragma once

#include "defines.h"

struct game;

/** 
 * @brief Represents configuration for the application.
 */
typedef struct application_config {
    /** @brief Window starting position x axis, if applicable. */
    i16 start_pos_x;

    /** @brief Window starting position y axis, if applicable. */
    i16 start_pos_y;

    /** @brief Window starting width, if applicable. */
    i16 start_width;

    /** @brief Window starting height, if applicable. */
    i16 start_height;

    /** @brief The application name used in windowing, if applicable. */
    char* name;
} application_config;

/**
 * @brief Creates the application, standing up the platform layer and all
 * underlying subsystems.
 * @param game_inst A pointer to the game instance associated with the application
 * @returns True on success; otherwise false.
 */
KAPI b8 application_create(struct game* game_inst);

/**
 * @brief Starts the main application loop.
 * @returns True on success; otherwise false.
 */
KAPI b8 application_run();

/**
 * @brief Obtains the framebuffer size of the application.
 * @deprecated NOTE: This is temporary, and should be removed once kvars are in place.
 */
void application_get_framebuffer_size(u32* width, u32* height);
