#include "mesh_loader.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "containers/darray.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "math/kmath.h"
#include "math/geometry_utils.h"
#include "loader_utils.h"
#include "geometry_system.h"

#include "platform/filesystem.h"

typedef enum mesh_file_type {
    MESH_FILE_TYPE_NOT_FOUND,
    MESH_FILE_TYPE_OBJ
} mesh_file_type;

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

b8 process_obj_file(file_handle* obj_file, geometry_config* out_geometries_darray);
void process_subobject(vec3* positions, vec3* normals, vec2* tex_coords, mesh_face_data* faces, geometry_config* out_data);

b8 mesh_loader_load(struct resource_loader* self, const char* name, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    file_handle f;
    // Supported extensions
    char* extensions[1] = {".obj"};

    char full_file_path[512];
    mesh_file_type type = MESH_FILE_TYPE_NOT_FOUND;
    // Try each supported extension.
    for (u32 i = 0; i < 1; + i) {
        string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, extensions[i]);
        if (filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
            type = MESH_FILE_TYPE_OBJ;
            break;
        }
    }

    if (type == MESH_FILE_TYPE_NOT_FOUND) {
        KERROR("Unable to find mesh of supported type called '%s'.", name);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    // The resource data is just an array of configs.
    geometry_config* resource_data = darray_create(geometry_config);

    b8 result = false;
    switch (type) {
        case MESH_FILE_TYPE_OBJ:
            result = process_obj_file(&f, resource_data);
            break;
    }

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = darray_stride(resource_data) * darray_length(resource_data);

    return true;
}

void mesh_loader_unload(struct resource_loader* self, resource* resource) {
    u32 count = darray_length(resource->data);
    for (u32 i = 0; i < count; ++i) {
        geometry_config* config = &((geometry_config*)resource->data)[i];
        geometry_system_config_dispose(config);
    }
    darray_destroy(resource->data);
    resource->data = 0;
    resource->data_size = 0;
}

b8 process_obj_file(file_handle* obj_file, geometry_config* out_geometries_darray) {
    // Positions
    vec3* positions = darray_reserve(vec3, 16384);

    // Normals
    vec3* normals = darray_reserve(vec3, 16384);

    // Normals
    vec2* tex_coords = darray_reserve(vec2, 16384);

    // Faces
    mesh_group_data* groups = darray_reserve(mesh_group_data, 4);

    char material_file_name[512] = "";
    // b8 hit_name = false;

    char name[512];
    u8 current_mat_name_count = 0;
    char material_names[32][64];

    char line_buf[512] = "";
    char* p = &line_buf[0];
    i64 line_length = 0;

    // index 0 is previous, 1 is previous before that.
    char prev_first_chars[2] = {0, 0};
    while (line_length > -1) {
        if (!filesystem_read_line(&obj_file, 511, &p, &line_length)) {
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
                        sscanf(
                            line_buf,
                            "%s %f %f %f",
                            t,
                            &pos.x,
                            &pos.y,
                            &pos.z);

                        darray_push(positions, pos);
                    } break;
                    case 'n': {
                        // Vertex normal
                        vec3 norm;
                        char t[2];
                        sscanf(
                            line_buf,
                            "%s %f %f %f",
                            t,
                            &norm.x,
                            &norm.y,
                            &norm.z);

                        darray_push(normals, norm);
                    } break;
                    case 't': {
                        // Vertex texture coords.
                        vec2 tex_coord;
                        char t[2];

                        // NOTE: Ignoring Z if present.
                        sscanf(
                            line_buf,
                            "%s %f %f",
                            t,
                            &tex_coord.x,
                            &tex_coord.y);

                        darray_push(tex_coords, tex_coord);
                    } break;
                }
            } break;
            // case 'g':
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
                    sscanf(
                        line_buf,
                        "%s %d %d %d",
                        t,
                        &face.vertices[0].position_index,
                        &face.vertices[1].position_index,
                        &face.vertices[2].position_index);
                } else {
                    sscanf(
                        line_buf,
                        "%s %d/%d/%d %d/%d/%d %d/%d/%d",
                        t,
                        &face.vertices[0].position_index,
                        &face.vertices[0].texcoord_index,
                        &face.vertices[0].normal_index,

                        &face.vertices[1].position_index,
                        &face.vertices[1].texcoord_index,
                        &face.vertices[1].normal_index,

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

                sscanf(
                    line_buf,
                    "%s %s",
                    substr,
                    material_file_name);

                // If found, save off the material file name.
                if (strings_nequali(substr, "mtllib", 6)) {
                    // TODO: verification
                }
            } break;
            case 'g': {
                // case 'o': {
                //  New object. process the previous object first if we previously read anything in. This will only be true after the first object..
                // if (hit_name) {
                u64 group_count = darray_length(groups);

                // Process each group as a subobject.
                for (u64 i = 0; i < group_count; ++i) {
                    geometry_config new_data = {};
                    string_ncopy(new_data.name, name, 255);
                    if (i > 0) {
                        string_append_int(new_data.name, new_data.name, i);
                    }
                    string_ncopy(new_data.material_name, material_names[i], 255);

                    process_subobject(positions, normals, tex_coords, groups[i].faces, &new_data);

                    darray_push(out_geometries_darray, new_data);

                    // Increment the number of objects.
                    darray_destroy(groups[i].faces);
                    kzero_memory(material_names[i], 64);
                }
                current_mat_name_count = 0;
                darray_clear(groups);
                kzero_memory(name, 512);
                //}

                // hit_name = true;

                // Read the name
                char t[2];
                sscanf(line_buf, "%s %s", t, name);

            } break;
            case 'u': {
                // Any time there is a usemtl, assume a new group.
                // New named group or smoothing group, all faces coming after should be added to it.
                mesh_group_data new_group;
                new_group.faces = darray_reserve(mesh_face_data, 16384);
                darray_push(groups, new_group);

                // usemtl
                // Read the material name.
                char t[8];
                sscanf(line_buf, "%s %s", t, material_names[current_mat_name_count]);
                current_mat_name_count++;

            } break;
        }
        prev_first_chars[1] = prev_first_chars[0];
        prev_first_chars[0] = first_char;
    }  // each line

    // Process the remaining group since the last one will not have been trigged
    // by the finding of a new name.
    // Process each group as a subobject.
    u64 group_count = darray_length(groups);
    for (u64 i = 0; i < group_count; ++i) {
        geometry_config new_data = {};
        string_ncopy(new_data.name, name, 255);
        if (i > 0) {
            string_append_int(new_data.name, new_data.name, i);
        }
        string_ncopy(new_data.material_name, material_names[i], 255);

        process_subobject(positions, normals, tex_coords, groups[i].faces, &new_data);
        darray_push(out_geometries_darray, new_data);

        // Increment the number of objects.
        darray_destroy(groups[i].faces);
    }

    darray_destroy(groups);
    darray_destroy(positions);
    darray_destroy(normals);
    darray_destroy(tex_coords);

    filesystem_close(&obj_file);

    if (string_length(material_file_name) > 0) {
        // Load up the material file
        char full_mtl_path[512];
        string_directory_from_path(full_mtl_path, full_file_path);
        string_append_string(full_mtl_path, full_mtl_path, material_file_name);

        if (!obj_mtl_loader_process_file(full_mtl_path, material_configs)) {
            KERROR("Error reading obj mtl file.");
        }

        // material_config* configs = darray_create(material_config);

        // // If materials were returned, create them.
        // if (obj_mtl_loader_process_file(full_mtl_path, &configs)) {
        //     u64 material_count = darray_length(configs);
        //     for (u64 i = 0; i < material_count; ++i) {
        //         material_manager_register_material(configs[i]);
        //     }
        // }
    }

    // De-duplicate geometry
    u32 count = darray_length(out_geometries_darray);
    for (u64 i = 0; i < count; ++i) {
        geometry_config* g = &out_geometries_darray[i];
        
        u32 new_vert_count = 0;
        vertex_3d* unique_verts = 0;
        geometry_deduplicate_vertices(g->vertex_count, g->vertices, g->index_count, g->indices, &new_vert_count, &unique_verts);


    }

    return true;
}

void process_subobject(vec3* positions, vec3* normals, vec2* tex_coords, mesh_face_data* faces, geometry_config* out_data) {
    out_data->indices = darray_create(u32);
    out_data->vertices = darray_create(vertex_3d);
    kzero_memory(&out_data->min_extents, sizeof(vec3));
    kzero_memory(&out_data->max_extents, sizeof(vec3));

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
            if (pos.x < out_data->min_extents.x) {
                out_data->min_extents.x = pos.x;
            }
            if (pos.y < out_data->min_extents.y) {
                out_data->min_extents.y = pos.y;
            }
            if (pos.z < out_data->min_extents.z) {
                out_data->min_extents.z = pos.z;
            }

            // Check extents - max
            if (pos.x > out_data->max_extents.x) {
                out_data->max_extents.x = pos.x;
            }
            if (pos.y > out_data->max_extents.y) {
                out_data->max_extents.y = pos.y;
            }
            if (pos.z > out_data->max_extents.z) {
                out_data->max_extents.z = pos.z;
            }

            if (skip_normals) {
                vert.normal = vec3_zero();
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

    // Calculate tangents.
    geometry_generate_tangents(out_data->vertex_count, out_data->vertices, out_data->index_count, out_data->indices);
}

resource_loader material_resource_loader_create() {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_MESH;
    loader.custom_type = 0;
    loader.load = mesh_loader_load;
    loader.unload = mesh_loader_unload;
    loader.type_path = "materials";

    return loader;
}