/**
 * @file engine.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains structures and logic pertaining to the
 * overall engine itself.
 * The engine is responsible for managing both the platform layers
 * as well as all systems within the engine.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "audio/audio_types.h"
#include "defines.h"
#include "renderer/renderer_types.h"
#include "systems/font_system.h"

struct application;
struct frame_data;

/**
 * @brief Represents configuration for the application. The application config
 * is fed to the engine on creation, so it knows how to configure itself internally.
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

    /** @brief Configuration for the font system. */
    font_system_config font_config;

    /** @brief A darray of render views. */
    render_view* views;

    renderer_plugin renderer_plugin;
    audio_plugin audio_plugin;

    /** @brief The size of the frame allocator. */
    u64 frame_allocator_size;

    /** @brief The size of the application-specific frame data. Set to 0 if not used. */
    u64 app_frame_data_size;
} application_config;

/**
 * @brief Creates the engine, standing up the platform layer and all
 * underlying subsystems.
 * @param game_inst A pointer to the application instance associated with the engine
 * @returns True on success; otherwise false.
 */
KAPI b8 engine_create(struct application* game_inst);

/**
 * @brief Starts the main engine loop.
 * @param game_inst A pointer to the application instance associated with the engine
 * @returns True on success; otherwise false.
 */
KAPI b8 engine_run(struct application* game_inst);

/**
 * @brief A callback made when the event system is initialized,
 * which internally allows the engine to begin listening for events
 * required for initialization.
 */
void engine_on_event_system_initialized(void);

/**
 * @brief Obtains a constant pointer to the current frame data.
 *
 * @param game_inst A pointer to the application instance.
 * @return A constant pointer to the current frame data.
 */
KAPI const struct frame_data* engine_frame_data_get(struct application* game_inst);
