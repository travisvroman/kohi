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

    // TODO: calculate based on actual terrain dimensions.
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

        u32 chunk_size_sq = (out_terrain->chunk_size + 1) * (out_terrain->chunk_size + 1);
        chunk->vertex_count = chunk_size_sq;
        chunk->vertices = kallocate(sizeof(terrain_vertex) * chunk->vertex_count, MEMORY_TAG_ARRAY);

        chunk->index_count = chunk_size_sq * 6;
        chunk->indices = kallocate(sizeof(u32) * chunk->index_count, MEMORY_TAG_ARRAY);

        // Invalidate the geometry.
        chunk->geo.id = INVALID_ID;
        chunk->geo.generation = INVALID_ID_U16;
    }

    /* out_terrain->vertex_count = out_terrain->tile_count_x * out_terrain->tile_count_z;
    out_terrain->vertices = kallocate(sizeof(terrain_vertex) * out_terrain->vertex_count, MEMORY_TAG_ARRAY); */

    // Height data.
    out_terrain->vertex_data_length = config->tile_count_x * config->tile_count_z * 4;
    out_terrain->vertex_datas = kallocate(sizeof(terrain_vertex_data) * out_terrain->vertex_data_length, MEMORY_TAG_ARRAY);
    kcopy_memory(out_terrain->vertex_datas, config->vertex_datas, config->vertex_data_length * sizeof(terrain_vertex_data));

    /* out_terrain->index_count = out_terrain->vertex_count * 6;
    out_terrain->indices = kallocate(sizeof(u32) * out_terrain->index_count, MEMORY_TAG_ARRAY); */

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
                chunk->vertices = 0;
                chunk->vertex_count = 0;
            }

            if (chunk->indices) {
                kfree(chunk->indices, sizeof(u32) * chunk->index_count, MEMORY_TAG_ARRAY);
                chunk->indices = 0;
                chunk->index_count = 0;
            }
        }
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

static void terrain_chunk_initialize(terrain *t, terrain_chunk *chunk, u32 chunk_index, u32 global_base_index, u32 chunk_row_count, u32 chunk_col_count) {
    u32 x_offset = chunk_index % chunk_col_count;
    u32 z_offset = chunk_index / chunk_col_count;

    f32 x_pos = x_offset * t->tile_scale_x;
    f32 z_pos = z_offset * t->tile_scale_z;

    for (u32 z = 0, i = 0; z < t->chunk_size; ++z) {
        for (u32 x = 0; x < t->chunk_size; ++x, ++i) {
            terrain_vertex *v = &chunk->vertices[i];
            v->position.x = x_pos + (x * t->tile_scale_x);
            v->position.z = z_pos + (z * t->tile_scale_z);
            v->position.y = t->vertex_datas[global_base_index].height * t->scale_y;

            v->colour = vec4_one();  // white;
            v->normal = (vec3){0, 1, 0};
            v->texcoord.x = x_offset + (f32)x;
            v->texcoord.y = z_offset + (f32)z;

            // NOTE: Assigning default weights based on overall height. Lower indices are
            // lower in altitude.
            // NOTE: These must overlap the min/max to blend properly.
            v->material_weights[0] = kattenuation_min_max(-0.2f, 0.2f, t->vertex_datas[i].height);  // mid 0
            v->material_weights[1] = kattenuation_min_max(0.0f, 0.3f, t->vertex_datas[i].height);   // mid .15
            v->material_weights[2] = kattenuation_min_max(0.15f, 0.9f, t->vertex_datas[i].height);  // mid 5
            v->material_weights[3] = kattenuation_min_max(0.5f, 1.2f, t->vertex_datas[i].height);   // mid 9
        }
    }

    // Generate indices.
    for (u32 z = 0, i = 0; z < t->chunk_size - 1; z++) {
        for (u32 x = 0; x < t->chunk_size - 1; ++x, i += 6) {
            u32 v0 = (z * t->chunk_size) + x;
            u32 v1 = (z * t->chunk_size) + x + 1;
            u32 v2 = ((z + 1) * t->chunk_size) + x;
            u32 v3 = ((z + 1) * t->chunk_size) + x + 1;

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
            u32 global_base_index = x * t->chunk_size + z * t->chunk_size;
            terrain_chunk_initialize(t, &t->chunks[i], i, global_base_index, chunk_row_count, chunk_col_count);
        }
    }

    /* // LEFTOFF: Redo this for chunks
    // Generate vertices.
    for (u32 z = 0, i = 0; z < t->tile_count_z; z++) {
        for (u32 x = 0; x < t->tile_count_x; ++x, ++i) {
            terrain_vertex *v = &t->vertices[i];
            v->position.x = x * t->tile_scale_x;
            v->position.z = z * t->tile_scale_z;
            v->position.y = t->vertex_datas[i].height * t->scale_y;

            v->colour = vec4_one();  // white;
            v->normal = (vec3){0, 1, 0};
            v->texcoord.x = (f32)x;
            v->texcoord.y = (f32)z;

            // -3 <- 1lo
            // -2
            // -1
            // 0 <- 2lo
            // 1
            // 2
            // 3 <- 1hi/3lo
            // 4
            // 5
            // 6 <- 2hi/4lo
            // 7
            // 8
            // 9 <-3hi
            // 1
            // 11
            // 12 <- 4hi
            // 13
            // NOTE: Assigning default weights based on overall height. Lower indices are
            // lower in altitude.
            // NOTE: These must overlap the min/max to blend properly.
            v->material_weights[0] = kattenuation_min_max(-0.2f, 0.2f, t->vertex_datas[i].height);  // mid 0
            v->material_weights[1] = kattenuation_min_max(0.0f, 0.3f, t->vertex_datas[i].height);   // mid .15
            v->material_weights[2] = kattenuation_min_max(0.15f, 0.9f, t->vertex_datas[i].height);  // mid 5
            v->material_weights[3] = kattenuation_min_max(0.5f, 1.2f, t->vertex_datas[i].height);   // mid 9
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

    terrain_geometry_generate_normals(t->vertex_count, t->vertices, t->index_count, t->indices);
    terrain_geometry_generate_tangents(t->vertex_count, t->vertices, t->index_count, t->indices); */

    return true;
}

static b8 terrain_chunk_load(terrain *t, terrain_chunk *chunk) {
    geometry *g = &chunk->geo;

    // LEFTOFF: index buffer alignment issues???
    if (!renderer_geometry_create(g, sizeof(terrain_vertex), chunk->vertex_count, chunk->vertices, sizeof(u32), chunk->index_count, chunk->indices)) {
        KERROR("Failed to upload geometry for terrain chunk.");
        return false;
    }
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(g)) {
        return false;
    }

    // Calculate extents for this chunk.
    vec3 min = chunk->vertices[0].position;
    vec3 max = chunk->vertices[chunk->vertex_count - 1].position;
    vec3 center = vec3_add(min, vec3_mul_scalar(vec3_sub(max, min), 0.5f));
    g->center = (vec3){center.x, t->origin.y, center.z};
    g->extents.min = (vec3){min.x, t->extents.min.y, min.z};
    g->extents.max = (vec3){max.x, t->extents.max.y, max.z};
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
            // TODO: Clean up the failure...
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
