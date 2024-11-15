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
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

#include "audio/audio_types.h"
#include "identifiers/khandle.h"
#include "platform/vfs.h"

struct application;
struct frame_data;
struct platform_state;
struct console_state;
struct kvar_state;
struct event_state;
struct input_state;
struct timeline_state;
struct resource_state;
struct shader_system_state;
struct renderer_system_state;
struct job_system_state;
struct audio_system_state;
struct xform_system_state;
struct texture_system_state;
struct font_system_state;
struct material_system_state;
struct static_mesh_system_state;
struct light_system_state;
struct camera_system_state;
struct plugin_system_state;
struct rendergraph_system_state;
struct asset_system_state;
struct vfs_state;
struct kwindow;

typedef struct engine_system_states {
    u64 platform_memory_requirement;
    struct platform_state* platform_system;

    u64 console_memory_requirement;
    struct console_state* console_system;

    u64 kvar_system_memory_requirement;
    struct kvar_state* kvar_system;

    u64 event_system_memory_requirement;
    struct event_state* event_system;

    u64 input_system_memory_requirement;
    struct input_state* input_system;

    u64 timeline_system_memory_requirement;
    struct timeline_system_state* timeline_system;

    u64 resource_system_memory_requirement;
    struct resource_state* resource_system;

    u64 shader_system_memory_requirement;
    struct shader_system_state* shader_system;

    u64 renderer_system_memory_requirement;
    struct renderer_system_state* renderer_system;

    u64 job_system_memory_requirement;
    struct job_system_state* job_system;

    u64 audio_system_memory_requirement;
    struct audio_system_state* audio_system;

    u64 xform_system_memory_requirement;
    struct xform_system_state* xform_system;

    u64 texture_system_memory_requirement;
    struct texture_system_state* texture_system;

    u64 font_system_memory_requirement;
    struct font_system_state* font_system;

    u64 material_system_memory_requirement;
    struct material_system_state* material_system;

    u64 static_mesh_system_memory_requirement;
    struct static_mesh_system_state* static_mesh_system;

    u64 light_system_memory_requirement;
    struct light_system_state* light_system;

    u64 camera_system_memory_requirement;
    struct camera_system_state* camera_system;

    u64 plugin_system_memory_requirement;
    struct plugin_system_state* plugin_system;

    u64 rendergraph_system_memory_requirement;
    struct rendergraph_system_state* rendergraph_system;

    u64 vfs_system_memory_requirement;
    struct vfs_state* vfs_system_state;

    u64 asset_system_memory_requirement;
    struct asset_system_state* asset_state;

    u64 kresource_system_memory_requirement;
    struct kresource_system_state* kresource_state;
} engine_system_states;

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
 * @return A constant pointer to the current frame data.
 */
KAPI const struct frame_data* engine_frame_data_get(void);

/**
 * @brief Obtains a constant pointer to the collection of system states from the engine.
 */
KAPI const engine_system_states* engine_systems_get(void);

KAPI khandle engine_external_system_register(u64 system_state_memory_requirement);

KAPI void* engine_external_system_state_get(khandle system_handle);

KAPI struct kwindow* engine_active_window_get(void);
