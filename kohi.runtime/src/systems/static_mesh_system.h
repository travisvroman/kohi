#pragma once

#include "kresources/kresource_types.h"
#include "systems/material_system.h"

/**
 * @brief Represents an instance of a static mesh resource. This is to be
 * used in the world as opposed to the actual resource itself. Material
 * instances are obtained when acquiring this instance, and released when
 * releasing this instance.
 */
typedef struct static_mesh_instance {
    /** @brief A randomly-generated identifier specific to this instance. */
    u64 instance_id;

    /** @brief A pointer to the underlying mesh resource. */
    kresource_static_mesh* mesh_resource;

    /**
     * @brief An array of material instances associated with the submeshes.
     * Elements match up to mesh_resource->submeshes index-wise. Thus the
     * count of this array is the same as mesh_resource->submesh_count.
     */
    material_instance* material_instances;

    vec4 tint;
} static_mesh_instance;

/** @brief Defines flags used for rendering static meshes */
typedef enum static_mesh_render_data_flag {
    /** @brief Indicates that the winding order for the given static mesh should be inverted. */
    STATICM_ESH_RENDER_DATA_FLAG_WINDING_INVERTED_BIT = 0x0001
} staticm_esh_render_data_flag;

/**
 * @brief Collection of flags for a static mesh submesh to be rendered.
 * @see static_mesh_render_data_flag
 */
typedef u32 static_mesh_render_data_flag_bits;

/**
 * @brief The render data for an individual static sub-mesh
 * to be rendered.
 */
typedef struct static_mesh_submesh_render_data {
    /** @brief Flags for the static mesh to be rendered. */
    static_mesh_render_data_flag_bits flags;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;

    /** @brief The instance of the material to use with this static mesh when rendering. */
    material_instance material;

} static_mesh_submesh_render_data;

/**
 * Contains data required to render a static mesh (ultimately its submeshes).
 */
typedef struct static_mesh_render_data {
    /** The identifier of the mesh instance being rendered. */
    u64 instance_id;

    /** @brief The number of submeshes to be rendered. */
    u32 submesh_count;
    /** @brief The array of submeshes to be rendered. */
    static_mesh_submesh_render_data* submeshes;

    /** @brief The tint override to be used when rendering all submeshes. Typically white (1, 1, 1, 1) if not used. */
    vec4 tint;
} static_mesh_render_data;

struct static_mesh_system_state;

KAPI b8 static_mesh_system_initialize(u64* memory_requirement, struct static_mesh_system_state* state);
KAPI void static_mesh_system_shutdown(struct static_mesh_system_state* state);

KAPI b8 static_mesh_system_instance_acquire(struct static_mesh_system_state* state, kname resource_name, kname package_name, static_mesh_instance* out_instance);
KAPI void static_mesh_system_instance_release(struct static_mesh_system_state* state, static_mesh_instance* instance);

KAPI b8 static_mesh_system_render_data_generate(const static_mesh_instance* instance, static_mesh_render_data_flag_bits flags, static_mesh_render_data* out_render_data);
KAPI void static_mesh_system_render_data_destroy(static_mesh_render_data* render_data);
