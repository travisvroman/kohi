/**
 * @file game_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains types to be consumed by the game library.
 * @version 1.0
 * @date 2022-01-10
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "core/application.h"
#include "memory/linear_allocator.h"

struct render_packet;

/**
 * @brief Represents the basic game state in a game.
 * Called for creation by the application.
 */
typedef struct game {
    /** @brief The application configuration. */
    application_config app_config;

    /**
     * @brief Function pointer to the game's boot sequence. This should 
     * fill out the application config with the game's specific requirements.
     * @param game_inst A pointer to the game instance.
     * @returns True on success; otherwise false.
     */
    b8 (*boot)(struct game* game_inst);

    /** 
     * @brief Function pointer to game's initialize function. 
     * @param game_inst A pointer to the game instance.
     * @returns True on success; otherwise false.
     * */
    b8 (*initialize)(struct game* game_inst);

    /** 
     * @brief Function pointer to game's update function. 
     * @param game_inst A pointer to the game instance.
     * @param delta_time The time in seconds since the last frame.
     * @returns True on success; otherwise false.
     * */
    b8 (*update)(struct game* game_inst, f32 delta_time);

    /** 
     * @brief Function pointer to game's render function. 
     * @param game_inst A pointer to the game instance.
     * @param packet A pointer to the packet to be populated by the game.
     * @param delta_time The time in seconds since the last frame.
     * @returns True on success; otherwise false.
     * */
    b8 (*render)(struct game* game_inst, struct render_packet* packet, f32 delta_time);

    /** 
     * @brief Function pointer to handle resizes, if applicable. 
     * @param game_inst A pointer to the game instance.
     * @param width The width of the window in pixels.
     * @param height The height of the window in pixels.
     * */
    void (*on_resize)(struct game* game_inst, u32 width, u32 height);

    /**
     * @brief Shuts down the game, prompting release of resources.
     * @param game_inst A pointer to the game instance.
     */
    void (*shutdown)(struct game* game_inst);

    /** @brief The required size for the game state. */
    u64 state_memory_requirement;

    /** @brief Game-specific game state. Created and managed by the game. */
    void* state;

    /** @brief A block of memory to hold the application state. Created and managed by the engine. */
    void* application_state;

    /** 
     * @brief An allocator used for allocations needing to be made every frame. Contents are wiped
     * at the beginning of the frame.
     */
    linear_allocator frame_allocator;
} game;
