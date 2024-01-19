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

    // The number of detail levels  (LOD) is calculated by first taking the dimension
    // figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    out_terrain->lod_count = (u32)(kfloor(klog2(config->chunk_size)) + 1);

    // Setup memory for the chunks.
    out_terrain->chunk_count = (config->tile_count_x / config->chunk_size) * (config->tile_count_z / config->chunk_size);
    out_terrain->chunks = kallocate(sizeof(terrain_chunk) * out_terrain->chunk_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < out_terrain->chunk_count; ++i) {
        terrain_chunk *chunk = &out_terrain->chunks[i];

        // NOTE: 1 extra row of verts to account for final edge.
        chunk->surface_vertex_count = (out_terrain->chunk_size + 1) * (out_terrain->chunk_size + 1);
        // Total vertex count includes side skirts.
        chunk->total_vertex_count = chunk->surface_vertex_count + ((out_terrain->chunk_size + 1) * 4);
        chunk->vertices = kallocate(sizeof(terrain_vertex) * chunk->total_vertex_count, MEMORY_TAG_ARRAY);

        chunk->lods = kallocate(sizeof(terrain_chunk_lod) * out_terrain->lod_count, MEMORY_TAG_ARRAY);
        for (u32 j = 0; j < out_terrain->lod_count; ++j) {
            terrain_chunk_lod *lod = &chunk->lods[j];

            u32 dimenson = j == 0 ? out_terrain->chunk_size : (u32)(out_terrain->chunk_size * (1.0f / (j * 2)));
            lod->surface_index_count = (dimenson * dimenson) * 6;
            lod->total_index_count = lod->surface_index_count + (dimenson * 6 * 4);
            lod->indices = kallocate(sizeof(u32) * lod->total_index_count, MEMORY_TAG_ARRAY);
        }

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
                kfree(chunk->vertices, sizeof(terrain_vertex) * chunk->total_vertex_count, MEMORY_TAG_ARRAY);
            }

            if (chunk->lods) {
                for (u32 j = 0; j < t->lod_count; ++j) {
                    terrain_chunk_lod *lod = &chunk->lods[j];
                    if (lod->indices) {
                        kfree(lod->indices, sizeof(u32) * lod->total_index_count, MEMORY_TAG_ARRAY);
                    }
                }
                kfree(chunk->lods, sizeof(terrain_chunk_lod) * t->lod_count, MEMORY_TAG_ARRAY);
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

    // Generate surface data.
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
            y_max = KMAX(y_max, v->position.y);

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

    // Generate skirt data for each side.
    // Left, top, right, bottom
    u32 vi = chunk->surface_vertex_count;
    // Left
    for (u32 i = 0; i < chunk_dimension; ++i, ++vi) {
        // Source vertex
        terrain_vertex *sv = &chunk->vertices[i * chunk_dimension];
        // Target vertex
        terrain_vertex *v = &chunk->vertices[vi];

        kcopy_memory(v, sv, sizeof(terrain_vertex));
        v->position.y -= 0.5f;
    }
    // right
    for (u32 i = 0; i < chunk_dimension; ++i, ++vi) {
        // Source vertex
        terrain_vertex *sv = &chunk->vertices[(i * chunk_dimension) + t->chunk_size];
        // Target vertex
        terrain_vertex *v = &chunk->vertices[vi];

        kcopy_memory(v, sv, sizeof(terrain_vertex));
        v->position.y -= 0.5f;
    }
    // top
    for (u32 i = 0; i < chunk_dimension; ++i, ++vi) {
        // Source vertex
        terrain_vertex *sv = &chunk->vertices[i];
        // Target vertex
        terrain_vertex *v = &chunk->vertices[vi];

        kcopy_memory(v, sv, sizeof(terrain_vertex));
        v->position.y -= 0.5f;
    }
    // bottom
    for (u32 i = 0; i < chunk_dimension; ++i, ++vi) {
        // Source vertex
        terrain_vertex *sv = &chunk->vertices[i + (chunk_dimension * t->chunk_size)];  // wrong?
        // Target vertex
        terrain_vertex *v = &chunk->vertices[vi];

        kcopy_memory(v, sv, sizeof(terrain_vertex));
        v->position.y -= 0.5f;
    }

    // Calculate extents for this chunk.
    chunk->geo.extents.min = chunk->vertices[0].position;
    chunk->geo.extents.min.y = y_min;
    // TODO: vertex count - 1 won't work once extra "connective" geometry exists.
    chunk->geo.extents.max = chunk->vertices[chunk->surface_vertex_count - 1].position;
    chunk->geo.extents.max.y = y_max;

    chunk->geo.center = extents_3d_half(chunk->geo.extents);

    // Generate indices.
    for (u32 j = 0; j < t->lod_count; ++j) {
        terrain_chunk_lod *lod = &chunk->lods[j];

        // Surface indices.
        for (u32 row = 0, i = 0; row < t->chunk_size; row += (1 << j)) {
            for (u32 col = 0; col < t->chunk_size; col += (1 << j), i += 6) {
                u32 next_row = row + (1 << j);
                u32 next_col = col + (1 << j);
                u32 v0 = (row * (chunk_dimension)) + col;
                u32 v1 = (row * (chunk_dimension)) + next_col;
                u32 v2 = (next_row * (chunk_dimension)) + col;
                u32 v3 = (next_row * (chunk_dimension)) + next_col;

                // v0, v1, v2, v2, v1, v3
                lod->indices[i + 0] = v2;
                lod->indices[i + 1] = v1;
                lod->indices[i + 2] = v0;
                lod->indices[i + 3] = v3;
                lod->indices[i + 4] = v1;
                lod->indices[i + 5] = v2;
            }
        }
        // The number of "tiles" for the current LOD.
        u32 dimension = j == 0 ? t->chunk_size : (u32)(t->chunk_size * (1.0f / (j * 2)));

        // Generate skirt indices.
        u32 ii = lod->surface_index_count;
        u32 vi = chunk->surface_vertex_count;

        // left
        for (u32 i = 0; i < dimension; ++i, ii += 6, vi += (1 << j)) {
            u32 v0 = i * chunk_dimension;
            u32 v1 = (i + (1 << j)) * chunk_dimension;
            u32 v2 = vi;
            u32 v3 = vi + (1 << j);

            lod->indices[ii + 0] = v2;
            lod->indices[ii + 1] = v1;
            lod->indices[ii + 2] = v0;
            lod->indices[ii + 3] = v2;
            lod->indices[ii + 4] = v3;
            lod->indices[ii + 5] = v1;
        }

        vi++;

        // right
        for (u32 i = 0; i < dimension; ++i, ii += 6, vi += (1 << j)) {
            u32 v0 = (i * chunk_dimension) + t->chunk_size;
            u32 v1 = ((i + (1 << j)) * chunk_dimension) + t->chunk_size;
            u32 v2 = vi;
            u32 v3 = vi + (1 << j);

            lod->indices[ii + 0] = v0;
            lod->indices[ii + 1] = v1;
            lod->indices[ii + 2] = v2;
            lod->indices[ii + 3] = v1;
            lod->indices[ii + 4] = v3;
            lod->indices[ii + 5] = v2;
        }
        vi++;

        // top
        for (u32 i = 0; i < dimension; ++i, ii += 6, vi += (1 << j)) {
            u32 v0 = i;
            u32 v1 = (i + (1 << j));
            u32 v2 = vi;
            u32 v3 = vi + (1 << j);

            lod->indices[ii + 0] = v0;
            lod->indices[ii + 1] = v1;
            lod->indices[ii + 2] = v2;
            lod->indices[ii + 3] = v1;
            lod->indices[ii + 4] = v3;
            lod->indices[ii + 5] = v2;
        }
        vi++;

        // bottom
        for (u32 i = 0; i < dimension; ++i, ii += 6, vi += (1 << j)) {
            u32 v0 = i + (chunk_dimension * t->chunk_size);
            u32 v1 = (i + (1 << j)) + (chunk_dimension * t->chunk_size);
            u32 v2 = vi;
            u32 v3 = vi + (1 << j);

            // TODO: invert winding order?
            // v0, v1, v2, v2, v1, v3
            lod->indices[ii + 0] = v0;
            lod->indices[ii + 1] = v1;
            lod->indices[ii + 2] = v2;
            lod->indices[ii + 3] = v1;
            lod->indices[ii + 4] = v3;
            lod->indices[ii + 5] = v2;

            /* u32 v0 = i + (t->chunk_size * (t->chunk_size - 1));
            u32 v1 = (i + 1) + (t->chunk_size * (t->chunk_size - 1));
            u32 v2 = ii;
            u32 v3 = ii + 1;

            // v0, v1, v2, v2, v1, v3 // TODO: winding?
            lod->indices[ii + 0] = v0;
            lod->indices[ii + 1] = v1;
            lod->indices[ii + 2] = v2;
            lod->indices[ii + 3] = v1;
            lod->indices[ii + 4] = v3;
            lod->indices[ii + 5] = v2; */
        }
    }

    // Only generate based on first LOD, others should naturally interpolate as verts are skipped.
    terrain_geometry_generate_normals(chunk->surface_vertex_count, chunk->vertices, chunk->lods[0].surface_index_count, chunk->lods[0].indices);
    terrain_geometry_generate_tangents(chunk->surface_vertex_count, chunk->vertices, chunk->lods[0].surface_index_count, chunk->lods[0].indices);
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

    // Base geometry off lod level 0.
    if (!renderer_geometry_create(g, sizeof(terrain_vertex), chunk->total_vertex_count, chunk->vertices, sizeof(u32), chunk->lods[0].total_index_count, chunk->lods[0].indices)) {
        KERROR("Failed to upload geometry for terrain chunk.");
        return false;
    }
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(g)) {
        return false;
    }

    // No need to upload this data twice, so just copy over these to the first LOD.
    chunk->lods[0].indices = g->indices;
    chunk->lods[0].total_index_count = g->index_count;
    chunk->lods[0].index_buffer_offset = g->index_buffer_offset;

    // Upload index data for all other LODs.
    renderbuffer *index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
    for (u32 i = 1; i < t->lod_count; ++i) {
        terrain_chunk_lod *lod = &chunk->lods[i];
        u32 total_size = sizeof(u32) * lod->total_index_count;
        if (!renderer_renderbuffer_allocate(index_buffer, total_size, &lod->index_buffer_offset)) {
            KERROR("Failed to allocate memory for terrain chunk lod index data.");
            return false;
        }
        if (!renderer_renderbuffer_load_range(index_buffer, lod->index_buffer_offset, total_size, lod->indices)) {
            KERROR("Failed to upload index data for terrain chunk lod.");
            return false;
        }
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
            kfree(chunk->vertices, sizeof(terrain_vertex) * chunk->total_vertex_count, MEMORY_TAG_ARRAY);
        }
        if (chunk->lods) {
            renderbuffer *index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
            for (u32 j = 0; j < t->lod_count; ++j) {
                terrain_chunk_lod *lod = &chunk->lods[j];
                if (lod->indices) {
                    renderer_renderbuffer_free(index_buffer, sizeof(u32) * lod->total_index_count, lod->index_buffer_offset);
                    kfree(lod->indices, sizeof(u32) * lod->total_index_count, MEMORY_TAG_ARRAY);
                }
            }
            kfree(chunk->lods, sizeof(terrain_chunk_lod) * t->lod_count, MEMORY_TAG_ARRAY);
        }
    }

    kfree(t->chunks, sizeof(terrain_chunk) * t->chunk_count, MEMORY_TAG_ARRAY);
    t->chunks = 0;
    t->chunk_count = 0;

    t->id.uniqueid = INVALID_ID_U64;

    return true;
}

b8 terrain_update(terrain *t) { return true; }
