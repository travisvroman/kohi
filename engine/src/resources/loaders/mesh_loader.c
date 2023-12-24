#include "mesh_loader.h"

#include <stdio.h>  //sscanf

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "loader_utils.h"
#include "math/geometry_utils.h"
#include "math/kmath.h"
#include "platform/filesystem.h"
#include "resources/resource_types.h"
#include "systems/geometry_system.h"
#include "systems/resource_system.h"

typedef enum mesh_file_type {
    MESH_FILE_TYPE_NOT_FOUND,
    MESH_FILE_TYPE_KSM,
    MESH_FILE_TYPE_OBJ
} mesh_file_type;

typedef struct supported_mesh_filetype {
    char *extension;
    mesh_file_type type;
    b8 is_binary;
} supported_mesh_filetype;

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
    mesh_face_data *faces;
} mesh_group_data;

static b8 import_obj_file(file_handle *obj_file, const char *out_ksm_filename,
                          geometry_config **out_geometries_darray);
static void process_subobject(vec3 *positions, vec3 *normals, vec2 *tex_coords,
                              mesh_face_data *faces, geometry_config *out_data);
static b8 import_obj_material_library_file(const char *mtl_file_path);

static b8 load_ksm_file(file_handle *ksm_file,
                        geometry_config **out_geometries_darray);
static b8 write_ksm_file(const char *path, const char *name, u32 geometry_count,
                         geometry_config *geometries);
static b8 write_kmt_file(const char *directory, material_config *config);

static b8 mesh_loader_load(struct resource_loader *self, const char *name,
                           void *params, resource *out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char *format_str = "%s/%s/%s%s";
    file_handle f;
    // Supported extensions. Note that these are in order of priority when looked
    // up. This is to prioritize the loading of a binary version of the mesh,
    // followed by importing various types of meshes to binary types, which would
    // be loaded on the next run.
    // TODO: Might be good to be able to specify an override to always import
    // (i.e. skip binary versions) for debug purposes.
#define SUPPORTED_FILETYPE_COUNT 2
    supported_mesh_filetype supported_filetypes[SUPPORTED_FILETYPE_COUNT];
    supported_filetypes[0] =
        (supported_mesh_filetype){".ksm", MESH_FILE_TYPE_KSM, true};
    supported_filetypes[1] =
        (supported_mesh_filetype){".obj", MESH_FILE_TYPE_OBJ, false};

    char full_file_path[512];
    mesh_file_type type = MESH_FILE_TYPE_NOT_FOUND;
    // Try each supported extension.
    for (u32 i = 0; i < SUPPORTED_FILETYPE_COUNT; ++i) {
        string_format(full_file_path, format_str, resource_system_base_path(),
                      self->type_path, name, supported_filetypes[i].extension);
        // If the file exists, open it and stop looking.
        if (filesystem_exists(full_file_path)) {
            if (filesystem_open(full_file_path, FILE_MODE_READ,
                                supported_filetypes[i].is_binary, &f)) {
                type = supported_filetypes[i].type;
                break;
            }
        }
    }

    if (type == MESH_FILE_TYPE_NOT_FOUND) {
        KERROR("Unable to find mesh of supported type called '%s'.", name);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    // The resource data is just an array of configs.
    geometry_config *resource_data = darray_create(geometry_config);

    b8 result = false;
    switch (type) {
        case MESH_FILE_TYPE_OBJ: {
            // Generate the ksm filename.
            char ksm_file_name[512];
            string_format(ksm_file_name, "%s/%s/%s%s", resource_system_base_path(),
                          self->type_path, name, ".ksm");
            result = import_obj_file(&f, ksm_file_name, &resource_data);
            break;
        }
        case MESH_FILE_TYPE_KSM:
            result = load_ksm_file(&f, &resource_data);
            break;
        default:
        case MESH_FILE_TYPE_NOT_FOUND:
            KERROR("Unable to find mesh of supported type called '%s'.", name);
            result = false;
            break;
    }

    filesystem_close(&f);

    if (!result) {
        KERROR("Failed to process mesh file '%s'.", full_file_path);
        darray_destroy(resource_data);
        out_resource->data = 0;
        out_resource->data_size = 0;
        return false;
    }

    out_resource->data = resource_data;
    // Use the data size as a count.
    out_resource->data_size = darray_length(resource_data);

    return true;
}

static void mesh_loader_unload(struct resource_loader *self,
                               resource *resource) {
    u32 count = darray_length(resource->data);
    for (u32 i = 0; i < count; ++i) {
        geometry_config *config = &((geometry_config *)resource->data)[i];
        geometry_system_config_dispose(config);
    }
    darray_destroy(resource->data);
    resource->data = 0;
    resource->data_size = 0;
}

static b8 load_ksm_file(file_handle *ksm_file,
                        geometry_config **out_geometries_darray) {
    // Version
    u64 bytes_read = 0;
    u16 version = 0;
    filesystem_read(ksm_file, sizeof(u16), &version, &bytes_read);

    // Name length
    u32 name_length = 0;
    filesystem_read(ksm_file, sizeof(u32), &name_length, &bytes_read);
    // Name + terminator
    char name[256];
    filesystem_read(ksm_file, sizeof(char) * name_length, name, &bytes_read);

    // Geometry count
    u32 geometry_count = 0;
    filesystem_read(ksm_file, sizeof(u32), &geometry_count, &bytes_read);

    // Each geometry
    for (u32 i = 0; i < geometry_count; ++i) {
        geometry_config g = {};

        // Vertices (size/count/array)
        filesystem_read(ksm_file, sizeof(u32), &g.vertex_size, &bytes_read);
        filesystem_read(ksm_file, sizeof(u32), &g.vertex_count, &bytes_read);
        g.vertices = kallocate(g.vertex_size * g.vertex_count, MEMORY_TAG_ARRAY);
        filesystem_read(ksm_file, g.vertex_size * g.vertex_count, g.vertices,
                        &bytes_read);

        // Indices (size/count/array)
        filesystem_read(ksm_file, sizeof(u32), &g.index_size, &bytes_read);
        filesystem_read(ksm_file, sizeof(u32), &g.index_count, &bytes_read);
        g.indices = kallocate(g.index_size * g.index_count, MEMORY_TAG_ARRAY);
        filesystem_read(ksm_file, g.index_size * g.index_count, g.indices,
                        &bytes_read);

        // Name
        u32 g_name_length = 0;
        filesystem_read(ksm_file, sizeof(u32), &g_name_length, &bytes_read);
        filesystem_read(ksm_file, sizeof(char) * g_name_length, g.name,
                        &bytes_read);

        // Material Name
        u32 m_name_length = 0;
        filesystem_read(ksm_file, sizeof(u32), &m_name_length, &bytes_read);
        filesystem_read(ksm_file, sizeof(char) * m_name_length, g.material_name,
                        &bytes_read);

        // Handles backward compatability for
        // https://github.com/travisvroman/kohi/issues/130
        u64 extent_size = sizeof(vec3);
        if (version == 0x0001U) {
            extent_size = sizeof(vertex_3d);
        }

        // Center
        filesystem_read(ksm_file, extent_size, &g.center, &bytes_read);

        // Extents (min/max)
        filesystem_read(ksm_file, extent_size, &g.min_extents, &bytes_read);
        filesystem_read(ksm_file, extent_size, &g.max_extents, &bytes_read);

        // Add to the output array.
        darray_push(*out_geometries_darray, g);
    }

    filesystem_close(ksm_file);

    return true;
}

static b8 write_ksm_file(const char *path, const char *name, u32 geometry_count,
                         geometry_config *geometries) {
    if (filesystem_exists(path)) {
        KINFO("File '%s' already exists and will be overwritten.", path);
    }

    file_handle f;
    if (!filesystem_open(path, FILE_MODE_WRITE, true, &f)) {
        KERROR("Unable to open file '%s' for writing. KSM write failed.", path);
        return false;
    }

    // Version
    u64 written = 0;
    u16 version = 0x0002U;
    filesystem_write(&f, sizeof(u16), &version, &written);

    // Name length
    u32 name_length = string_length(name) + 1;
    filesystem_write(&f, sizeof(u32), &name_length, &written);
    // Name + terminator
    filesystem_write(&f, sizeof(char) * name_length, name, &written);

    // Geometry count
    filesystem_write(&f, sizeof(u32), &geometry_count, &written);

    // Each geometry
    for (u32 i = 0; i < geometry_count; ++i) {
        geometry_config *g = &geometries[i];

        // Vertices (size/count/array)
        filesystem_write(&f, sizeof(u32), &g->vertex_size, &written);
        filesystem_write(&f, sizeof(u32), &g->vertex_count, &written);
        filesystem_write(&f, g->vertex_size * g->vertex_count, g->vertices,
                         &written);

        // Indices (size/count/array)
        filesystem_write(&f, sizeof(u32), &g->index_size, &written);
        filesystem_write(&f, sizeof(u32), &g->index_count, &written);
        filesystem_write(&f, g->index_size * g->index_count, g->indices, &written);

        // Name
        u32 g_name_length = string_length(g->name) + 1;
        filesystem_write(&f, sizeof(u32), &g_name_length, &written);
        filesystem_write(&f, sizeof(char) * g_name_length, g->name, &written);

        // Material Name
        u32 m_name_length = string_length(g->material_name) + 1;
        filesystem_write(&f, sizeof(u32), &m_name_length, &written);
        filesystem_write(&f, sizeof(char) * m_name_length, g->material_name,
                         &written);

        // Center
        filesystem_write(&f, sizeof(vec3), &g->center, &written);

        // Extents (min/max)
        filesystem_write(&f, sizeof(vec3), &g->min_extents, &written);
        filesystem_write(&f, sizeof(vec3), &g->max_extents, &written);
    }

    filesystem_close(&f);

    return true;
}

/**
 * @brief Imports an obj file. This reads the obj, creates geometry configs,
 * then calls logic to write those geometries out to a binary ksm file. That
 * file can be used on the next load.
 *
 * @param obj_file A pointer to the obj file handle to be read.
 * @param out_ksm_filename The path to the ksm file to be written to.
 * @param out_geometries_darray A darray of geometries parsed from the file.
 * @return True on success; otherwise false.
 */
static b8 import_obj_file(file_handle *obj_file, const char *out_ksm_filename,
                          geometry_config **out_geometries_darray) {
    // Positions
    vec3 *positions = darray_reserve(vec3, 16384);

    // Normals
    vec3 *normals = darray_reserve(vec3, 16384);

    // Normals
    vec2 *tex_coords = darray_reserve(vec2, 16384);

    // Groups
    mesh_group_data *groups = darray_reserve(mesh_group_data, 4);

    char material_file_name[512] = "";

    char name[512];
    kzero_memory(name, sizeof(char) * 512);
    u8 current_mat_name_count = 0;
    char material_names[32][64];

    char line_buf[512] = "";
    char *p = &line_buf[0];
    u64 line_length = 0;

    // index 0 is previous, 1 is previous before that.
    char prev_first_chars[2] = {0, 0};
    while (true) {
        if (!filesystem_read_line(obj_file, 511, &p, &line_length)) {
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
                    geometry_config new_data = {};
                    string_ncopy(new_data.name, name, 255);
                    if (i > 0) {
                        string_append_int(new_data.name, new_data.name, i);
                    }
                    string_ncopy(new_data.material_name, material_names[i], 255);

                    process_subobject(positions, normals, tex_coords, groups[i].faces,
                                      &new_data);
                    new_data.vertex_count = darray_length(new_data.vertices);
                    new_data.vertex_size = sizeof(vertex_3d);
                    new_data.index_count = darray_length(new_data.indices);
                    new_data.index_size = sizeof(u32);

                    darray_push(*out_geometries_darray, new_data);

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

        process_subobject(positions, normals, tex_coords, groups[i].faces,
                          &new_data);
        new_data.vertex_count = darray_length(new_data.vertices);
        new_data.vertex_size = sizeof(vertex_3d);
        new_data.index_count = darray_length(new_data.indices);
        new_data.index_size = sizeof(u32);

        darray_push(*out_geometries_darray, new_data);

        // Increment the number of objects.
        darray_destroy(groups[i].faces);
    }

    darray_destroy(groups);
    darray_destroy(positions);
    darray_destroy(normals);
    darray_destroy(tex_coords);

    if (string_length(material_file_name) > 0) {
        // Load up the material file
        char full_mtl_path[512];
        kzero_memory(full_mtl_path, sizeof(char) * 512);
        string_directory_from_path(full_mtl_path, out_ksm_filename);
        string_append_string(full_mtl_path, full_mtl_path, material_file_name);

        // Process material library file.
        if (!import_obj_material_library_file(full_mtl_path)) {
            KERROR("Error reading obj mtl file.");
        }
    }

    // De-duplicate geometry
    u32 count = darray_length(*out_geometries_darray);
    for (u64 i = 0; i < count; ++i) {
        geometry_config *g = &((*out_geometries_darray)[i]);
        KDEBUG(
            "Geometry de-duplication process starting on geometry object named "
            "'%s'...",
            g->name);

        u32 new_vert_count = 0;
        vertex_3d *unique_verts = 0;
        geometry_deduplicate_vertices(g->vertex_count, g->vertices, g->index_count,
                                      g->indices, &new_vert_count, &unique_verts);

        // Destroy the old, large array...
        darray_destroy(g->vertices);

        // And replace with the de-duplicated one.
        g->vertices = unique_verts;
        g->vertex_count = new_vert_count;

        // Take a copy of the indices as a normal, non-darray
        u32 *indices = kallocate(sizeof(u32) * g->index_count, MEMORY_TAG_ARRAY);
        kcopy_memory(indices, g->indices, sizeof(u32) * g->index_count);
        // Destroy the darray
        darray_destroy(g->indices);
        // Replace with the non-darray version.
        g->indices = indices;

        // Also generate tangents here, this way tangents are also stored in the
        // output file.
        geometry_generate_tangents(g->vertex_count, g->vertices, g->index_count,
                                   g->indices);
    }

    // Output a ksm file, which will be loaded in the future.
    return write_ksm_file(out_ksm_filename, name, count, *out_geometries_darray);
}

static void process_subobject(vec3 *positions, vec3 *normals, vec2 *tex_coords,
                              mesh_face_data *faces,
                              geometry_config *out_data) {
    out_data->indices = darray_create(u32);
    out_data->vertices = darray_create(vertex_3d);
    b8 extent_set = false;
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
            if (pos.x < out_data->min_extents.x || !extent_set) {
                out_data->min_extents.x = pos.x;
            }
            if (pos.y < out_data->min_extents.y || !extent_set) {
                out_data->min_extents.y = pos.y;
            }
            if (pos.z < out_data->min_extents.z || !extent_set) {
                out_data->min_extents.z = pos.z;
            }

            // Check extents - max
            if (pos.x > out_data->max_extents.x || !extent_set) {
                out_data->max_extents.x = pos.x;
            }
            if (pos.y > out_data->max_extents.y || !extent_set) {
                out_data->max_extents.y = pos.y;
            }
            if (pos.z > out_data->max_extents.z || !extent_set) {
                out_data->max_extents.z = pos.z;
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
        out_data->center.elements[i] = (out_data->min_extents.elements[i] +
                                        out_data->max_extents.elements[i]) /
                                       2.0f;
    }
}

// TODO: Load the material library file, and create material definitions from
// it. These definitions should be output to .kmt files. These .kmt files are
// then loaded when the material is acquired on mesh load. NOTE: This should
// eventually account for duplicate materials. When the .kmt files are written,
// if the file already exists the material should have something such as a
// number appended to its name and a warning thrown to the console. The artist
// should make sure material names are unique. When the material is acquired,
// the _original_ existing material name would be used, which would visually be
// wrong and serve as additional reinforcement of the message for material
// uniqueness. Material configs should not be returned or used here.
static b8 import_obj_material_library_file(const char *mtl_file_path) {
    KDEBUG("Importing obj .mtl file '%s'...", mtl_file_path);
    // Grab the .mtl file, if it exists, and read the material information.
    file_handle mtl_file;
    if (!filesystem_open(mtl_file_path, FILE_MODE_READ, false, &mtl_file)) {
        KERROR("Unable to open mtl file: %s", mtl_file_path);
        return false;
    }

    material_config current_config = {0};
    current_config.properties = darray_create(material_config_prop);
    current_config.maps = darray_create(material_map);

    b8 hit_name = false;

    char *line = 0;
    char line_buffer[512];
    char *p = &line_buffer[0];
    u64 line_length = 0;
    while (true) {
        if (!filesystem_read_line(&mtl_file, 512, &p, &line_length)) {
            break;
        }
        // Trim the line first.
        line = string_trim(line_buffer);
        line_length = string_length(line);

        // Skip blank lines.
        if (line_length < 1) {
            continue;
        }

        char first_char = line[0];
        switch (first_char) {
            case '#':
                // Skip comments
                continue;
            case 'K': {
                char second_char = line[1];
                switch (second_char) {
                    case 'a':
                    case 'd': {
                        // Ambient/Diffuse colour are treated the same at this level.
                        // ambient colour is determined by the level.
                        char t[2];
                        material_config_prop prop = {0};
                        prop.name = "diffuse_colour";
                        prop.type = SHADER_UNIFORM_TYPE_FLOAT32_4;

                        sscanf(line, "%s %f %f %f", t, &prop.value_v4.r,
                               &prop.value_v4.g,
                               &prop.value_v4.b);

                        // NOTE: This is only used by the colour shader, and will set to
                        // max_norm by default. Transparency could be added as a material
                        // property all its own at a later time.
                        prop.value_v4.a = 1.0f;

                        darray_push(current_config.properties, prop);
                    } break;
                    case 's': {
                        // Specular colour
                        char t[2];

                        // NOTE: Not using this for now.
                        f32 spec_rubbish = 0.0f;
                        sscanf(line, "%s %f %f %f", t, &spec_rubbish, &spec_rubbish,
                               &spec_rubbish);
                    } break;
                }
            } break;
            case 'N': {
                char second_char = line[1];
                switch (second_char) {
                    case 's': {
                        // Specular exponent
                        char t[2];

                        material_config_prop prop = {0};
                        prop.name = "shininess";
                        prop.type = SHADER_UNIFORM_TYPE_FLOAT32;
                        sscanf(line, "%s %f", t, &prop.value_f32);
                        // NOTE: Need to make sure this is nonzero as this will cause
                        // artefacts in the rendering of objects.
                        if (prop.value_f32 == 0) {
                            prop.value_f32 = 8.0f;
                        }

                        darray_push(current_config.properties, prop);
                    } break;
                }
            } break;
            case 'm': {
                // map
                char substr[10];
                char texture_file_name[512];

                sscanf(line, "%s %s", substr, texture_file_name);

                material_map map = {0};
                // NOTE: Making some assumptions about filtering and repeat modes.
                map.filter_min = map.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
                map.repeat_u = map.repeat_v = map.repeat_w = TEXTURE_REPEAT_REPEAT;

                // Texture name
                char tex_name_buf[512] = {0};
                string_filename_no_extension_from_path(tex_name_buf, texture_file_name);
                map.texture_name = string_duplicate(tex_name_buf);

                // map name/type
                if (strings_nequali(substr, "map_Kd", 6)) {
                    // Is a diffuse texture map
                    map.name = "diffuse";
                } else if (strings_nequali(substr, "map_Ks", 6)) {
                    // Is a specular texture map
                    map.name = "specular";
                } else if (strings_nequali(substr, "map_bump", 8)) {
                    // Is a bump texture map
                    map.name = "normal";
                }

                darray_push(current_config.maps, map);
            } break;
            case 'b': {
                // Some implementations use 'bump' instead of 'map_bump'.
                char substr[10];
                char texture_file_name[512];

                sscanf(line, "%s %s", substr, texture_file_name);

                material_map map = {0};
                // NOTE: Making some assumptions about filtering and repeat modes.
                map.filter_min = map.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
                map.repeat_u = map.repeat_v = map.repeat_w = TEXTURE_REPEAT_REPEAT;

                // Texture name
                char tex_name_buf[512] = {0};
                string_filename_no_extension_from_path(tex_name_buf, texture_file_name);
                map.texture_name = string_duplicate(tex_name_buf);

                if (strings_nequali(substr, "bump", 4)) {
                    map.name = "normal";
                }

                darray_push(current_config.maps, map);
            }
            case 'n': {
                // Some implementations use 'bump' instead of 'map_bump'.
                char substr[10];
                char material_name[512];

                sscanf(line, "%s %s", substr, material_name);
                if (strings_nequali(substr, "newmtl", 6)) {
                    // Is a material name.

                    // NOTE: Hardcoding default material shader name because all objects
                    // imported this way will be treated the same.
                    current_config.shader_name = "Shader.PBRMaterial";
                    if (hit_name) {
                        //  Write out a kmt file and move on.
                        if (!write_kmt_file(mtl_file_path, &current_config)) {
                            KERROR("Unable to write kmt file.");
                            return false;
                        }

                        // Reset the material for the next round.
                        darray_destroy(current_config.properties);
                        darray_destroy(current_config.maps);
                        if (current_config.name) {
                            u32 len = string_length(current_config.name);
                            kfree(current_config.name, len + 1, MEMORY_TAG_STRING);
                        }
                        kzero_memory(&current_config, sizeof(current_config));
                    }

                    hit_name = true;
                    if (!current_config.name) {
                        current_config.name = string_duplicate(material_name);
                    } else {
                        string_ncopy(current_config.name, material_name, 256);
                    }
                }
            }
        }
    }  // each line

    // Write out the remaining kmt file.
    // NOTE: Hardcoding default material shader name because all objects imported
    // this way will be treated the same.
    current_config.shader_name = "Shader.PBRMaterial";
    if (!write_kmt_file(mtl_file_path, &current_config)) {
        KERROR("Unable to write kmt file.");
        return false;
    }

    filesystem_close(&mtl_file);
    return true;
}

static const char *string_from_repeat(texture_repeat repeat) {
    switch (repeat) {
        default:
        case TEXTURE_REPEAT_REPEAT:
            return "repeat";
        case TEXTURE_REPEAT_CLAMP_TO_EDGE:
            return "clamp_to_edge";
        case TEXTURE_REPEAT_CLAMP_TO_BORDER:
            return "clamp_to_border";
        case TEXTURE_REPEAT_MIRRORED_REPEAT:
            return "mirrored";
    }
}

static const char *string_from_type(shader_uniform_type type) {
    switch (type) {
        case SHADER_UNIFORM_TYPE_FLOAT32:
            return "f32";
        case SHADER_UNIFORM_TYPE_FLOAT32_2:
            return "vec2";
        case SHADER_UNIFORM_TYPE_FLOAT32_3:
            return "vec3";
        case SHADER_UNIFORM_TYPE_FLOAT32_4:
            return "vec4";
        case SHADER_UNIFORM_TYPE_INT8:
            return "i8";
        case SHADER_UNIFORM_TYPE_INT16:
            return "i16";
        case SHADER_UNIFORM_TYPE_INT32:
            return "i32";
        case SHADER_UNIFORM_TYPE_UINT8:
            return "u8";
        case SHADER_UNIFORM_TYPE_UINT16:
            return "u16";
        case SHADER_UNIFORM_TYPE_UINT32:
            return "u32";
        case SHADER_UNIFORM_TYPE_MATRIX_4:
            return "mat4";
        default:
            KERROR("Unrecognized uniform type %d, defaulting to i32.", type);
            return "i32";
    }
}

/**
 * @brief Write out a kohi material file from config. This gets loaded by name
 * later when the mesh is requested for load.
 *
 * @param mtl_file_path The filepath of the material library file which
 * originally contained the material definition.
 * @param config A pointer to the config to be converted to kmt.
 * @return True on success; otherwise false.
 */
static b8 write_kmt_file(const char *mtl_file_path, material_config *config) {
    // NOTE: The .obj file this came from (and resulting .mtl file) sit in the
    // models directory. This moves up a level and back into the materials folder.
    // TODO: Read from config and get an absolute path for output.
    char *format_str = "%s../materials/%s%s";
    file_handle f;
    char directory[320];
    string_directory_from_path(directory, mtl_file_path);

    char full_file_path[512];
    string_format(full_file_path, format_str, directory, config->name, ".kmt");
    if (!filesystem_open(full_file_path, FILE_MODE_WRITE, false, &f)) {
        KERROR("Error opening material file for writing: '%s'", full_file_path);
        return false;
    }
    KDEBUG("Writing .kmt file '%s'...", full_file_path);

    char line_buffer[512];
    // File header
    filesystem_write_line(&f, "#material file");
    filesystem_write_line(&f, "");
    filesystem_write_line(&f, "version=2");

    filesystem_write_line(&f, "# Types can be phong,pbr,custom");
    filesystem_write_line(&f, "type=phong");  // TODO: Other material types

    string_format(line_buffer, "name=%s", config->name);
    filesystem_write_line(&f, line_buffer);

    filesystem_write_line(&f, "# If custom, shader is required.");
    string_format(line_buffer, "shader=%s", config->shader_name);
    filesystem_write_line(&f, line_buffer);

    // Write maps
    u32 map_count = darray_length(config->maps);
    for (u32 i = 0; i < map_count; ++i) {
        filesystem_write_line(&f, "[map]");
        string_format(line_buffer, "name=%s", config->maps[i].name);
        filesystem_write_line(&f, line_buffer);

        string_format(line_buffer, "filter_min=%s", config->maps[i].filter_min == TEXTURE_FILTER_MODE_LINEAR ? "linear" : "nearest");
        filesystem_write_line(&f, line_buffer);
        string_format(line_buffer, "filter_mag=%s", config->maps[i].filter_mag == TEXTURE_FILTER_MODE_LINEAR ? "linear" : "nearest");
        filesystem_write_line(&f, line_buffer);

        string_format(line_buffer, "repeat_u=%s", string_from_repeat(config->maps[i].repeat_u));
        filesystem_write_line(&f, line_buffer);
        string_format(line_buffer, "repeat_v=%s", string_from_repeat(config->maps[i].repeat_v));
        filesystem_write_line(&f, line_buffer);
        string_format(line_buffer, "repeat_w=%s", string_from_repeat(config->maps[i].repeat_w));
        filesystem_write_line(&f, line_buffer);

        string_format(line_buffer, "texture_name=%s", config->maps[i].texture_name);
        filesystem_write_line(&f, line_buffer);
        filesystem_write_line(&f, "[/map]");
    }

    // Write properties.
    u32 prop_count = darray_length(config->properties);
    for (u32 i = 0; i < prop_count; ++i) {
        filesystem_write_line(&f, "[prop]");
        string_format(line_buffer, "name=%s", config->properties[i].name);
        filesystem_write_line(&f, line_buffer);

        // type
        string_format(line_buffer, "type=%s", string_from_type(config->properties[i].type));
        filesystem_write_line(&f, line_buffer);
        // value
        switch (config->properties[i].type) {
            case SHADER_UNIFORM_TYPE_FLOAT32:
                string_format(line_buffer, "value=%f", config->properties[i].value_f32);
                break;
            case SHADER_UNIFORM_TYPE_FLOAT32_2:
                string_format(line_buffer, "value=%f %f", config->properties[i].value_v2);
                break;
            case SHADER_UNIFORM_TYPE_FLOAT32_3:
                string_format(line_buffer, "value=%f %f %f", config->properties[i].value_v3);
                break;
            case SHADER_UNIFORM_TYPE_FLOAT32_4:
                string_format(line_buffer, "value=%f %f %f %f", config->properties[i].value_v4);
                break;
            case SHADER_UNIFORM_TYPE_INT8:
                string_format(line_buffer, "value=%d", config->properties[i].value_i8);
                break;
            case SHADER_UNIFORM_TYPE_INT16:
                string_format(line_buffer, "value=%d", config->properties[i].value_i16);
                break;
            case SHADER_UNIFORM_TYPE_INT32:
                string_format(line_buffer, "value=%d", config->properties[i].value_i32);
                break;
            case SHADER_UNIFORM_TYPE_UINT8:
                string_format(line_buffer, "value=%u", config->properties[i].value_u8);
                break;
            case SHADER_UNIFORM_TYPE_UINT16:
                string_format(line_buffer, "value=%u", config->properties[i].value_u16);
                break;
            case SHADER_UNIFORM_TYPE_UINT32:
                string_format(line_buffer, "value=%u", config->properties[i].value_u32);
                break;
            case SHADER_UNIFORM_TYPE_MATRIX_4:
                string_format(line_buffer, "value=%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f ",
                              config->properties[i].value_mat4.data[0],
                              config->properties[i].value_mat4.data[1],
                              config->properties[i].value_mat4.data[2],
                              config->properties[i].value_mat4.data[3],
                              config->properties[i].value_mat4.data[4],
                              config->properties[i].value_mat4.data[5],
                              config->properties[i].value_mat4.data[6],
                              config->properties[i].value_mat4.data[7],
                              config->properties[i].value_mat4.data[8],
                              config->properties[i].value_mat4.data[9],
                              config->properties[i].value_mat4.data[10],
                              config->properties[i].value_mat4.data[11],
                              config->properties[i].value_mat4.data[12],
                              config->properties[i].value_mat4.data[13],
                              config->properties[i].value_mat4.data[14],
                              config->properties[i].value_mat4.data[15]);
                break;
            case SHADER_UNIFORM_TYPE_CUSTOM:
            default:
                // NOTE: All sampler types are included in this.
                KERROR("Unsupported material property type.");
                break;
        }
        filesystem_write_line(&f, line_buffer);
        filesystem_write_line(&f, "[/prop]");
    }

    filesystem_close(&f);

    return true;
}

resource_loader mesh_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_MESH;
    loader.custom_type = 0;
    loader.load = mesh_loader_load;
    loader.unload = mesh_loader_unload;
    loader.type_path = "models";

    return loader;
}
