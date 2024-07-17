#include "water_plane.h"

#include "logger.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include "core/engine.h"
#include "core/event.h"
#include "strings/kstring.h"
#include "platform/platform.h"

static b8 generate_texture(struct renderer_system_state* renderer, texture* t, const char* name, u32 tex_width, u32 tex_height, b8 is_depth);
static b8 water_plane_on_event(u16 code, void* sender, void* listener_inst, event_context data);

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
        plane->tiling = 0.25f;        // TODO: configurable.
        plane->wave_strength = 0.02f; // TODO: configurable.
        plane->wave_speed = 0.03f;    // TODO: configurable.

        // Create the geometry, but don't load it yet.
        // TODO: should probably be based on some size.
        f32 size = 256.0f;
        plane->vertices[0] = (water_plane_vertex){-size, 0, -size, 1};
        plane->vertices[1] = (water_plane_vertex){-size, 0, +size, 1};
        plane->vertices[2] = (water_plane_vertex){+size, 0, +size, 1};
        plane->vertices[3] = (water_plane_vertex){+size, 0, -size, 1};

        plane->indices[0] = 0;
        plane->indices[1] = 1;
        plane->indices[2] = 2;
        plane->indices[3] = 2;
        plane->indices[4] = 3;
        plane->indices[5] = 0;

        // Maps array
        plane->map_count = WATER_PLANE_MAP_COUNT;
        plane->maps = kallocate(sizeof(texture_map) * plane->map_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < plane->map_count; ++i) {
            texture_map* map = &plane->maps[i];
            map->filter_magnify = map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
            map->generation = INVALID_ID_U8;
            map->internal_id = INVALID_ID;
            map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_REPEAT;
            map->mip_levels = 1;
            map->texture = 0;
        }
        return true;
    }
    return false;
}

b8 water_plane_load(water_plane* plane) {
    if (plane) {

        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        // Allocate space
        if (!renderer_renderbuffer_allocate(vertex_buffer, sizeof(water_plane_vertex) * 4, &plane->vertex_buffer_offset)) {
            KERROR("Failed to allocate space in vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_allocate(index_buffer, sizeof(u32) * 6, &plane->index_buffer_offset)) {
            KERROR("Failed to allocate space in index buffer.");
            return false;
        }

        // Load data
        if (!renderer_renderbuffer_load_range(vertex_buffer, plane->vertex_buffer_offset, sizeof(water_plane_vertex) * 4, plane->vertices, false)) {
            KERROR("Failed to load data into vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_load_range(index_buffer, plane->index_buffer_offset, sizeof(u32) * 6, plane->indices, false)) {
            KERROR("Failed to load data into index buffer.");
            return false;
        }

        // Get the current window size as the dimensions of these textures will be based on this.
        kwindow* window = engine_active_window_get();
        // TODO: should probably cut this in half.
        u32 tex_width = window->width;
        u32 tex_height = window->height;
        struct renderer_system_state* renderer = engine_systems_get()->renderer_system;

        // Create reflection textures.
        texture* t = &plane->reflection_colour;
        if (!generate_texture(renderer, t, "__waterplane_reflection_colour__", tex_width, tex_height, false)) {
            return false;
        }
        t = &plane->reflection_depth;
        if (!generate_texture(renderer, t, "__waterplane_reflection_depth__", tex_width, tex_height, true)) {
            return false;
        }

        // Create refraction textures.
        t = &plane->refraction_colour;
        if (!generate_texture(renderer, t, "__waterplane_refraction_colour__", tex_width, tex_height, false)) {
            return false;
        }
        t = &plane->refraction_depth;
        if (!generate_texture(renderer, t, "__waterplane_refraction_depth__", tex_width, tex_height, true)) {
            return false;
        }

        // Get dudv texture.
        plane->dudv_texture = texture_system_acquire("Runtime.Texture.Water_DUDV", true);
        if (!plane->dudv_texture) {
            KERROR("Failed to load default DUDV texture for water plane. Water planes won't render correctly.");
        }

        // Get normal texture.
        plane->normal_texture = texture_system_acquire("Runtime.Texture.Water_Normal", true);
        if (!plane->normal_texture) {
            KERROR("Failed to load default Normal texture for water plane. Water planes won't render correctly.");
        }

        // Fill out texture maps.
        plane->maps[WATER_PLANE_MAP_REFLECTION].texture = &plane->reflection_colour;
        plane->maps[WATER_PLANE_MAP_REFRACTION].texture = &plane->refraction_colour;
        plane->maps[WATER_PLANE_MAP_DUDV].texture = plane->dudv_texture;
        plane->maps[WATER_PLANE_MAP_NORMAL].texture = plane->normal_texture;
        plane->maps[WATER_PLANE_MAP_SHADOW].texture = 0;
        plane->maps[WATER_PLANE_MAP_IBL_CUBE].texture = 0;
        plane->maps[WATER_PLANE_MAP_REFRACT_DEPTH].texture = &plane->refraction_depth;

        // Acquire instance resources for this plane.
        u32 shader_id = shader_system_get_id("Runtime.Shader.Water");
        if (!shader_system_shader_instance_acquire(shader_id, plane->map_count, plane->maps, &plane->instance_id)) {
            KERROR("Failed to acquire instance resources for water plane.");
            return false;
        }

        // Listen for window resizes, as these must trigger a resize of our reflect/refract
        // texture render targets. This should only be active while the plane is loaded.
        if (!event_register(EVENT_CODE_WINDOW_RESIZED, plane, water_plane_on_event)) {
            KERROR("Unable to register water plane for resize event. See logs for details.");
            return false;
        }

        return true;
    }
    return false;
}

b8 water_plane_unload(water_plane* plane) {
    if (plane) {
        // Immediately stop listening for resize events.
        if (!event_unregister(EVENT_CODE_WINDOW_RESIZED, plane, water_plane_on_event)) {
            // Nothing to really do about it, but warn the user.
            KWARN("Unable to unregister water plane for resize event. See logs for details.");
        }

        struct renderer_system_state* renderer = engine_systems_get()->renderer_system;
        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        // Free space
        if (!renderer_renderbuffer_free(vertex_buffer, sizeof(water_plane_vertex) * 4, plane->vertex_buffer_offset)) {
            KERROR("Failed to free space in vertex buffer.");
            return false;
        }
        if (!renderer_renderbuffer_free(index_buffer, sizeof(u32) * 6, plane->index_buffer_offset)) {
            KERROR("Failed to allfreeocate space in index buffer.");
            return false;
        }

        // Destroy generated textures.
        texture* textures[4] = {
            &plane->reflection_colour,
            &plane->reflection_depth,
            &plane->refraction_colour,
            &plane->refraction_depth};
        for (u32 i = 0; i < 4; ++i) {
            texture* t = textures[i];
            renderer_texture_resources_release(renderer, &t->renderer_texture_handle);
            if (t->name) {
                string_free(t->name);
                t->name = 0;
            }
            t->array_size = 0;
            t->channel_count = 0;
            t->flags = 0;
            t->generation = INVALID_ID_U8;
            t->height = t->width = 0;
            t->mip_levels = 0;
            t->type = 0;
        }

        // Release instance resources for this plane.
        u32 shader_id = shader_system_get_id("Runtime.Shader.Water");
        if (!shader_system_shader_instance_release(shader_id, plane->instance_id, plane->map_count, plane->maps)) {
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

static b8 generate_texture(struct renderer_system_state* renderer, texture* t, const char* name, u32 tex_width, u32 tex_height, b8 is_depth) {
    t->width = tex_width;
    t->height = tex_height;
    t->type = TEXTURE_TYPE_2D;
    t->flags = TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING;
    if (is_depth) {
        t->flags |= TEXTURE_FLAG_DEPTH;
    }
    t->array_size = 1;
    t->channel_count = 4;
    t->mip_levels = 1;
    t->generation = INVALID_ID_U8;
    t->id = -1;
    t->name = string_duplicate(name);

    if (!renderer_texture_resources_acquire(
            renderer,
            t->name,
            t->type,
            t->width,
            t->height,
            t->channel_count,
            t->mip_levels,
            t->array_size,
            t->flags,
            &t->renderer_texture_handle)) {
        KERROR("Failed to acquire renderer resources for '%s'.", t->name);
        return false;
    }

    // Count as "loaded".
    t->generation = 0;

    return true;
}

static b8 water_plane_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_WINDOW_RESIZED) {
        // Resize textures to match new frame buffer.
        u16 width = context.data.u16[0];
        u16 height = context.data.u16[1];

        // const kwindow* window = sender;
        water_plane* plane = listener_inst;

        if (plane->reflection_colour.generation != INVALID_ID_U8) {
            if (!texture_system_resize(&plane->reflection_colour, width, height, true)) {
                KERROR("Failed to resize reflection colour texture for water plane.");
            }
        }
        if (plane->reflection_depth.generation != INVALID_ID_U8) {
            if (!texture_system_resize(&plane->reflection_depth, width, height, true)) {
                KERROR("Failed to resize reflection depth texture for water plane.");
            }
        }

        if (plane->refraction_colour.generation != INVALID_ID_U8) {
            if (!texture_system_resize(&plane->refraction_colour, width, height, true)) {
                KERROR("Failed to resize refraction colour texture for water plane.");
            }
        }
        if (plane->refraction_depth.generation != INVALID_ID_U8) {
            if (!texture_system_resize(&plane->refraction_depth, width, height, true)) {
                KERROR("Failed to resize refraction depth texture for water plane.");
            }
        }
    }

    // Allow other systems to pick up event.
    return false;
}