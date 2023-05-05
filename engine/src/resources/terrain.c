#include "terrain.h"

#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/geometry_utils.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.inl"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"

b8 terrain_create(const terrain_config *config, terrain *out_terrain) {
    if (!out_terrain) {
        KERROR("terrain_create requires a valid pointer to out_terrain.");
        return false;
    }

    out_terrain->name = string_duplicate(config->name);

    if (!config->tile_count_x) {
        KERROR("Tile count x cannot be less than one.");
        return false;
    }

    if (!config->tile_count_z) {
        KERROR("Tile count z cannot be less than one.");
        return false;
    }

    out_terrain->xform = config->xform;

    // TODO: calculate based on actual terrain dimensions.
    out_terrain->extents = (extents_3d){0};
    out_terrain->origin = vec3_zero();

    out_terrain->tile_count_x = config->tile_count_x;
    out_terrain->tile_count_z = config->tile_count_z;
    out_terrain->tile_scale_x = config->tile_scale_x;
    out_terrain->tile_scale_z = config->tile_scale_z;

    out_terrain->scale_y = config->scale_y;

    out_terrain->vertex_count = out_terrain->tile_count_x * out_terrain->tile_count_z;
    out_terrain->vertices = kallocate(sizeof(terrain_vertex) * out_terrain->vertex_count, MEMORY_TAG_ARRAY);

    out_terrain->vertex_data_length = out_terrain->vertex_count;
    out_terrain->vertex_datas = kallocate(sizeof(terrain_vertex_data) * out_terrain->vertex_data_length, MEMORY_TAG_ARRAY);
    kcopy_memory(out_terrain->vertex_datas, config->vertex_datas, config->vertex_data_length * sizeof(terrain_vertex_data));

    out_terrain->index_count = out_terrain->vertex_count * 6;
    out_terrain->indices = kallocate(sizeof(u32) * out_terrain->index_count, MEMORY_TAG_ARRAY);

    out_terrain->material_count = config->material_count;
    if (out_terrain->material_count) {
        out_terrain->materials = kallocate(sizeof(material *) * out_terrain->material_count, MEMORY_TAG_ARRAY);
        out_terrain->material_names = kallocate(sizeof(char *) * out_terrain->material_count, MEMORY_TAG_ARRAY);
        kcopy_memory(out_terrain->material_names, config->material_names, sizeof(char *) * out_terrain->material_count);
    } else {
        out_terrain->materials = 0;
        out_terrain->material_names = 0;
    }

    return true;
}
void terrain_destroy(terrain *t) {
    // TODO: Fill me out!
}

b8 terrain_initialize(terrain *t) {
    if (!t) {
        KERROR("terrain_initialize requires a valid pointer to a terrain!");
        return false;
    }

    // Generate vertices.
    for (u32 z = 0, i = 0; z < t->tile_count_z; z++) {
        for (u32 x = 0; x < t->tile_count_x; ++x, ++i) {
            terrain_vertex *v = &t->vertices[i];
            v->position.x = x * t->tile_scale_x;
            v->position.z = z * t->tile_scale_z;
            v->position.y = t->vertex_datas[i].height * t->scale_y;

            v->colour = vec4_one();       // white;
            v->normal = (vec3){0, 1, 0};  // TODO: calculate based on geometry.
            v->texcoord.x = (f32)x;
            v->texcoord.y = (f32)z;

            // TODO: Figure out a way to auto-assign terrain material weights. Height?
            v->material_weights[0] = 1.0f;
        }
    }

    // Generate indices.
    for (u32 z = 0, i = 0; z < t->tile_count_z - 1; z++) {
        for (u32 x = 0; x < t->tile_count_x - 1; ++x, i += 6) {
            u32 v0 = (z * t->tile_count_x) + x;
            u32 v1 = (z * t->tile_count_x) + x + 1;
            u32 v2 = ((z + 1) * t->tile_count_x) + x;
            u32 v3 = ((z + 1) * t->tile_count_x) + x + 1;

            // v0, v1, v2, v2, v1, v3
            t->indices[i + 0] = v2;
            t->indices[i + 1] = v1;
            t->indices[i + 2] = v0;
            t->indices[i + 3] = v3;
            t->indices[i + 4] = v1;
            t->indices[i + 5] = v2;
        }
    }

    terrain_geometry_generate_normals(t->vertex_count, t->vertices,
                                      t->index_count, t->indices);
    terrain_geometry_generate_tangents(t->vertex_count, t->vertices,
                                       t->index_count, t->indices);

    return true;
}

b8 terrain_load(terrain *t) {
    if (!t) {
        KERROR("terrain_load requires a valid pointer to a terrain, ya dingus!");
        return false;
    }

    geometry *g = &t->geo;
    kzero_memory(g, sizeof(geometry));
    g->generation = INVALID_ID_U16;

    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_create(g, sizeof(terrain_vertex), t->vertex_count,
                                  t->vertices, sizeof(u32), t->index_count,
                                  t->indices)) {
        return false;
    }

    // Copy over extents, center, etc.
    g->center = t->origin;
    g->extents.min = t->extents.min;
    g->extents.max = t->extents.max;
    // TODO: offload generation increments to frontend. Also do this in geometry_system_create.
    g->generation++;

    // Acquire material(s)
    for (u32 i = 0; i < t->material_count; ++i) {
        t->materials[i] = material_system_acquire(t->material_names[i]);
        if (!t->materials[i]) {
            t->materials[i] = material_system_get_default();
        }
    }

    return true;
}
b8 terrain_unload(terrain *t) { return true; }

b8 terrain_update(terrain *t) { return true; }
