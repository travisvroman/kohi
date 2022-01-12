/**
 * @file geometry_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The geometry system is responsible for loading and unloading geometry,
 * as well as reference counting.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer/renderer_types.inl"

/** @brief The geometry system configuration. */
typedef struct geometry_system_config {
    /**
     * @brief NOTE: Should be significantly greater than the number of static meshes because
     * the there can and will be more than one of these per mesh.
     * Take other systems into account as well.
     */
    u32 max_geometry_count;

} geometry_system_config;

/**
 * @brief Represents the configuration for a geometry.
 */
typedef struct geometry_config {
    /** @brief The size of each vertex. */
    u32 vertex_size;
    /** @brief The number of vertices. */
    u32 vertex_count;
    /** @brief An array of vertices. */
    void* vertices;
    /** @brief The size of each index. */
    u32 index_size;
    /** @brief The number of indices. */
    u32 index_count;
    /** @brief An array of indices. */
    void* indices;
    /** @brief The name of the geometry. */
    char name[GEOMETRY_NAME_MAX_LENGTH];
    /** @brief The name of the material used by the geometry. */
    char material_name[MATERIAL_NAME_MAX_LENGTH];
} geometry_config;

/** @brief The name of the default geometry. */
#define DEFAULT_GEOMETRY_NAME "default"

/**
 * @brief Initializes the geometry system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 * 
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration for this system.
 * @return True on success; otherwise false.
 */
b8 geometry_system_initialize(u64* memory_requirement, void* state, geometry_system_config config);

/**
 * @brief Shuts down the geometry system.
 * 
 * @param state The state block of memory.
 */
void geometry_system_shutdown(void* state);

/**
 * @brief Acquires an existing geometry by id.
 * 
 * @param id The geometry identifier to acquire by.
 * @return A pointer to the acquired geometry or nullptr if failed.
 */
geometry* geometry_system_acquire_by_id(u32 id);

/**
 * @brief Registers and acquires a new geometry using the given config.
 * 
 * @param config The geometry configuration.
 * @param auto_release Indicates if the acquired geometry should be unloaded when its reference count reaches 0.
 * @return A pointer to the acquired geometry or nullptr if failed. 
 */
geometry* geometry_system_acquire_from_config(geometry_config config, b8 auto_release);

/**
 * @brief Releases a reference to the provided geometry.
 * 
 * @param geometry The geometry to be released.
 */
void geometry_system_release(geometry* geometry);

/**
 * @brief Obtains a pointer to the default geometry.
 * 
 * @return A pointer to the default geometry. 
 */
geometry* geometry_system_get_default();

/**
 * @brief Obtains a pointer to the default geometry.
 * 
 * @return A pointer to the default geometry. 
 */
geometry* geometry_system_get_default_2d();

/**
 * @brief Generates configuration for plane geometries given the provided parameters.
 * NOTE: vertex and index arrays are dynamically allocated and should be freed upon object disposal.
 * Thus, this should not be considered production code.
 * 
 * @param width The overall width of the plane. Must be non-zero.
 * @param height The overall height of the plane. Must be non-zero.
 * @param x_segment_count The number of segments along the x-axis in the plane. Must be non-zero.
 * @param y_segment_count The number of segments along the y-axis in the plane. Must be non-zero.
 * @param tile_x The number of times the texture should tile across the plane on the x-axis. Must be non-zero.
 * @param tile_y The number of times the texture should tile across the plane on the y-axis. Must be non-zero.
 * @param name The name of the generated geometry.
 * @param material_name The name of the material to be used.
 * @return A geometry configuration which can then be fed into geometry_system_acquire_from_config().
 */
geometry_config geometry_system_generate_plane_config(f32 width, f32 height, u32 x_segment_count, u32 y_segment_count, f32 tile_x, f32 tile_y, const char* name, const char* material_name);
