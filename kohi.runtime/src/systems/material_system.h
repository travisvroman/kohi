/**
 * @file material_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The material system is responsible for managing materials in the
 * engine, including reference counting and auto-unloading.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"
#include "kresources/kresource_types.h"

struct material_system_state;

/** @brief The configuration for the material system. */
typedef struct material_system_config {
    /** @brief The maximum number of loaded materials. */
    u32 max_material_count;
} material_system_config;

typedef struct material_instance {
    const kresource_material* material;

    u32 per_draw_id;
} material_instance;

/**
 * @brief Initializes the material system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (material_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 material_system_initialize(u64* memory_requirement, struct material_system_state* state, const material_system_config* config);

/**
 * @brief Shuts down the material system.
 *
 * @param state The state block of memory.
 */
void material_system_shutdown(struct material_system_state* state);

/**
 * @brief Attempts to acquire a material with the given name. If it has not yet been loaded,
 * this triggers it to load. If the material is not found, a pointer to the default material
 * is returned. If the material _is_ found and loaded, its reference counter is incremented.
 *
 * @param state A pointer to the material system state.
 * @param name The name of the material to find.
 * @param out_instance A pointer to hold the loaded material instance if successful.
 * @return True if material was found, otherwise false.
 */
KAPI b8 material_system_acquire(struct material_system_state* state, kname name, material_instance* out_instance);

/**
 * @brief Releases the given material instance.
 * Decreases the reference counter by 1. If the reference counter reaches 0
 * the material is unloaded, releasing internal resources.
 *
 * @param instance A pointer to the material instance to unload.
 */
KAPI void material_system_release_instance(struct material_system_state* state, material_instance* instance);

/**
 * @brief Gets an instance of the default unlit material. Does not reference count.
 */
KAPI material_instance material_system_get_default_unlit(struct material_system_state* state);

/**
 * @brief Gets an instance of the default phong material. Does not reference count.
 */
KAPI material_instance material_system_get_default_phong(struct material_system_state* state);

/**
 * @brief Gets an instance of the default PBR material. Does not reference count.
 */
KAPI material_instance material_system_get_default_pbr(struct material_system_state* state);

/**
 * @brief Gets an instance of the default terrain material. Does not reference count.
 */
KAPI material_instance material_system_get_default_terrain_pbr(struct material_system_state* state);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 */
KAPI void material_system_dump(struct material_system_state* state);
