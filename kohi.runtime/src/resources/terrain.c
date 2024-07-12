#include "terrain.h"

#include "defines.h"
#include "identifiers/identifier.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

static void terrain_chunk_destroy(terrain* t, terrain_chunk* chunk);
static void terrain_chunk_calculate_geometry(terrain* t, terrain_chunk* chunk, u32 chunk_offset_x, u32 chunk_offset_z);

typedef enum terrain_skirt_side {
    TSS_LEFT = 0,
    TSS_RIGHT = 1,
    TSS_TOP = 2,
    TSS_BOTTOM = 3,
    TSS_COUNT = 4
} terrain_skirt_side;

b8 terrain_create(const terrain_config* config, terrain* out_terrain) {
    if (!out_terrain) {
        KERROR("terrain_create requires a valid pointer to out_terrain.");
        out_terrain->state = TERRAIN_STATE_UNDEFINED;
        return false;
    }

    out_terrain->name = string_duplicate(config->name);
    out_terrain->resource_name = string_duplicate(config->resource_name);
    out_terrain->state = TERRAIN_STATE_CREATED;

    return true;
}

void terrain_destroy(terrain* t) {
    t->state = TERRAIN_STATE_UNDEFINED;
    // If the terrain is still loaded, unload it first.
    if (t->generation != INVALID_ID) {
        if (!terrain_unload(t)) {
            KERROR("Failed to properly unload terrain before destroying. See logs for details.");
        }
    }

    if (t->name) {
        u32 length = string_length(t->name);
        kfree(t->name, length + 1, MEMORY_TAG_STRING);
        t->name = 0;
    }

    if (t->chunks) {
        for (u32 i = 0; i < t->chunk_count; ++i) {
            terrain_chunk* chunk = &t->chunks[i];
            terrain_chunk_destroy(t, chunk);
        }

        kfree(t->chunks, sizeof(terrain_chunk) * t->chunk_count, MEMORY_TAG_ARRAY);
        t->chunks = 0;
        t->chunk_count = 0;
    }

    if (t->material_names) {
        kfree(t->material_names, sizeof(char*) * t->material_count, MEMORY_TAG_ARRAY);
        t->material_names = 0;
    }

    if (t->vertex_datas) {
        kfree(t->vertex_datas, sizeof(terrain_vertex_data) * t->vertex_data_length, MEMORY_TAG_ARRAY);
        t->vertex_datas = 0;
    }

    t->id.uniqueid = INVALID_ID_U64;

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

b8 terrain_initialize(terrain* t) {
    if (!t) {
        KERROR("terrain_initialize requires a valid pointer to a terrain!");
        return false;
    }

    t->state = TERRAIN_STATE_INITIALIZED;
    return true;
}

b8 terrain_load(terrain* t) {
    if (!t) {
        KERROR("terrain_load requires a valid pointer to a terrain, ya dingus!");
        return false;
    }
    t->state = TERRAIN_STATE_LOADING;

    // Load the terrain resource.
    resource terr_resource;
    if (!resource_system_load(t->resource_name, RESOURCE_TYPE_TERRAIN, 0, &terr_resource)) {
        KWARN("Failed to load terrain resource.");
        return false;
    }

    terrain_resource* typed_resource = (terrain_resource*)terr_resource.data;

    if (!typed_resource->tile_count_x) {
        KERROR("Tile count x cannot be less than one.");
        return false;
    }

    if (!typed_resource->tile_count_z) {
        KERROR("Tile count z cannot be less than one.");
        return false;
    }

    if (!typed_resource->chunk_size) {
        KERROR("Chunk size cannot be less than one.");
        return false;
    }

    t->extents = (extents_3d){0};
    t->origin = vec3_zero();

    t->tile_count_x = typed_resource->tile_count_x;
    t->tile_count_z = typed_resource->tile_count_z;
    t->tile_scale_x = typed_resource->tile_scale_x;
    t->tile_scale_z = typed_resource->tile_scale_z;

    t->scale_y = typed_resource->scale_y;

    t->chunk_size = typed_resource->chunk_size;

    // Invalidate the terrain so it doesn't get rendered before it's ready.
    t->generation = INVALID_ID;

    // The number of detail levels  (LOD) is calculated by first taking the dimension
    // figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    t->lod_count = (u32)(kfloor(klog2(typed_resource->chunk_size)) + 1);

    // Setup memory for the chunks.
    t->chunk_count = (typed_resource->tile_count_x / typed_resource->chunk_size) * (typed_resource->tile_count_z / typed_resource->chunk_size);
    t->chunks = kallocate(sizeof(terrain_chunk) * t->chunk_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < t->chunk_count; ++i) {
        terrain_chunk* chunk = &t->chunks[i];

        // NOTE: Account for one more row/column at the end so there are chunk_size number of tiles.
        u32 vertex_stride = t->chunk_size + 1;
        chunk->surface_vertex_count = vertex_stride * vertex_stride;
        // Total vertex count includes side skirts.
        chunk->total_vertex_count = chunk->surface_vertex_count + (vertex_stride * 4);
        chunk->vertices = kallocate(sizeof(terrain_vertex) * chunk->total_vertex_count, MEMORY_TAG_ARRAY);

        chunk->lods = kallocate(sizeof(terrain_chunk_lod) * t->lod_count, MEMORY_TAG_ARRAY);
        for (u32 j = 0; j < t->lod_count; ++j) {
            terrain_chunk_lod* lod = &chunk->lods[j];

            u32 lod_tile_stride = (j == 0 ? t->chunk_size : (u32)(t->chunk_size * (1.0f / (j * 2))));
            lod->surface_index_count = (lod_tile_stride * lod_tile_stride) * 6;
            lod->total_index_count = lod->surface_index_count + (lod_tile_stride * 6 * 4);
            lod->indices = kallocate(sizeof(u32) * lod->total_index_count, MEMORY_TAG_ARRAY);
        }

        // Invalidate the chunk.
        chunk->generation = INVALID_ID_U16;
    }

    // Height data.
    t->vertex_data_length = typed_resource->vertex_data_length;
    t->vertex_datas = kallocate(sizeof(terrain_vertex_data) * t->vertex_data_length, MEMORY_TAG_ARRAY);
    kcopy_memory(t->vertex_datas, typed_resource->vertex_datas, typed_resource->vertex_data_length * sizeof(terrain_vertex_data));

    t->material_count = typed_resource->material_count;
    if (t->material_count) {
        t->material_names = kallocate(sizeof(char*) * t->material_count, MEMORY_TAG_ARRAY);
        kcopy_memory(t->material_names, typed_resource->material_names, sizeof(char*) * t->material_count);
    } else {
        t->material_names = 0;
    }

    // Unload the terrain typed resource.
    resource_system_unload(&terr_resource);

    u32 chunk_row_count = t->tile_count_z / t->chunk_size;
    u32 chunk_col_count = t->tile_count_x / t->chunk_size;

    for (u32 z = 0, i = 0; z < chunk_row_count; z++) {
        for (u32 x = 0; x < chunk_col_count; ++x, ++i) {
            // x/z chunk indices within terrain grid.
            u32 chunk_offset_x = i % chunk_col_count;
            u32 chunk_offset_z = i / chunk_col_count;
            terrain_chunk_calculate_geometry(t, &t->chunks[i], chunk_offset_x, chunk_offset_z);
        }
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

    // Mark it as valid for rendering.
    t->generation++;

    t->state = TERRAIN_STATE_LOADED;
    return true;
}

b8 terrain_chunk_load(terrain* t, terrain_chunk* chunk) {
    // NOTE: Instead of using geometry here, which essentially wraps a single set of vertex and index data,
    // these will be handled manually here for terrains.

    // Upload vertex data.
    renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    u64 total_vertex_size = sizeof(terrain_vertex) * chunk->total_vertex_count;
    if (!renderer_renderbuffer_allocate(vertex_buffer, total_vertex_size, &chunk->vertex_buffer_offset)) {
        KERROR("Failed to allocate memory for terrain chunk vertex data.");
        return false;
    }

    // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
    if (!renderer_renderbuffer_load_range(vertex_buffer, chunk->vertex_buffer_offset, total_vertex_size, chunk->vertices, false)) {
        KERROR("Failed to upload vertex data for terrain chunk.");
        return false;
    }

    // Upload index data for all LODs.
    renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
    for (u32 i = 0; i < t->lod_count; ++i) {
        terrain_chunk_lod* lod = &chunk->lods[i];
        u32 total_size = sizeof(u32) * lod->total_index_count;
        if (!renderer_renderbuffer_allocate(index_buffer, total_size, &lod->index_buffer_offset)) {
            KERROR("Failed to allocate memory for terrain chunk lod index data.");
            return false;
        }

        // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
        if (!renderer_renderbuffer_load_range(index_buffer, lod->index_buffer_offset, total_size, lod->indices, false)) {
            KERROR("Failed to upload index data for terrain chunk lod.");
            return false;
        }
    }

    // Create a terrain material by copying the properties of these materials to a new terrain material.
    char terrain_material_name[MATERIAL_NAME_MAX_LENGTH] = {0};
    string_format_unsafe(terrain_material_name, "terrain_mat_%s", t->name);
    // NOTE: While the terrain could technically hold the material, doing this here lends the ability
    // for each chunk to have a separate material.
    chunk->material = material_system_acquire_terrain_material(terrain_material_name, t->material_count, (const char**)t->material_names, true);
    if (!chunk->material) {
        KWARN("Failed to acquire terrain material. Using defualt instead.");
        chunk->material = material_system_get_default_terrain();
    }

    // Update the generation, making this valid to render.
    chunk->generation++;

    return true;
}

b8 terrain_unload(terrain* t) {
    if (!t) {
        KERROR("terrain_unload requires a valid pointer to a terrain.");
        return false;
    }
    t->state = TERRAIN_STATE_UNDEFINED;

    // Immediately invalidate the terrain.
    t->generation = INVALID_ID;

    b8 has_error = false;
    // Unload all chunks.
    for (u32 i = 0; i < t->chunk_count; ++i) {
        terrain_chunk* chunk = &t->chunks[i];
        if (!terrain_chunk_unload(t, chunk)) {
            KERROR("Failed to unload terrain chunk. See logs for details.");
            has_error = true; // Flag the error, but continue.
        }
    }

    return !has_error;
}

b8 terrain_chunk_unload(terrain* t, terrain_chunk* chunk) {
    if (!t || !chunk) {
        KERROR("terrain_chunk_unload requires valid pointers to terrain and chunk to be unloaded.");
        return false;
    }

    b8 has_error = false;

    // Immediately invalidate the terrain chunk to stop it from being rendered.
    chunk->generation = INVALID_ID_U16;

    // Unload all resources from GPU, but do not destroy vertex data.
    // This will allow for chunks to be unloaded/reloaded at will.

    // Release the material reference.
    material_system_release(chunk->material->name);
    chunk->material = 0;

    if (chunk->vertices) {
        // NOTE: since geometry is not used here, need to release vertex and index data manually.
        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        if (!renderer_renderbuffer_free(vertex_buffer, sizeof(terrain_vertex) * chunk->total_vertex_count, chunk->vertex_buffer_offset)) {
            KERROR("Error freeing vertex data for terrain chunk. See logs for details.");
            has_error = true; // Flag that an error occurred.
        }
    }

    // Release each LOD.
    if (chunk->lods) {
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        for (u32 j = 0; j < t->lod_count; ++j) {
            terrain_chunk_lod* lod = &chunk->lods[j];
            if (lod->indices) {
                if (!renderer_renderbuffer_free(index_buffer, sizeof(u32) * lod->total_index_count, lod->index_buffer_offset)) {
                    KERROR("Error freeing vertex data for terrain chunk, lod level=%u. See logs for details.", j);
                    has_error = true; // Flag that an error occurred.
                }
            }
        }
    }

    return !has_error;
}

b8 terrain_update(terrain* t) {
    return true;
}

// Destroys the given chunk, releasing all host memory. Unloads first if needed.
static void terrain_chunk_destroy(terrain* t, terrain_chunk* chunk) {
    if (!t || !chunk) {
        return;
    }

    // If it is still loaded, unload it first.
    if (chunk->generation != INVALID_ID_U16) {
        if (!terrain_chunk_unload(t, chunk)) {
            // Log, but continue since there's nothing to be done.
            KERROR("Failed to unload terrain chunk before destroying it. See logs for details.");
        }
    }

    // Destroy vertex data.
    if (chunk->vertices) {
        kfree(chunk->vertices, sizeof(terrain_vertex) * chunk->total_vertex_count, MEMORY_TAG_ARRAY);
        chunk->vertex_buffer_offset = 0;
        chunk->total_vertex_count = 0;
    }

    // Destroy each LOD.
    if (chunk->lods) {
        for (u32 j = 0; j < t->lod_count; ++j) {
            terrain_chunk_lod* lod = &chunk->lods[j];
            if (lod->indices) {
                kfree(lod->indices, sizeof(u32) * lod->total_index_count, MEMORY_TAG_ARRAY);
            }
        }
        // Make sure to clean up the lods data as well.
        kfree(chunk->lods, sizeof(terrain_chunk_lod) * t->lod_count, MEMORY_TAG_ARRAY);
        chunk->lods = 0;
    }
}

// Calculates vertex data as well as sets up index data for each LOD for the given chunk.
static void terrain_chunk_calculate_geometry(terrain* t, terrain_chunk* chunk, u32 chunk_offset_x, u32 chunk_offset_z) {
    // The base x/z position of the first vertex within the chunk.
    f32 chunk_base_pos_x = chunk_offset_x * t->chunk_size * t->tile_scale_x;
    f32 chunk_base_pos_z = chunk_offset_z * t->chunk_size * t->tile_scale_z;

    f32 y_min = 99999.0f;
    f32 y_max = -99999.0f;

    // Generate surface data.
    // NOTE: One more row/column at the end so there are chunk_size number of tiles.
    u32 vertex_stride = t->chunk_size + 1;
    for (u32 z = 0, i = 0; z < vertex_stride; ++z) {
        for (u32 x = 0; x < vertex_stride; ++x, ++i) {
            terrain_vertex* v = &chunk->vertices[i];
            v->position.x = chunk_base_pos_x + (x * t->tile_scale_x);
            v->position.z = chunk_base_pos_z + (z * t->tile_scale_z);

            // Get global x/y offset into the terrain tile array.
            // NOTE: Because of the extra row and
            // column of vertices, the first row/column of this chunk must be the
            // same as the previous in that direction.
            i32 globalx = x + (chunk_offset_x * (vertex_stride));
            if (chunk_offset_x > 0) {
                globalx -= chunk_offset_x;
            }
            i32 globalz = z + (chunk_offset_z * (vertex_stride));
            if (chunk_offset_z > 0) {
                globalz -= chunk_offset_z;
            }

            u32 global_terrain_index = globalx + (globalz * (t->tile_count_x + 1));
            if (global_terrain_index < 0) {
                global_terrain_index = 0;
            }

            terrain_vertex_data* vert_data = &t->vertex_datas[global_terrain_index];
            f32 point_height = vert_data->height;

            v->position.y = point_height * t->scale_y;
            y_min = KMIN(y_min, v->position.y);
            y_max = KMAX(y_max, v->position.y);

            v->colour = vec4_one(); // white;
            v->normal = (vec3){0, 1, 0};
            v->texcoord.x = chunk_offset_x + (f32)x;
            v->texcoord.y = chunk_offset_z + (f32)z;

            // NOTE: Assigning default weights based on overall height. Lower material indices are
            // lower in altitude.
            // NOTE: These must overlap the min/max to blend properly.
            v->material_weights[0] = kattenuation_min_max(-0.2f, 0.2f, point_height); // mid 0
            v->material_weights[1] = kattenuation_min_max(0.0f, 0.3f, point_height);  // mid .15
            v->material_weights[2] = kattenuation_min_max(0.15f, 0.9f, point_height); // mid 5
            v->material_weights[3] = kattenuation_min_max(0.5f, 1.2f, point_height);  // mid 9
        }
    }

    // Generate skirt data for each side.
    u32 vvi = chunk->surface_vertex_count;
    // Order is important here: Left, right, top, then bottom.
    for (u8 s = 0; s < TSS_COUNT; ++s) {
        // Left
        for (u32 i = 0; i < vertex_stride; ++i, ++vvi) {
            // Source vertex
            terrain_vertex* sv;
            if (s == TSS_LEFT) {
                sv = &chunk->vertices[i * vertex_stride];
            } else if (s == TSS_RIGHT) {
                sv = &chunk->vertices[(i * vertex_stride) + t->chunk_size];
            } else if (s == TSS_TOP) {
                sv = &chunk->vertices[i];
            } else { // TSS_BOTTOM
                sv = &chunk->vertices[i + (vertex_stride * t->chunk_size)];
            }

            // Target vertex
            terrain_vertex* v = &chunk->vertices[vvi];

            // Copy the source vertex data to the target, then change the height.
            kcopy_memory(v, sv, sizeof(terrain_vertex));
            v->position.y -= 0.1f * t->scale_y;
        }
    }

    // Calculate extents for this chunk.
    // Use the first surface vertex for the min extents.
    chunk->extents.min = chunk->vertices[0].position;
    chunk->extents.min.y = y_min;
    // Use the last surface vertex for the max extents.
    chunk->extents.max = chunk->vertices[chunk->surface_vertex_count - 1].position;
    chunk->extents.max.y = y_max;

    chunk->center = extents_3d_half(chunk->extents);

    // Generate indices for each LOD.
    for (u32 j = 0; j < t->lod_count; ++j) {
        terrain_chunk_lod* lod = &chunk->lods[j];

        // The number of vertices that loops move forward per loop for this LOD.
        u32 lod_skip_rate = (1 << j);

        // Surface indices. Generate 1 set of 6 per tile.
        for (u32 row = 0, i = 0; row < t->chunk_size; row += lod_skip_rate) {
            for (u32 col = 0; col < t->chunk_size; col += lod_skip_rate, i += 6) {
                u32 next_row = row + lod_skip_rate;
                u32 next_col = col + lod_skip_rate;
                u32 v0 = (row * vertex_stride) + col;
                u32 v1 = (row * vertex_stride) + next_col;
                u32 v2 = (next_row * vertex_stride) + col;
                u32 v3 = (next_row * vertex_stride) + next_col;

                lod->indices[i + 0] = v2;
                lod->indices[i + 1] = v1;
                lod->indices[i + 2] = v0;
                lod->indices[i + 3] = v3;
                lod->indices[i + 4] = v1;
                lod->indices[i + 5] = v2;
            }
        }

        // Generate skirt indices starting at the end of the vertex and index arrays.
        u32 ii = lod->surface_index_count;
        u32 vi = chunk->surface_vertex_count;

        // Order is important here: Left, right, top, then bottom.
        for (u8 s = 0; s < TSS_COUNT; ++s) {
            // Iterate vertices at the lod skip rate.
            for (u32 i = 0; i < t->chunk_size; i += lod_skip_rate, ii += 6, vi += lod_skip_rate) {
                // Find the 2 verts along the surface's edge.
                u32 v0, v1;
                if (s == TSS_LEFT) {
                    v0 = i * vertex_stride;
                    v1 = (i + lod_skip_rate) * vertex_stride;
                } else if (s == TSS_RIGHT) {
                    v0 = (i * vertex_stride) + (vertex_stride - 1);
                    v1 = ((i + lod_skip_rate) * vertex_stride) + (vertex_stride - 1);
                } else if (s == TSS_TOP) {
                    v0 = i;
                    v1 = i + lod_skip_rate;
                } else { // Bottom
                    v0 = i + (vertex_stride * t->chunk_size);
                    v1 = (i + lod_skip_rate) + (vertex_stride * t->chunk_size);
                }

                // The other 2 are the verts directly below that.
                u32 v2 = vi;
                u32 v3 = vi + lod_skip_rate;

                if (s == TSS_LEFT || s == TSS_BOTTOM) {
                    // Counter-clockwise for left and bottom.
                    lod->indices[ii + 0] = v0;
                    lod->indices[ii + 1] = v3;
                    lod->indices[ii + 2] = v1;
                    lod->indices[ii + 3] = v0;
                    lod->indices[ii + 4] = v2;
                    lod->indices[ii + 5] = v3;
                } else { // Right, top
                    // Clockwise for right and top.
                    lod->indices[ii + 0] = v0;
                    lod->indices[ii + 1] = v1;
                    lod->indices[ii + 2] = v2;
                    lod->indices[ii + 3] = v1;
                    lod->indices[ii + 4] = v3;
                    lod->indices[ii + 5] = v2;
                }
            }

            // Skip the last vertex since the loop above takes i and i + 1.
            vi++;
        }
    }

    // Only generate based on first LOD, others should naturally interpolate as verts are skipped.
    terrain_geometry_generate_normals(chunk->surface_vertex_count, chunk->vertices, chunk->lods[0].surface_index_count, chunk->lods[0].indices);
    terrain_geometry_generate_tangents(chunk->surface_vertex_count, chunk->vertices, chunk->lods[0].surface_index_count, chunk->lods[0].indices);
}

// FIXME: These should be made more generic and be rolled back into geometry utils in core.
void terrain_geometry_generate_normals(u32 vertex_count, terrain_vertex* vertices, u32 index_count, u32* indices) {
    for (u32 i = 0; i < index_count; i += 3) {
        u32 i0 = indices[i + 0];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];

        vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
        vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

        vec3 normal = vec3_normalized(vec3_cross(edge1, edge2));

        // NOTE: This just generates a face normal. Smoothing out should be done in
        // a separate pass if desired.
        vertices[i0].normal = normal;
        vertices[i1].normal = normal;
        vertices[i2].normal = normal;
    }
}

void terrain_geometry_generate_tangents(u32 vertex_count, terrain_vertex* vertices, u32 index_count, u32* indices) {
    for (u32 i = 0; i < index_count; i += 3) {
        u32 i0 = indices[i + 0];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];

        vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
        vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

        f32 deltaU1 = vertices[i1].texcoord.x - vertices[i0].texcoord.x;
        f32 deltaV1 = vertices[i1].texcoord.y - vertices[i0].texcoord.y;

        f32 deltaU2 = vertices[i2].texcoord.x - vertices[i0].texcoord.x;
        f32 deltaV2 = vertices[i2].texcoord.y - vertices[i0].texcoord.y;

        f32 dividend = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
        f32 fc = 1.0f / dividend;

        vec3 tangent = (vec3){(fc * (deltaV2 * edge1.x - deltaV1 * edge2.x)),
                              (fc * (deltaV2 * edge1.y - deltaV1 * edge2.y)),
                              (fc * (deltaV2 * edge1.z - deltaV1 * edge2.z))};

        tangent = vec3_normalized(tangent);

        f32 sx = deltaU1, sy = deltaU2;
        f32 tx = deltaV1, ty = deltaV2;
        f32 handedness = ((tx * sy - ty * sx) < 0.0f) ? -1.0f : 1.0f;

        vec4 t4 = vec4_from_vec3(vec3_mul_scalar(tangent, handedness), 0.0f);
        vertices[i0].tangent = t4;
        vertices[i1].tangent = t4;
        vertices[i2].tangent = t4;
    }
}
