#include "water_plane.h"

#include "logger.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "systems/shader_system.h"
#include "core/engine.h"

b8 water_plane_create(water_plane* out_plane) {
    if (!out_plane) {
        return false;
    }

    kzero_memory(out_plane, sizeof(water_plane));

    out_plane->model = mat4_identity();

    return true;
}
void water_plane_destroy(water_plane* plane) {
    if (plane) {
        //
    }
}

b8 water_plane_initialize(water_plane* plane) {
    if (plane) {
        // Create the geometry, but don't load it yet.
        // TODO: should probably be based on some size.
        f32 size = 100.0f;
        plane->vertices[0] = (vec4){-size, 0, -size, 1};
        plane->vertices[1] = (vec4){-size, 0, size, 1};
        plane->vertices[2] = (vec4){size, 0, size, 1};
        plane->vertices[3] = (vec4){size, 0, -size, 1};

        plane->indices[0] = 0;
        plane->indices[1] = 1;
        plane->indices[2] = 2;
        plane->indices[3] = 2;
        plane->indices[4] = 3;
        plane->indices[5] = 0;
        return true;
    }
    return false;
}

b8 water_plane_load(water_plane* plane) {
    if (plane) {

        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        // Allocate space
        if (!renderer_renderbuffer_allocate(vertex_buffer, sizeof(vec4) * 4, &plane->vertex_buffer_offset)) {
            KERROR("Failed to allocate space in vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_allocate(index_buffer, sizeof(u32) * 6, &plane->index_buffer_offset)) {
            KERROR("Failed to allocate space in index buffer.");
            return false;
        }

        // Load data
        if (!renderer_renderbuffer_load_range(vertex_buffer, plane->vertex_buffer_offset, sizeof(vec4) * 4, plane->vertices, false)) {
            KERROR("Failed to load data into vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_load_range(index_buffer, plane->index_buffer_offset, sizeof(u32) * 6, plane->indices, false)) {
            KERROR("Failed to load data into index buffer.");
            return false;
        }

        // Acquire instance resources for this plane.
        u32 shader_id = shader_system_get_id("Runtime.Shader.Water");
        if (!shader_system_shader_instance_acquire(shader_id, 0, 0, &plane->instance_id)) {
            KERROR("Failed to acquire instance resources for water plane.");
            return false;
        }

        return true;
    }
    return false;
}
b8 water_plane_unload(water_plane* plane) {
    if (plane) {
        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        // Free space
        if (!renderer_renderbuffer_free(vertex_buffer, sizeof(vec4) * 4, plane->vertex_buffer_offset)) {
            KERROR("Failed to free space in vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_free(index_buffer, sizeof(u32) * 6, plane->index_buffer_offset)) {
            KERROR("Failed to allfreeocate space in index buffer.");
            return false;
        }

        // Release instance resources for this plane.
        u32 shader_id = shader_system_get_id("Runtime.Shader.Water");
        if (!shader_system_shader_instance_release(shader_id, plane->instance_id)) {
            KERROR("Failed to release instance resources for water plane.");
            return false;
        }
        return true;
    }
    return false;
}

b8 water_plane_update(water_plane* plane) {
    if (plane) {
        //
        return true;
    }
    return false;
}