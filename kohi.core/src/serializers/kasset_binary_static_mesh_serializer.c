#include "kasset_binary_static_mesh_serializer.h"

#include "assets/kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "strings/kstring.h"

typedef struct binary_static_mesh_header {
    // The base binary asset header. Must always be the first member.
    binary_asset_header base;
    // The static_mesh extents.
    extents_3d extents;
    // The static_mesh center point.
    vec3 center;
    // The number of geometries in the static_mesh.
    u16 geometry_count;
} binary_static_mesh_header;

typedef struct binary_static_mesh_geometry {
    u32 vertex_count;
    u32 index_count;
    extents_3d extents;
    vec3 center;
} binary_static_mesh_geometry;

KAPI void* kasset_binary_static_mesh_serialize(const kasset* asset, u64* out_size) {
    if (!asset) {
        KERROR("Cannot serialize without an asset, ya dingus!");
        return 0;
    }

    if (asset->type != KASSET_TYPE_STATIC_MESH) {
        KERROR("Cannot serialize a non-static_mesh asset using the static_mesh serializer.");
        return 0;
    }

    binary_static_mesh_header header = {0};
    // Base attributes.
    header.base.magic = ASSET_MAGIC;
    header.base.type = (u32)asset->type;
    header.base.data_block_size = 0;
    // Always write the most current version.
    header.base.version = 1;

    kasset_static_mesh* typed_asset = (kasset_static_mesh*)asset;

    header.geometry_count = typed_asset->geometry_count;
    header.extents = typed_asset->extents;
    header.center = typed_asset->center;

    // Calculate the total required size first (for everything after the header.
    {

        for (u32 i = 0; i < typed_asset->geometry_count; ++i) {
            kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

            // Center and extents.
            header.base.data_block_size += sizeof(g->center);
            header.base.data_block_size += sizeof(g->extents);

            // Name length.
            header.base.data_block_size += sizeof(u32);

            // Name string (no null terminator).
            if (g->name) {
                const char* gname = kname_string_get(g->name);
                u64 len = string_length(gname);
                header.base.data_block_size += len;
            }

            // Material asset name length.
            header.base.data_block_size += sizeof(u32);

            // Material asset name string (no null terminator).
            if (g->material_asset_name) {
                const char* gmat_asset_name = kname_string_get(g->material_asset_name);
                u64 len = string_length(gmat_asset_name) + 1;
                header.base.data_block_size += len;
            }

            // index count.
            header.base.data_block_size += sizeof(u32);

            // Indices
            if (g->index_count && g->indices) {
                // indices.
                u64 index_array_size = sizeof(u32) * g->index_count;
                header.base.data_block_size += index_array_size;
            }

            // Write vertex count.
            header.base.data_block_size += sizeof(u32);

            // Vertices
            if (g->vertex_count && g->indices) {
                // Write vertices.
                u64 vertex_array_size = sizeof(vertex_3d) * g->vertex_count;
                header.base.data_block_size += vertex_array_size;
            }
        }
    }

    // The total space required for the data block.
    *out_size = sizeof(binary_static_mesh_header) + header.base.data_block_size;

    // Allocate said block.
    u8* block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
    // Write the header block.
    kcopy_memory(block, &header, sizeof(binary_static_mesh_header));

    // For this asset, it's not quite a simple manner of just using the byte block.
    // Start by moving past the header.
    u64 offset = sizeof(binary_static_mesh_header);

    // Iterate the geometries, writing first the size of the block it takes, followed
    // by the actual block of data itself.
    for (u32 i = 0; i < header.geometry_count; ++i) {
        kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

        // Copy center and extents.
        kcopy_memory(block + offset, &g->center, sizeof(g->center));
        offset += sizeof(g->center);
        kcopy_memory(block + offset, &g->extents, sizeof(g->extents));
        offset += sizeof(g->extents);

        // Name
        {
            u32 name_len = 0;
            const char* str = 0;
            if (g->name) {
                str = kname_string_get(g->name);
                name_len = string_length(str);
            }

            // Store name length.
            kcopy_memory(block + offset, &name_len, sizeof(u32));
            offset += sizeof(u32);

            // Store the name string if one exists.
            if (name_len) {
                kcopy_memory(block + offset, str, name_len);
                offset += name_len;
            }
        }

        // Asset material name.
        {
            u32 mat_asset_name_len = 0;
            const char* str = 0;
            if (g->material_asset_name) {
                str = kname_string_get(g->material_asset_name);
                mat_asset_name_len = string_length(str);
            }

            // Store material asset name length.
            kcopy_memory(block + offset, &mat_asset_name_len, sizeof(u32));
            offset += sizeof(u32);

            // Store the material_asset_name string if one exists.
            if (mat_asset_name_len) {
                kcopy_memory(block + offset, str, mat_asset_name_len);
                offset += mat_asset_name_len;
            }
        }

        // Write index count.
        kcopy_memory(block + offset, &g->index_count, sizeof(u32));
        offset += sizeof(u32);

        // Indices
        if (g->index_count && g->indices) {
            u64 index_array_size = sizeof(u32) * g->index_count;
            kcopy_memory(block + offset, g->indices, index_array_size);
            offset += index_array_size;
        }

        // Write vertex count.
        kcopy_memory(block + offset, &g->vertex_count, sizeof(u32));
        offset += sizeof(u32);

        // Vertices
        if (g->vertex_count && g->vertices) {
            u64 vertex_array_size = sizeof(vertex_3d) * g->vertex_count;
            kcopy_memory(block + offset, g->vertices, vertex_array_size);
            offset += vertex_array_size;
        }
    }

    // Return the serialized block of memory.
    return block;
}

KAPI b8 kasset_binary_static_mesh_deserialize(u64 size, const void* in_block, kasset* out_asset) {
    if (!size || !in_block || !out_asset) {
        KERROR("Cannot deserialize without a nonzero size, block of memory and an static_mesh to write to.");
        return false;
    }

    const u8* block = in_block;

    // Extract header info by casting the first bits of the block to the header.
    const binary_static_mesh_header* header = (const binary_static_mesh_header*)block;
    if (header->base.magic != ASSET_MAGIC) {
        KERROR("Memory is not a Kohi binary asset.");
        return false;
    }

    kasset_type type = (kasset_type)header->base.type;
    if (type != KASSET_TYPE_STATIC_MESH) {
        KERROR("Memory is not a Kohi static_mesh asset.");
        return false;
    }

    out_asset->meta.version = header->base.version;
    out_asset->type = type;

    kasset_static_mesh* typed_asset = (kasset_static_mesh*)out_asset;
    typed_asset->geometry_count = header->geometry_count;
    typed_asset->extents = header->extents;
    typed_asset->center = header->center;

    u64 offset = sizeof(binary_static_mesh_header);
    if (typed_asset->geometry_count) {
        typed_asset->geometries = kallocate(sizeof(kasset_static_mesh_geometry) * typed_asset->geometry_count, MEMORY_TAG_ARRAY);

        // Get geometry data.
        for (u32 i = 0; i < typed_asset->geometry_count; ++i) {
            kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

            // Copy center and extents.
            kcopy_memory(&g->center, block + offset, sizeof(g->center));
            offset += sizeof(g->center);
            kcopy_memory(&g->extents, block + offset, sizeof(g->extents));
            offset += sizeof(g->extents);

            // Name
            {
                // Name length first.
                u32 name_len = 0;
                kcopy_memory(&name_len, block + offset, sizeof(u32));
                offset += sizeof(u32);

                // Name if it exists.
                if (name_len) {
                    // Allocate 1 extra byte for a null-terminator here.
                    char* name_buffer = kallocate(sizeof(char) * (name_len + 1), MEMORY_TAG_STRING);
                    kcopy_memory(name_buffer, block + offset, sizeof(char) * name_len);
                    name_buffer[name_len] = 0;
                    offset += name_len;
                    g->name = kname_create(name_buffer);
                    string_free(name_buffer);
                }
            }

            // Material asset name
            {
                // length
                u32 mat_asset_name_len = 0;
                kcopy_memory(&mat_asset_name_len, block + offset, sizeof(u32));
                offset += sizeof(u32);

                // Asset if it exists.
                if (mat_asset_name_len) {
                    // Allocate 1 extra byte for a null-terminator here.
                    char* mat_name_buffer = kallocate(sizeof(char) * (mat_asset_name_len + 1), MEMORY_TAG_STRING);
                    kcopy_memory(mat_name_buffer, block + offset, sizeof(char) * mat_asset_name_len);
                    mat_name_buffer[mat_asset_name_len] = 0;
                    offset += mat_asset_name_len;
                    g->material_asset_name = kname_create(mat_name_buffer);
                    string_free(mat_name_buffer);
                }
            }

            // Indices
            {
                // read count first.
                kcopy_memory(&g->index_count, block + offset, sizeof(u32));
                offset += sizeof(u32);

                // Read indices if there are any.
                if (g->index_count) {
                    u64 index_array_size = sizeof(u32) * g->index_count;
                    g->indices = kallocate(index_array_size, MEMORY_TAG_ARRAY);
                    kcopy_memory(g->indices, block + offset, index_array_size);
                    offset += index_array_size;
                }
            }

            // Vertices
            {
                // read count first.
                kcopy_memory(&g->vertex_count, block + offset, sizeof(u32));
                offset += sizeof(u32);

                // Read vertices if there are any.
                if (g->vertex_count) {
                    u64 vertex_array_size = sizeof(vertex_3d) * g->vertex_count;
                    g->vertices = kallocate(vertex_array_size, MEMORY_TAG_ARRAY);
                    kcopy_memory(g->vertices, block + offset, vertex_array_size);
                    offset += vertex_array_size;
                }
            }
        }
    } // end geometries.

    // Done!
    return true;
}
