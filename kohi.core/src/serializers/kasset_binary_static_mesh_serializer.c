#include "kasset_binary_static_mesh_serializer.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
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
    for (u32 i = 0; i < typed_asset->geometry_count; ++i) {
        kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

        // Keep a reference to the point in memory where size will be written.
        header.base.data_block_size += sizeof(u64);

        // Center and extents.
        header.base.data_block_size += sizeof(g->center);
        header.base.data_block_size += sizeof(g->extents);

        // Name + null terminator.
        if (g->name) {
            const char* gname = kname_string_get(g->name);
            u64 len = string_length(gname + 1);
            header.base.data_block_size += len;
        }

        if (g->material_asset_name) {
            const char* gmat_asset_name = kname_string_get(g->material_asset_name);
            u64 len = string_length(gmat_asset_name) + 1;
            header.base.data_block_size += len;
        }

        // Indices
        if (g->index_count && g->indices) {
            // index count.
            header.base.data_block_size += sizeof(u32);

            // indices.
            u64 index_array_size = sizeof(u32) * g->index_count;
            header.base.data_block_size += index_array_size;
        } else {
            // Only write a zero size. Serialization will see this and skip it in this case.
            header.base.data_block_size += sizeof(u32);
        }

        // Vertices
        if (g->vertex_count && g->indices) {
            // Write vertex count.
            header.base.data_block_size += sizeof(u32);

            // Write vertices.
            u64 vertex_array_size = sizeof(vertex_3d) * g->vertex_count;
            header.base.data_block_size += vertex_array_size;
        } else {
            // Only write a zero size. Serialization will see this and skip it in this case.
            header.base.data_block_size += sizeof(u32);
        }
    }

    // The total space required for the data block.
    *out_size = sizeof(binary_static_mesh_header) + header.base.data_block_size;

    // Allocate said block.
    void* block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
    // Write the header.
    kcopy_memory(block, &header, sizeof(binary_static_mesh_header));

    // For this asset, it's not quite a simple manner of just using the byte block.
    // Start by moving past the header.
    u64 offset = sizeof(binary_static_mesh_header);

    // Iterate the geometries, writing first the size of the block it takes, followed
    // by the actual block of data itself.
    for (u32 i = 0; i < typed_asset->geometry_count; ++i) {
        kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

        // Keep a reference to the point in memory where size will be written.
        u64* size = block + offset;
        offset += sizeof(u64);

        u64 geometry_start = offset;

        // Copy center and extents.
        kcopy_memory(block + offset, &g->center, sizeof(g->center));
        offset += sizeof(g->center);
        kcopy_memory(block + offset, &g->extents, sizeof(g->extents));
        offset += sizeof(g->extents);

        // Name + null terminator.
        if (g->name) {
            const char* str = kname_string_get(g->name);
            u64 len = string_length(str + 1);
            kcopy_memory(block + offset, str, len);
            offset += len;
        }

        if (g->material_asset_name) {
            const char* str = kname_string_get(g->material_asset_name);
            u64 len = string_length(str) + 1;
            kcopy_memory(block + offset, str, len);
            offset += len;
        }

        // Indices
        if (g->index_count && g->indices) {
            // Write index count.
            kcopy_memory(block + offset, &g->index_count, sizeof(u32));
            offset += sizeof(u32);

            // Write indices.
            u64 index_array_size = sizeof(u32) * g->index_count;
            kcopy_memory(block + offset, g->indices, index_array_size);
            offset += index_array_size;
        } else {
            // Only write a zero size. Serialization will see this and skip it in this case.
            u32 zero_size = 0;
            kcopy_memory(block + offset, &zero_size, sizeof(u32));
            offset += sizeof(u32);
        }

        // Vertices
        if (g->vertex_count && g->indices) {
            // Write vertex count.
            kcopy_memory(block + offset, &g->vertex_count, sizeof(u32));
            offset += sizeof(u32);

            // Write vertices.
            u64 vertex_array_size = sizeof(vertex_3d) * g->vertex_count;
            kcopy_memory(block + offset, g->vertices, vertex_array_size);
            offset += vertex_array_size;
        } else {
            // Only write a zero size. Serialization will see this and skip it in this case.
            u32 zero_size = 0;
            kcopy_memory(block + offset, &zero_size, sizeof(u32));
            offset += sizeof(u32);
        }

        // Write the diff in pointer position (i.e. the size) in the earlier saved pointer.
        u64 geometry_size = offset - geometry_start;
        *size = geometry_size;
    }

    // Return the serialized block of memory.
    return block;
}

KAPI b8 kasset_binary_static_mesh_deserialize(u64 size, const void* block, kasset* out_asset) {
    if (!size || !block || !out_asset) {
        KERROR("Cannot deserialize without a nonzero size, block of memory and an static_mesh to write to.");
        return false;
    }

    const binary_static_mesh_header* header = block;
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

            // Get the size of the next block.
            u64 geometry_block_size = 0;
            kcopy_memory(&geometry_block_size, block + offset, sizeof(u64));
            offset += sizeof(u64);

            // Copy center and extents.
            kcopy_memory(&g->center, block + offset, sizeof(g->center));
            offset += sizeof(g->center);
            kcopy_memory(&g->extents, block + offset, sizeof(g->extents));
            offset += sizeof(g->extents);

            // Name + null terminator.
            if (g->name) {
                // Extracting this requires moving ahead until a \0 is found (the null terminator).
                u64 len = 0;
                const char* cblock = block + offset;
                for (len = 0; cblock[len]; ++len)
                    ;

                // Now copy all the string at once.
                kcopy_memory((void*)g->name, block + offset, sizeof(char) * len);
                // +1 more to account for null terminator.
                offset += len + 1;
            }

            // Asset name + null terminator.
            if (g->material_asset_name) {
                // Extracting this requires moving ahead until a \0 is found (the null terminator).
                u64 len = 0;
                const char* cblock = block + offset;
                for (len = 0; cblock[len]; ++len)
                    ;

                // Now copy all the string at once.
                kcopy_memory((void*)g->material_asset_name, block + offset, sizeof(char) * len);
                // +1 more to account for null terminator.
                offset += len + 1;
            }

            // Indices - read count first.
            kcopy_memory(&g->index_count, block + offset, sizeof(u32));
            offset += sizeof(u32);

            // Read indices if there are any.
            if (g->index_count) {
                u64 index_array_size = sizeof(u32) * g->index_count;
                g->indices = kallocate(index_array_size, MEMORY_TAG_ARRAY);
                kcopy_memory(g->indices, block + offset, index_array_size);
                offset += index_array_size;
            }

            // Vertices - read count first.
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
    } // end geometries.

    // Done!
    return true;
}
