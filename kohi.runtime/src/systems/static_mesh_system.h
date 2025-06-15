#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "systems/material_system.h"

/**
 * @brief Represents an instance of a static mesh. This is to be
 * used in the world. Material instances are obtained when acquiring this
 * static mesh instance, and released when releasing this static mesh instance.
 */
typedef struct kstatic_mesh_instance {
    /** @brief The underlying mesh. */
    kstatic_mesh mesh;
    /** @brief The identifier of the instance. */
    u16 instance_id;
} kstatic_mesh_instance;

typedef struct static_mesh_system_config {
    kname application_package_name;
} static_mesh_system_config;

struct static_mesh_system_state;

KAPI b8 static_mesh_system_initialize(u64* memory_requirement, struct static_mesh_system_state* state, static_mesh_system_config config);
KAPI void static_mesh_system_shutdown(struct static_mesh_system_state* state);

KAPI kstatic_mesh_instance static_mesh_instance_acquire(struct static_mesh_system_state* state, kname asset_name);
KAPI kstatic_mesh_instance static_mesh_instance_acquire_from_package(struct static_mesh_system_state* state, kname asset_name, kname package_name);
KAPI void static_mesh_instance_release(struct static_mesh_system_state* state, kstatic_mesh_instance* instance);

KAPI b8 static_mesh_is_loaded(struct static_mesh_system_state* state, kstatic_mesh m);
KAPI b8 static_mesh_tint_get(struct static_mesh_system_state* state, kstatic_mesh_instance instance, vec4* out_tint);
KAPI b8 static_mesh_tint_set(struct static_mesh_system_state* state, kstatic_mesh_instance instance, vec4 tint);
KAPI b8 static_mesh_extents_get(struct static_mesh_system_state* state, kstatic_mesh m, extents_3d* out_extents);

KAPI b8 static_mesh_submesh_count_get(struct static_mesh_system_state* state, kstatic_mesh m, u16* out_count);
KAPI const kgeometry* static_mesh_submesh_geometry_get_at(struct static_mesh_system_state* state, kstatic_mesh m, u16 index);
KAPI const material_instance* static_mesh_submesh_material_instance_get_at(struct static_mesh_system_state* state, kstatic_mesh_instance instance, u16 index);

KAPI b8 static_mesh_render_data_generate(struct static_mesh_system_state* state, kstatic_mesh_instance instance, kstatic_mesh_render_data_flag_bits flags, kstatic_mesh_render_data* out_render_data);
KAPI void static_mesh_render_data_destroy(kstatic_mesh_render_data* render_data);
