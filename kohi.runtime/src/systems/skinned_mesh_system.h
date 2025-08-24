#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "math/math_types.h"

/**
 * @brief Represents an instance of a skinned mesh. This is to be
 * used in the world. Material instances are obtained when acquiring this
 * skinned mesh instance, and released when releasing this skinned mesh instance.
 */
typedef struct kskinned_mesh_instance {
    /** @brief The underlying mesh. */
    kskinned_mesh mesh;
    /** @brief The identifier of the instance. */
    u16 instance_id;
} kskinned_mesh_instance;

typedef struct skinned_mesh_system_config {
    kname application_package_name;
} skinned_mesh_system_config;

struct skinned_mesh_system_state;

typedef void (*PFN_skinned_mesh_loaded)(kskinned_mesh_instance instance, void* context);

KAPI b8 skinned_mesh_system_initialize(u64* memory_requirement, struct skinned_mesh_system_state* state, skinned_mesh_system_config config);
KAPI void skinned_mesh_system_shutdown(struct skinned_mesh_system_state* state);

KAPI kskinned_mesh_instance skinned_mesh_instance_acquire(struct skinned_mesh_system_state* state, kname asset_name, PFN_skinned_mesh_loaded callback, void* context);
KAPI kskinned_mesh_instance skinned_mesh_instance_acquire_from_package(struct skinned_mesh_system_state* state, kname asset_name, kname package_name, PFN_skinned_mesh_loaded callback, void* context);
// NOTE: Also releases held material instances.
KAPI void skinned_mesh_instance_release(struct skinned_mesh_system_state* state, kskinned_mesh_instance* instance);

KAPI b8 skinned_mesh_is_loaded(struct skinned_mesh_system_state* state, kskinned_mesh m);
KAPI b8 skinned_mesh_tint_get(struct skinned_mesh_system_state* state, kskinned_mesh_instance instance, vec4* out_tint);
KAPI b8 skinned_mesh_tint_set(struct skinned_mesh_system_state* state, kskinned_mesh_instance instance, vec4 tint);
KAPI b8 skinned_mesh_extents_get(struct skinned_mesh_system_state* state, kskinned_mesh m, extents_3d* out_extents);

KAPI b8 skinned_mesh_submesh_count_get(struct skinned_mesh_system_state* state, kskinned_mesh m, u16* out_count);
KAPI const kgeometry* skinned_mesh_submesh_geometry_get_at(struct skinned_mesh_system_state* state, kskinned_mesh m, u16 index);
KAPI const kmaterial_instance* skinned_mesh_submesh_material_instance_get_at(struct skinned_mesh_system_state* state, kskinned_mesh_instance instance, u16 index);

KAPI b8 skinned_mesh_render_data_generate(struct skinned_mesh_system_state* state, kskinned_mesh_instance instance, kskinned_mesh_render_data_flag_bits flags, kskinned_mesh_render_data* out_render_data);
KAPI void skinned_mesh_render_data_destroy(kskinned_mesh_render_data* render_data);
