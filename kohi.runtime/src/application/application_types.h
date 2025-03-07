/**
 * @file application_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains types to be consumed by the application library.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "application_config.h"
#include "core/engine.h"
#include "platform/platform.h"

struct render_packet;
struct frame_data;
struct application_config;

/** @brief Represents the various stages of application lifecycle. */
typedef enum application_stage {
    /** @brief Application is in an uninitialized state. */
    APPLICATION_STAGE_UNINITIALIZED,
    /** @brief Application is currently booting up. */
    APPLICATION_STAGE_BOOTING,
    /** @brief Application completed boot process and is ready to be initialized. */
    APPLICATION_STAGE_BOOT_COMPLETE,
    /** @brief Application is currently initializing. */
    APPLICATION_STAGE_INITIALIZING,
    /** @brief Application initialization is complete. */
    APPLICATION_STAGE_INITIALIZED,
    /** @brief Application is currently running. */
    APPLICATION_STAGE_RUNNING,
    /** @brief Application is in the process of shutting down. */
    APPLICATION_STAGE_SHUTTING_DOWN
} application_stage;

struct application_state;

/**
 * @brief Represents the basic application state in a application.
 * Called for creation by the application.
 */
typedef struct application {
    /** @brief The application configuration. */
    application_config app_config;

    /**
     * @brief Function pointer to the application's boot sequence. This should
     * fill out the application config with the application's specific requirements.
     * @param app_inst A pointer to the application instance.
     * @returns True on success; otherwise false.
     */
    b8 (*boot)(struct application* app_inst);

    /**
     * @brief Function pointer to application's initialize function.
     * @param app_inst A pointer to the application instance.
     * @returns True on success; otherwise false.
     * */
    b8 (*initialize)(struct application* app_inst);

    /**
     * @brief Function pointer to application's update function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     * */
    b8 (*update)(struct application* app_inst, struct frame_data* p_frame_data);

    /**
     * @brief Function pointer to application's prepare_frame function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     */
    b8 (*prepare_frame)(struct application* app_inst, struct frame_data* p_frame_data);

    /**
     * @brief Function pointer to application's render_frame function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     * */
    b8 (*render_frame)(struct application* app_inst, struct frame_data* p_frame_data);

    /**
     * @brief Function pointer to handle resizes, if applicable.
     * @param app_inst A pointer to the application instance.
     * @param window A constant pointer to the window that was resized.
     * @param height The height of the window in pixels.
     * */
    void (*on_window_resize)(struct application* app_inst, const struct kwindow* window);

    /**
     * @brief Shuts down the application, prompting release of resources.
     * @param app_inst A pointer to the application instance.
     */
    void (*shutdown)(struct application* app_inst);

    void (*lib_on_unload)(struct application* game_inst);

    void (*lib_on_load)(struct application* game_inst);

    /** @brief The application stage of execution. */
    application_stage stage;

    /** @brief application-specific state. Created and managed by the application. */
    struct application_state* state;

    /** @brief A block of memory to hold the engine state. Created and managed by the engine. */
    void* engine_state;

    dynamic_library game_library;
} application;
