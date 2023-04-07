#include "terrain.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "core/kmemory.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "math/geometry_utils.h"
#include "renderer/renderer_frontend.h"

b8 terrain_create(terrain_config config, terrain* out_terrain) {
    if (!out_terrain) {
        KERROR("terrain_create requires a valid pointer to out_terrain.");
        return false;
    }

    out_terrain->config = config;
    out_terrain->name = string_duplicate(config.name);

    return true;
}
void terrain_destroy(terrain* t) {
    // TODO: Fill me out!
}

b8 terrain_initialize(terrain* t) {
    if (!t) {
        KERROR("terrain_initialize requires a valid pointer to a terrain!");
        return false;
    }

    if (!t->config.tile_count_x) {
        KERROR("Tile count x cannot be less than one.");
        return false;
    }

    if (!t->config.tile_count_z) {
        KERROR("Tile count z cannot be less than one.");
        return false;
    }

    t->xform = transform_create();
    t->extents = (extents_3d){0};
    t->origin = vec3_zero();

    t->tile_count_x = t->config.tile_count_x;
    t->tile_count_z = t->config.tile_count_z;
    t->tile_scale_x = t->config.tile_scale_x;
    t->tile_scale_z = t->config.tile_scale_z;

    t->vertex_count = t->tile_count_x * t->tile_count_z;
    t->vertices = kallocate(sizeof(terrain_vertex) * t->vertex_count, MEMORY_TAG_ARRAY);

    t->index_count = t->vertex_count * 6;
    t->indices = kallocate(sizeof(u32) * t->index_count, MEMORY_TAG_ARRAY);

    t->material_count = t->config.material_count;
    t->materials = kallocate(sizeof(material_config) * t->material_count, MEMORY_TAG_ARRAY);

    // Generate vertices.
    for (u32 z = 0; z < t->tile_count_z; z++) {
        for (u32 x = 0, i = 0; x < t->tile_count_x; ++x, ++i) {
            terrain_vertex* v = &t->vertices[i];
            v->position.x = x * t->tile_scale_x;
            v->position.z = z * t->tile_scale_z;
            v->position.y = 0.0f;  // <-- this will be modified by a heightmap.

            v->colour = vec4_one();       // white;
            v->normal = (vec3){0, 1, 0};  // TODO: calculate based on geometry.
            v->texcoord.x = (f32)x;
            v->texcoord.y = (f32)z;
            kzero_memory(v->material_weights, sizeof(f32) * TERRAIN_MAX_MATERIAL_COUNT);
            v->material_weights[0] = 1.0f;
            v->tangent = vec3_zero();  // TODO: obviously wrong.
        }
    }

    // Generate indices.
    for (u32 z = 0; z < t->tile_count_z - 1; z++) {
        for (u32 x = 0, i = 0; x < t->tile_count_x - 1; ++x, i += 6) {
            u32 v0 = (z * t->tile_count_x) + x;
            u32 v1 = (z * t->tile_count_x) + x + 1;
            u32 v2 = ((z + 1) * t->tile_count_x) + x;
            u32 v3 = ((z + 1) * t->tile_count_x) + x + 1;

            // v0, v1, v2, v2, v1, v3
            t->indices[i + 0] = v0;
            t->indices[i + 1] = v1;
            t->indices[i + 2] = v2;
            t->indices[i + 3] = v2;
            t->indices[i + 4] = v1;
            t->indices[i + 5] = v3;
        }
    }

    return true;
}

b8 terrain_load(terrain* t) {
    if (!t) {
        KERROR("terrain_load requires a valid pointer to a terrain, ya dingus!");
        return false;
    }

    geometry* g = &t->geo;
    kzero_memory(g, sizeof(geometry));
    g->generation = INVALID_ID_U16;

    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_create_geometry(g, sizeof(terrain_vertex), t->vertex_count, t->vertices, sizeof(u32), t->index_count, t->indices)) {
        return false;
    }

    // Copy over extents, center, etc.
    g->center = t->origin;
    g->extents.min = t->extents.min;
    g->extents.max = t->extents.max;
    g->generation++;

    // TODO: acquire material(s)
    // Acquire the material
    // if (string_length(config.material_name) > 0) {
    //     g->material = material_system_acquire(config.material_name);
    //     if (!g->material) {
    //         g->material = material_system_get_default();
    //     }
    // }

    return true;
}
b8 terrain_unload(terrain* t) {
    return true;
}

b8 terrain_update(terrain* t) {
    return true;
}

b8 terrain_render(const terrain* t, frame_data* p_frame_data) {
    return true;
}