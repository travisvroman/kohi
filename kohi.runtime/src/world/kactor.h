#pragma once

#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "strings/kname.h"

/**
 * An actor is an in-world representation of something which exists in or can be spawned in
 * the world. It may contain actor-components, which can be used to control how actors are rendered,
 * move about in the world, sound, etc. Each actor-component typically has reference to at least one resource, which
 * is generally what gets rendered (i.e. a static mesh resource), but not always (i.e. a sound effect).
 *
 * When used with a scene, these may be parented to one another via the scene's hierarchy view and
 * xform graph, when attached to a scene node.
 */
typedef struct kactor {
    u64 id;
    kname name;
    k_handle xform;
} kactor;

// staticmesh system
struct kactor_staticmesh_system_state;

typedef struct kactor_staticmesh_system_config {
    u32 max_components;
} kactor_staticmesh_system_config;

typedef enum staticmesh_render_data_flag {
    /** @brief Indicates that the winding order for the given static mesh should be inverted. */
    STATICMESH_RENDER_DATA_FLAG_WINDING_INVERTED_BIT = 0x0001
} staticmesh_render_data_flag;

/**
 * @brief Collection of flags for a static mesh to be rendered.
 * @see staticmesh_render_data_flag
 */
typedef u32 staticmesh_render_data_flag_bits;

typedef struct kactor_comp_staticmesh_render_data {
    const kresource_material_instance* material;
    // TODO: Should there be another way to represent this? (note: used in pick shader for flat-colour rendering)
    u64 unique_id;

    /** @brief Flags for the static mesh to be rendered. */
    staticmesh_render_data_flag_bits flags;

    /** @brief Additional tint to be applied to the static mesh when rendered. */
    vec4 tint;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;

    /** @brief The index of the IBL probe to use. */
    u32 ibl_probe_index;
} kactor_comp_staticmesh_render_data;

KAPI b8 kactor_comp_staticmesh_system_initialize(u64* memory_requirement, void* state, const kactor_staticmesh_system_config* config);
KAPI void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_system_state* state);

KAPI u32 kactor_comp_staticmesh_create(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name, geometry g, kresource_material_instance material);
KAPI u32 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name);
KAPI void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_system_state* state, u32 id);

KAPI b8 kactor_comp_staticmesh_load(u32 actor_id);
KAPI b8 kactor_comp_staticmesh_unload(u32 actor_id);

KAPI geometry* kactor_comp_staticmesh_get_geometry(struct kactor_staticmesh_system_state* state, u32 id);
KAPI kresource_material_instance* kactor_comp_staticmesh_get_material(struct kactor_staticmesh_system_state* state, u32 id);
KAPI b8 kactor_comp_staticmesh_get_geometry_material(struct kactor_staticmesh_system_state* state, u32 id, geometry** out_geometry, kresource_material_instance** out_material);

KAPI b8 kactor_comp_staticmesh_get_render_data(struct kactor_staticmesh_system_state* state, u32 id, struct kactor_comp_staticmesh_render_data* out_render_data);
