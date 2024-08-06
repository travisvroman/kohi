#include "obj_serializer.h"
#include "assets/kasset_types.h"

#include <containers/darray.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <strings/kstring.h>

#include <stdio.h> // sscanf

typedef struct mesh_vertex_index_data {
    u32 position_index;
    u32 normal_index;
    u32 texcoord_index;
} mesh_vertex_index_data;

typedef struct mesh_face_data {
    mesh_vertex_index_data vertices[3];
} mesh_face_data;

typedef struct mesh_group_data {
    // darray
    mesh_face_data* faces;
} mesh_group_data;

static void process_subobject(
    vec3* positions,
    vec3* normals,
    vec2* tex_coords,
    mesh_face_data* faces,
    obj_source_geometry* out_data);

b8 obj_serializer_serialize(const obj_source_asset* out_source_asset, const char** out_file_text) {
    KASSERT_MSG(false, "Not yet implemented");
    return false;
}

b8 obj_serializer_deserialize(const char* obj_file_text, obj_source_asset* out_source_asset) {
    if (!obj_file_text || !out_source_asset) {
        KERROR("obj_serializer_deserialize requires valid pointers to obj_file_text and out_source_asset.");
        return false;
    }

    // Positions
    vec3* positions = darray_reserve(vec3, 16384);

    // Normals
    vec3* normals = darray_reserve(vec3, 16384);

    // Texture coordinates
    vec2* tex_coords = darray_reserve(vec2, 16384);

    // Groups
    mesh_group_data* groups = darray_reserve(mesh_group_data, 4);

    obj_source_geometry* geometries_darray = darray_create(obj_source_geometry);

    char material_file_name[512] = "";

    char name[512];
    kzero_memory(name, sizeof(char) * 512);
    u8 current_mat_name_count = 0;
    char material_names[32][64];

    char line_buf[512] = "";
    char* p = &line_buf[0];
    u32 line_length = 0;

    // index 0 is previous, 1 is previous before that.
    char prev_first_chars[2] = {0, 0};
    u32 start_from = 0;
    while (true) {
        start_from += line_length; // TODO: might need +1 for \n?
        if (!string_line_get(obj_file_text, 511, start_from, &p, &line_length)) {
            /* if (!filesystem_read_line(obj_file, 511, &p, &line_length)) { */
            break;
        }

        // Skip blank lines.
        if (line_length < 1) {
            continue;
        }

        char first_char = line_buf[0];

        switch (first_char) {
        case '#':
            // Skip comments
            continue;
        case 'v': {
            char second_char = line_buf[1];
            switch (second_char) {
            case ' ': {
                // Vertex position
                vec3 pos;
                char t[2];
                sscanf(line_buf, "%s %f %f %f", t, &pos.x, &pos.y, &pos.z);

                darray_push(positions, pos);
            } break;
            case 'n': {
                // Vertex normal
                vec3 norm;
                char t[2];
                sscanf(line_buf, "%s %f %f %f", t, &norm.x, &norm.y, &norm.z);

                darray_push(normals, norm);
            } break;
            case 't': {
                // Vertex texture coords.
                vec2 tex_coord;
                char t[2];

                // NOTE: Ignoring Z if present.
                sscanf(line_buf, "%s %f %f", t, &tex_coord.x, &tex_coord.y);

                darray_push(tex_coords, tex_coord);
            } break;
            }
        } break;
        case 's': {
        } break;
        case 'f': {
            // face
            // f 1/1/1 2/2/2 3/3/3  = pos/tex/norm pos/tex/norm pos/tex/norm
            mesh_face_data face;
            char t[2];

            u64 normal_count = darray_length(normals);
            u64 tex_coord_count = darray_length(tex_coords);

            if (normal_count == 0 || tex_coord_count == 0) {
                sscanf(line_buf, "%s %d %d %d", t, &face.vertices[0].position_index,
                       &face.vertices[1].position_index,
                       &face.vertices[2].position_index);
            } else {
                sscanf(line_buf, "%s %d/%d/%d %d/%d/%d %d/%d/%d", t,
                       &face.vertices[0].position_index,
                       &face.vertices[0].texcoord_index, &face.vertices[0].normal_index,

                       &face.vertices[1].position_index,
                       &face.vertices[1].texcoord_index, &face.vertices[1].normal_index,

                       &face.vertices[2].position_index,
                       &face.vertices[2].texcoord_index,
                       &face.vertices[2].normal_index);
            }
            u64 group_index = darray_length(groups) - 1;
            darray_push(groups[group_index].faces, face);
        } break;
        case 'm': {
            // Material library file.
            char substr[7];

            sscanf(line_buf, "%s %s", substr, material_file_name);

            // If found, save off the material file name.
            if (strings_nequali(substr, "mtllib", 6)) {
                // TODO: verification
                out_source_asset->material_file_name = string_duplicate(material_file_name);
            }
        } break;
        case 'u': {
            // Any time there is a usemtl, assume a new group.
            // New named group or smoothing group, all faces coming after should be
            // added to it.
            mesh_group_data new_group;
            new_group.faces = darray_reserve(mesh_face_data, 16384);
            darray_push(groups, new_group);

            // usemtl
            // Read the material name.
            char t[8];
            sscanf(line_buf, "%s %s", t, material_names[current_mat_name_count]);
            current_mat_name_count++;
        } break;
        case 'g': {
            u64 group_count = darray_length(groups);
            // Process each group as a subobject.
            for (u64 i = 0; i < group_count; ++i) {
                obj_source_geometry new_data = {};
                if (i == 0) {
                    new_data.name = string_duplicate(name);
                } else if (i > 0) {
                    new_data.name = string_format("%s%u", name, i);
                }
                new_data.material_asset_name = string_duplicate(material_names[i]);

                process_subobject(positions, normals, tex_coords, groups[i].faces, &new_data);
                new_data.vertex_count = darray_length(new_data.vertices);
                new_data.index_count = darray_length(new_data.indices);

                darray_push(geometries_darray, new_data);

                // Increment the number of objects.
                darray_destroy(groups[i].faces);
                kzero_memory(material_names[i], 64);
            }

            current_mat_name_count = 0;
            darray_clear(groups);
            kzero_memory(name, 512);

            // Read the name
            char t[2];
            sscanf(line_buf, "%s %s", t, name);

        } break;
        }

        prev_first_chars[1] = prev_first_chars[0];
        prev_first_chars[0] = first_char;
    } // each line

    // Process the remaining group since the last one will not have been trigged
    // by the finding of a new name.
    // Process each group as a subobject.
    u64 group_count = darray_length(groups);
    for (u64 i = 0; i < group_count; ++i) {
        obj_source_geometry new_data = {};
        if (i == 0) {
            new_data.name = string_duplicate(name);
        } else if (i > 0) {
            new_data.name = string_format("%s%u", new_data.name, i);
        }

        new_data.material_asset_name = string_duplicate(material_names[i]);

        process_subobject(positions, normals, tex_coords, groups[i].faces, &new_data);
        new_data.vertex_count = darray_length(new_data.vertices);
        new_data.index_count = darray_length(new_data.indices);

        darray_push(geometries_darray, new_data);

        // Increment the number of objects.
        darray_destroy(groups[i].faces);
    }

    // Cleanup
    darray_destroy(groups);
    darray_destroy(positions);
    darray_destroy(normals);
    darray_destroy(tex_coords);

    // De-duplicate geometry
    u32 count = darray_length(geometries_darray);
    for (u64 i = 0; i < count; ++i) {
        obj_source_geometry* g = &((geometries_darray)[i]);
        KDEBUG("Geometry de-duplication process starting on geometry object named '%s'...", g->name);

        u32 new_vert_count = 0;
        vertex_3d* unique_verts = 0;
        geometry_deduplicate_vertices(
            g->vertex_count,
            g->vertices,
            g->index_count,
            g->indices,
            &new_vert_count,
            &unique_verts);

        // Destroy the old, large array...
        darray_destroy(g->vertices);

        // And replace with the de-duplicated one.
        g->vertices = unique_verts;
        g->vertex_count = new_vert_count;

        // Take a copy of the indices as a normal, non-darray
        u32* indices = kallocate(sizeof(u32) * g->index_count, MEMORY_TAG_ARRAY);
        kcopy_memory(indices, g->indices, sizeof(u32) * g->index_count);
        // Destroy the darray
        darray_destroy(g->indices);
        // Replace with the non-darray version.
        g->indices = indices;

        // Also generate tangents here, this way tangents are also stored in the output file.
        geometry_generate_tangents(g->vertex_count, g->vertices, g->index_count, g->indices);
    }

    // Take a copy of the array since the output doesn't need to be a darray.
    out_source_asset->geometry_count = darray_length(geometries_darray);
    out_source_asset->geometries = kallocate(sizeof(obj_source_geometry) * out_source_asset->geometry_count, MEMORY_TAG_ARRAY);
    kcopy_memory(out_source_asset->geometries, geometries_darray, sizeof(obj_source_geometry) * out_source_asset->geometry_count);
    darray_destroy(geometries_darray);

    return true;
}

static void process_subobject(
    vec3* positions,
    vec3* normals,
    vec2* tex_coords,
    mesh_face_data* faces,
    obj_source_geometry* out_data) {
    out_data->indices = darray_create(u32);
    out_data->vertices = darray_create(vertex_3d);
    b8 extent_set = false;
    kzero_memory(&out_data->extents.min, sizeof(vec3));
    kzero_memory(&out_data->extents.max, sizeof(vec3));

    u64 face_count = darray_length(faces);
    u64 normal_count = darray_length(normals);
    u64 tex_coord_count = darray_length(tex_coords);

    b8 skip_normals = false;
    b8 skip_tex_coords = false;
    if (normal_count == 0) {
        KWARN("No normals are present in this model.");
        skip_normals = true;
    }
    if (tex_coord_count == 0) {
        KWARN("No texture coordinates are present in this model.");
        skip_tex_coords = true;
    }
    for (u64 f = 0; f < face_count; ++f) {
        mesh_face_data face = faces[f];

        // Each vertex
        for (u64 i = 0; i < 3; ++i) {
            mesh_vertex_index_data index_data = face.vertices[i];
            darray_push(out_data->indices, (u32)(i + (f * 3)));

            vertex_3d vert;

            vec3 pos = positions[index_data.position_index - 1];
            vert.position = pos;

            // Check extents - min
            if (pos.x < out_data->extents.min.x || !extent_set) {
                out_data->extents.min.x = pos.x;
            }
            if (pos.y < out_data->extents.min.y || !extent_set) {
                out_data->extents.min.y = pos.y;
            }
            if (pos.z < out_data->extents.min.z || !extent_set) {
                out_data->extents.min.z = pos.z;
            }

            // Check extents - max
            if (pos.x > out_data->extents.max.x || !extent_set) {
                out_data->extents.max.x = pos.x;
            }
            if (pos.y > out_data->extents.max.y || !extent_set) {
                out_data->extents.max.y = pos.y;
            }
            if (pos.z > out_data->extents.max.z || !extent_set) {
                out_data->extents.max.z = pos.z;
            }

            extent_set = true;

            if (skip_normals) {
                vert.normal = vec3_create(0, 0, 1);
            } else {
                vert.normal = normals[index_data.normal_index - 1];
            }

            if (skip_tex_coords) {
                vert.texcoord = vec2_zero();
            } else {
                vert.texcoord = tex_coords[index_data.texcoord_index - 1];
            }

            // TODO: Color. Hardcode to white for now.
            vert.colour = vec4_one();

            darray_push(out_data->vertices, vert);
        }
    }

    // Calculate the center based on the extents.
    for (u8 i = 0; i < 3; ++i) {
        out_data->center.elements[i] = (out_data->extents.min.elements[i] +
                                        out_data->extents.max.elements[i]) /
                                       2.0f;
    }
}
