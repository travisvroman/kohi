/**
 * @file resource_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the implementation of the resource system.
 * The resource system is responsible for managing resources and their
 * loaders in the engine.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "resources/resource_types.h"

/** @brief The configuration for the resource system */
typedef struct resource_system_config {
    /** @brief The maximum number of loaders that can be registered with this system. */
    u32 max_loader_count;
    /** @brief The relative base path for assets. */
    char* asset_base_path;
} resource_system_config;

/** @brief An "interface" for a resource loader. All registered loaders use this. */
typedef struct resource_loader {
    /** @brief The loader identifier. */
    u32 id;
    /** @brief The loader resource type. */
    resource_type type;
    /** @brief The loader custom type string, if type is set to custom. */
    const char* custom_type;
    /** @brief A type path which is prepended for the asset type. */
    const char* type_path;
    /**
     * @brief Loads a resource using this loader.
     * @param self A pointer to the loader itself.
     * @param name The name of the resource to be loaded.
     * @param params Parameters to be passed to the loader, or 0.
     * @param out_resource A pointer to hold the loaded resource.
     * @returns True on success; otherwise false.
     */
    b8 (*load)(struct resource_loader* self, const char* name, void* params, resource* out_resource);

    /**
     * @brief Unloads the given resource. Loader is determined by the resource's assigned loader id.
     * @param self A pointer to the loader itself.
     * @param name The name of the resource to be unloaded.
     */
    void (*unload)(struct resource_loader* self, resource* resource);
} resource_loader;

/**
 * @brief Initializes this system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (resource_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 resource_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the resource system.
 *
 * @param state The state block of memory for this system.
 */
void resource_system_shutdown(void* state);

/**
 * @brief Registers the given resource loader with the system.
 *
 * @param loader The loader to be registered.
 * @return True on success; otherwise false.
 */
KAPI b8 resource_system_loader_register(resource_loader loader);

/**
 * @brief Loads a resource of the given name.
 *
 * @param name The name of the resource to load.
 * @param type The type of resource to load.
 * @param params Parameters to be passed to the loader, or 0.
 * @param out_resource A pointer to hold the newly-loaded resource.
 * @return True on success; otherwise false.
 */
KAPI b8 resource_system_load(const char* name, resource_type type, void* params, resource* out_resource);

/**
 * @brief Loads a resource of the given name and of a custom type.
 *
 * @param name The name of the resource to load.
 * @param custom_type The custom resource type.
 * @param params Parameters to be passed to the loader, or 0.
 * @param out_resource A pointer to hold the newly-loaded resource.
 * @return True on success; otherwise false.
 */
KAPI b8 resource_system_load_custom(const char* name, const char* custom_type, void* params, resource* out_resource);

/**
 * @brief Writes a resource of the given name to disk.
 *
 * @param type The type of resource to write.
 * @param r A pointer to the resource to be written.
 * @return True on success; otherwise false.
 */
KAPI b8 resource_system_write(resource_type type, resource* r);

/**
 * @brief Attempts to obtain the base asset path for the given type. Includes trailing slash '/'. Returns 0 if not found.
 * @param type The resource type to search for.
 * @return A string containing the base asset path for the given type; otherwise 0.
 */
KAPI const char* resource_system_base_path_for_type(resource_type type);

/**
 * @brief Unloads the given resource.
 *
 * @param resource A pointer to the resource to be unloaded.
 */
KAPI void resource_system_unload(resource* resource);

/** @brief Returns the base path of the resource system. */
KAPI const char* resource_system_base_path(void);
