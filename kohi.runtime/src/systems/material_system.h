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
 * @param name The name of the material to find.
 * @return A pointer to the loaded material if found; otherwise 0/null.
 */
KAPI kresource_material* material_system_acquire(struct material_system_state* state, kname name);

/**
 * @brief Releases the given material.
 * Decreases the reference counter by 1. If the reference counter reaches 0
 * the material is unloaded, releasing internal resources.
 *
 * @param material A pointer to the material to unload.
 */
KAPI void material_system_release(struct material_system_state* state, kresource_material* material);

/**
 * @brief Gets a constant pointer to the default unlit material. Does not reference count.
 */
KAPI const kresource_material* material_system_get_default_unlit(struct material_system_state* state);

/**
 * @brief Gets a constant pointer to the default phong material. Does not reference count.
 */
KAPI const kresource_material* material_system_get_default_phong(struct material_system_state* state);

/**
 * @brief Gets a constant pointer to the default PBR material. Does not reference count.
 */
KAPI const kresource_material* material_system_get_default_pbr(struct material_system_state* state);

/**
 * @brief Gets a constant pointer to the default terrain material. Does not reference count.
 */
KAPI const kresource_material* material_system_get_default_terrain_pbr(struct material_system_state* state);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 */
KAPI void material_system_dump(struct material_system_state* state);
