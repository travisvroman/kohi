#include "terrain.h"

#include "core/identifier.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "math/geometry_utils.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"
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

    if (!config->chunk_size) {
        KERROR("Chunk size cannot be less than one.");
        return false;
    }

    out_terrain->xform = config->xform;

    out_terrain->extents = (extents_3d){0};
    out_terrain->origin = vec3_zero();

    out_terrain->tile_count_x = config->tile_count_x;
    out_terrain->tile_count_z = config->tile_count_z;
    out_terrain->tile_scale_x = config->tile_scale_x;
    out_terrain->tile_scale_z = config->tile_scale_z;

    out_terrain->scale_y = config->scale_y;

    out_terrain->chunk_size = config->chunk_size;

    // Setup memory for the chunks.
    out_terrain->chunk_count = (config->tile_count_x / config->chunk_size) * (config->tile_count_z / config->chunk_size);
    out_terrain->chunks = kallocate(sizeof(terrain_chunk) * out_terrain->chunk_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < out_terrain->chunk_count; ++i) {
        terrain_chunk *chunk = &out_terrain->chunks[i];

        // NOTE: 1 extra row of verts to account for final edge.
        chunk->vertex_count = (out_terrain->chunk_size + 1) * (out_terrain->chunk_size + 1);
        chunk->vertices = kallocate(sizeof(terrain_vertex) * chunk->vertex_count, MEMORY_TAG_ARRAY);

        chunk->index_count = (out_terrain->chunk_size * out_terrain->chunk_size) * 6;
        chunk->indices = kallocate(sizeof(u32) * chunk->index_count, MEMORY_TAG_ARRAY);

        // Invalidate the geometry.
        chunk->geo.id = INVALID_ID;
        chunk->geo.generation = INVALID_ID_U16;
    }

    // Height data.
    out_terrain->vertex_data_length = config->tile_count_x * config->tile_count_z * 4;
    out_terrain->vertex_datas = kallocate(sizeof(terrain_vertex_data) * out_terrain->vertex_data_length, MEMORY_TAG_ARRAY);
    kcopy_memory(out_terrain->vertex_datas, config->vertex_datas, config->vertex_data_length * sizeof(terrain_vertex_data));

    out_terrain->material_count = config->material_count;
    if (out_terrain->material_count) {
        out_terrain->material_names = kallocate(sizeof(char *) * out_terrain->material_count, MEMORY_TAG_ARRAY);
        kcopy_memory(out_terrain->material_names, config->material_names, sizeof(char *) * out_terrain->material_count);
    } else {
        out_terrain->material_names = 0;
    }

    return true;
}
void terrain_destroy(terrain *t) {
    if (t->name) {
        u32 length = string_length(t->name);
        kfree(t->name, length + 1, MEMORY_TAG_STRING);
        t->name = 0;
    }

    if (t->chunks) {
        for (u32 i = 0; i < t->chunk_count; ++i) {
            terrain_chunk *chunk = &t->chunks[i];
            if (chunk->vertices) {
                kfree(chunk->vertices, sizeof(terrain_vertex) * chunk->vertex_count, MEMORY_TAG_ARRAY);
            }

            if (chunk->indices) {
                kfree(chunk->indices, sizeof(u32) * chunk->index_count, MEMORY_TAG_ARRAY);
            }
        }

        kfree(t->chunks, sizeof(terrain_chunk) * t->chunk_count, MEMORY_TAG_ARRAY);
        t->chunks = 0;
        t->chunk_count = 0;
    }

    if (t->material_names) {
        kfree(t->material_names, sizeof(char *) * t->material_count, MEMORY_TAG_ARRAY);
        t->material_names = 0;
    }

    if (t->vertex_datas) {
        kfree(t->vertex_datas, sizeof(terrain_vertex_data) * t->vertex_data_length, MEMORY_TAG_ARRAY);
        t->vertex_datas = 0;
    }

    // NOTE: Don't just zero the memory, because some structs like geometry should have invalid ids.
    t->scale_y = 0;
    t->tile_scale_x = 0;
    t->tile_scale_z = 0;
    t->tile_count_x = 0;
    t->tile_count_z = 0;
    t->vertex_data_length = 0;
    kzero_memory(&t->origin, sizeof(vec3));
    kzero_memory(&t->extents, sizeof(vec3));
}

static void terrain_chunk_initialize(terrain *t, terrain_chunk *chunk, u32 chunk_offset_x, u32 chunk_offset_z) {
    // The base x/z position of the first vertex within the chunk.
    f32 chunk_base_pos_x = chunk_offset_x * t->chunk_size * t->tile_scale_x;
    f32 chunk_base_pos_z = chunk_offset_z * t->chunk_size * t->tile_scale_z;

    f32 y_min = 99999.0f;
    f32 y_max = -99999.0f;

    u32 chunk_dimension = t->chunk_size + 1;
    for (u32 z = 0, i = 0; z < chunk_dimension; ++z) {
        for (u32 x = 0; x < chunk_dimension; ++x, ++i) {
            terrain_vertex *v = &chunk->vertices[i];
            v->position.x = chunk_base_pos_x + (x * t->tile_scale_x);
            v->position.z = chunk_base_pos_z + (z * t->tile_scale_z);

            // Get global offset into the terrain tile array.
            u32 globalx = x + (chunk_offset_x * t->chunk_size);
            u32 globalz = z + (chunk_offset_z * t->chunk_size);
            u32 global_terrain_index = globalx + (globalz * t->tile_count_x);

            terrain_vertex_data *vert_data = &t->vertex_datas[global_terrain_index];
            f32 point_height = vert_data->height;

            v->position.y = point_height * t->scale_y;
            y_min = KMIN(y_min, v->position.y);
            y_max = KMIN(y_max, v->position.y);

            v->colour = vec4_one();  // white;
            v->normal = (vec3){0, 1, 0};
            v->texcoord.x = chunk_offset_x + (f32)x;
            v->texcoord.y = chunk_offset_z + (f32)z;

            // NOTE: Assigning default weights based on overall height. Lower material indices are
            // lower in altitude.
            // NOTE: These must overlap the min/max to blend properly.
            v->material_weights[0] = kattenuation_min_max(-0.2f, 0.2f, point_height);  // mid 0
            v->material_weights[1] = kattenuation_min_max(0.0f, 0.3f, point_height);   // mid .15
            v->material_weights[2] = kattenuation_min_max(0.15f, 0.9f, point_height);  // mid 5
            v->material_weights[3] = kattenuation_min_max(0.5f, 1.2f, point_height);   // mid 9
        }
    }

    // Calculate extents for this chunk.
    vec3 min = chunk->vertices[0].position;
    min.y = y_min;
    vec3 max = chunk->vertices[chunk->vertex_count - 1].position;
    max.y = y_max;
    vec3 center = vec3_add(min, vec3_mul_scalar(vec3_sub(max, min), 0.5f));
    chunk->geo.center = (vec3){center.x, t->origin.y, center.z};
    chunk->geo.extents.min = min;
    chunk->geo.extents.max = max;

    // Generate indices.
    for (u32 row = 0, i = 0; row < t->chunk_size; row++) {
        for (u32 col = 0; col < t->chunk_size; ++col, i += 6) {
            u32 next_row = row + 1;
            u32 next_col = col + 1;
            u32 v0 = (row * (chunk_dimension)) + col;
            u32 v1 = (row * (chunk_dimension)) + next_col;
            u32 v2 = (next_row * (chunk_dimension)) + col;
            u32 v3 = (next_row * (chunk_dimension)) + next_col;

            // v0, v1, v2, v2, v1, v3
            chunk->indices[i + 0] = v2;
            chunk->indices[i + 1] = v1;
            chunk->indices[i + 2] = v0;
            chunk->indices[i + 3] = v3;
            chunk->indices[i + 4] = v1;
            chunk->indices[i + 5] = v2;
        }
    }

    terrain_geometry_generate_normals(chunk->vertex_count, chunk->vertices, chunk->index_count, chunk->indices);
    terrain_geometry_generate_tangents(chunk->vertex_count, chunk->vertices, chunk->index_count, chunk->indices);
}

b8 terrain_initialize(terrain *t) {
    if (!t) {
        KERROR("terrain_initialize requires a valid pointer to a terrain!");
        return false;
    }

    u32 chunk_row_count = t->tile_count_z / t->chunk_size;
    u32 chunk_col_count = t->tile_count_x / t->chunk_size;

    for (u32 z = 0, i = 0; z < chunk_row_count; z++) {
        for (u32 x = 0; x < chunk_col_count; ++x, ++i) {
            // x/z chunk indices within terrain grid.
            u32 chunk_offset_x = i % chunk_col_count;
            u32 chunk_offset_z = i / chunk_col_count;
            terrain_chunk_initialize(t, &t->chunks[i], chunk_offset_x, chunk_offset_z);
        }
    }

    return true;
}

static b8 terrain_chunk_load(terrain *t, terrain_chunk *chunk) {
    geometry *g = &chunk->geo;

    if (!renderer_geometry_create(g, sizeof(terrain_vertex), chunk->vertex_count, chunk->vertices, sizeof(u32), chunk->index_count, chunk->indices)) {
        KERROR("Failed to upload geometry for terrain chunk.");
        return false;
    }
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(g)) {
        return false;
    }

    // TODO: offload generation increments to frontend. Also do this in geometry_system_create.
    g->generation++;

    // Create a terrain material by copying the properties of these materials to a new terrain material.
    char terrain_material_name[MATERIAL_NAME_MAX_LENGTH] = {0};
    string_format(terrain_material_name, "terrain_mat_%s", t->name);
    g->material = material_system_acquire_terrain_material(terrain_material_name, t->material_count, (const char **)t->material_names, true);
    if (!g->material) {
        KWARN("Failed to acquire terrain material. Using defualt instead.");
        g->material = material_system_get_default_terrain();
    }

    return true;
}

b8 terrain_load(terrain *t) {
    if (!t) {
        KERROR("terrain_load requires a valid pointer to a terrain, ya dingus!");
        return false;
    }

    t->id = identifier_create();

    for (u32 i = 0; i < t->chunk_count; ++i) {
        if (!terrain_chunk_load(t, &t->chunks[i])) {
            // Clean up the failure.
            terrain_destroy(t);
            KERROR("Terrain chunk failed to load, thus the terrain cannot be loaded.");
            return false;
        }
    }

    return true;
}

b8 terrain_unload(terrain *t) {
    // Unload all chunks.
    for (u32 i = 0; i < t->chunk_count; ++i) {
        terrain_chunk *chunk = &t->chunks[i];
        material_system_release(chunk->geo.material->name);
        chunk->geo.material = 0;

        renderer_geometry_destroy(&chunk->geo);

        if (chunk->vertices) {
            kfree(chunk->vertices, sizeof(terrain_vertex) * chunk->vertex_count, MEMORY_TAG_ARRAY);
        }
        if (chunk->indices) {
            kfree(chunk->indices, sizeof(u32) * chunk->index_count, MEMORY_TAG_ARRAY);
        }
    }

    kfree(t->chunks, sizeof(terrain_chunk) * t->chunk_count, MEMORY_TAG_ARRAY);
    t->chunks = 0;
    t->chunk_count = 0;

    t->id.uniqueid = INVALID_ID_U64;

    return true;
}

b8 terrain_update(terrain *t) { return true; }
