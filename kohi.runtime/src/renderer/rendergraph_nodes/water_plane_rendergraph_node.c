#include "water_plane_rendergraph_node.h"

#include "core/engine.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "renderer/viewport.h"

#include "resources/water_plane.h"

typedef struct water_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
    u16 dummy;
} water_shader_locations;

typedef struct water_plane_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;

    u32 water_shader_id;
    shader* water_shader;
    water_shader_locations shader_locations;

    renderbuffer* vertex_buffer;
    renderbuffer* index_buffer;

    // Default colour/depth buffers
    struct texture* colourbuffer_texture;
    struct texture* depthbuffer_texture;

    struct texture* refraction;
    struct texture* reflection;

    viewport vp;
    mat4 view;
    mat4 projection;

    u32 count;
    struct water_plane** planes;
} water_plane_rendergraph_node_internal_data;

b8 water_plane_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self) {
        KERROR("water_plane_rendergraph_node_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("water_plane_rendergraph_node_create requires a valid configuration.");
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(water_plane_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;

    self->name = string_duplicate(config->name);

    // Has two sinks, one for the colourbuffer and one for depthbuffer.
    self->sink_count = 2;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    rendergraph_node_sink_config* colourbuffer_sink_config = 0;
    rendergraph_node_sink_config* depthbuffer_sink_config = 0;
    for (u32 i = 0; i < config->sink_count; ++i) {
        rendergraph_node_sink_config* sink = &config->sinks[i];
        if (strings_equali("colourbuffer", sink->name)) {
            colourbuffer_sink_config = sink;
        } else if (strings_equali("depthbuffer", sink->name)) {
            depthbuffer_sink_config = sink;
        } else {
            KWARN("Water plane rendergraph node contains config for unknown sink '%s', which will be ignored.", sink->name);
        }
    }

    if (colourbuffer_sink_config) {
        // Setup the colourbuffer sink.
        rendergraph_sink* colourbuffer_sink = &self->sinks[0];
        colourbuffer_sink->name = string_duplicate("colourbuffer");
        colourbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        colourbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        colourbuffer_sink->configured_source_name = string_duplicate(colourbuffer_sink_config->source_name);
    } else {
        KERROR("Water plane rendergraph node requires configuration for sink called 'colourbuffer'.");
        return false;
    }

    if (depthbuffer_sink_config) {
        // Setup the colourbuffer sink.
        rendergraph_sink* depthbuffer_sink = &self->sinks[1];
        depthbuffer_sink->name = string_duplicate("depthbuffer");
        depthbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        depthbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        depthbuffer_sink->configured_source_name = string_duplicate(depthbuffer_sink_config->source_name);
    } else {
        KERROR("Water plane rendergraph node requires configuration for sink called 'depthbuffer'.");
        return false;
    }

    // Has two sources, for the colourbuffer and depth buffer.
    self->source_count = 2;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* colourbuffer_source = &self->sources[0];
    colourbuffer_source->name = string_duplicate("colourbuffer");
    colourbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    colourbuffer_source->value.t = 0;
    colourbuffer_source->is_bound = false;

    // Setup the depthbuffer source.
    rendergraph_source* depthbuffer_source = &self->sources[1];
    depthbuffer_source->name = string_duplicate("depthbuffer");
    depthbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    depthbuffer_source->value.t = 0;
    depthbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = water_plane_rendergraph_node_initialize;
    self->destroy = water_plane_rendergraph_node_destroy;
    self->load_resources = water_plane_rendergraph_node_load_resources;
    self->execute = water_plane_rendergraph_node_execute;

    return true;
}

b8 water_plane_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load Water plane shader and get shader uniform locations.
    // Get a pointer to the shader.
    internal_data->water_shader = shader_system_get("Runtime.Shader.Water");
    internal_data->water_shader_id = internal_data->water_shader->id;
    internal_data->shader_locations.projection = shader_system_uniform_location(internal_data->water_shader_id, "projection");
    internal_data->shader_locations.view = shader_system_uniform_location(internal_data->water_shader_id, "view");
    internal_data->shader_locations.model = shader_system_uniform_location(internal_data->water_shader_id, "model");
    internal_data->shader_locations.dummy = shader_system_uniform_location(internal_data->water_shader_id, "dummy");

    internal_data->vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    internal_data->index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    return true;
}

b8 water_plane_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    // Resolve framebuffer handle via bound source.
    water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;
    if (self->sinks[0].bound_source) {
        internal_data->colourbuffer_texture = self->sinks[0].bound_source->value.t;
        self->sources[0].value.t = internal_data->colourbuffer_texture;
        self->sources[0].is_bound = true;
    } else {
        return false;
    }
    if (self->sinks[1].bound_source) {
        internal_data->depthbuffer_texture = self->sinks[1].bound_source->value.t;
        self->sources[1].value.t = internal_data->depthbuffer_texture;
        self->sources[1].is_bound = true;
    } else {
        return false;
    }

    return true;
}

b8 water_plane_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;

    renderer_begin_debug_label("Water Plane", (vec3){0, 0, 1});

    if (internal_data->count > 0) {
        // Bind the viewport
        renderer_active_viewport_set(&internal_data->vp);

        // TODO: Will need to do this once for refraction, then once for reflection w/ transformed camera.
        renderer_begin_rendering(internal_data->renderer, p_frame_data, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, internal_data->depthbuffer_texture->renderer_texture_handle, 0);

        shader_system_use_by_id(internal_data->water_shader->id);

        // Globals
        shader_system_uniform_set_by_location(internal_data->water_shader_id, internal_data->shader_locations.projection, &internal_data->projection);
        shader_system_uniform_set_by_location(internal_data->water_shader_id, internal_data->shader_locations.view, &internal_data->view);
        shader_system_apply_global(internal_data->water_shader_id);

        // Each water plane.
        for (u32 i = 0; i < internal_data->count; ++i) {
            // NOTE: No instance-level uniforms to be set.
            water_plane* plane = internal_data->planes[i];

            // Instance uniforms
            shader_system_bind_instance(internal_data->water_shader_id, plane->instance_id);
            vec4 dummy = (vec4){0.0f, 0.0f, 1.0f, 0.0f};
            shader_system_uniform_set_by_location(internal_data->water_shader_id, internal_data->shader_locations.dummy, &dummy /*&plane->dummy*/);
            shader_system_apply_instance(internal_data->water_shader_id);

            // Set model matrix.
            // TODO: model matrix from transform.
            mat4 model = mat4_identity();
            shader_system_uniform_set_by_location(internal_data->water_shader_id, internal_data->shader_locations.model, &model);
            shader_system_apply_local(internal_data->water_shader_id);

            // Draw it.
            // TODO: Draw based on vert/index data.
            // renderer_geometry_draw(render_data);
            if (!renderer_renderbuffer_draw(internal_data->vertex_buffer, plane->vertex_buffer_offset, 4, true)) {
                KERROR("Failed to bind vertex buffer data for water plane.");
                return false;
            }
            if (!renderer_renderbuffer_draw(internal_data->index_buffer, plane->index_buffer_offset, 6, false)) {
                KERROR("Failed to draw water plane using index data.");
                return false;
            }
        }

        renderer_end_rendering(internal_data->renderer, p_frame_data);
    }

    renderer_end_debug_label();

    return true;
}

void water_plane_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            kfree(self->internal_data, sizeof(water_plane_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}

b8 water_plane_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v) {
    if (self && self->internal_data) {
        water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->vp = v;
        return true;
    }
    return false;
}

b8 water_plane_rendergraph_node_view_projection_set(struct rendergraph_node* self, mat4 view_matrix, vec3 view_pos, mat4 projection_matrix) {
    if (self && self->internal_data) {
        water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->view = view_matrix;
        internal_data->projection = projection_matrix;
        return true;
    }
    return false;
}

b8 water_plane_rendergraph_node_water_planes_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 count, struct water_plane** planes) {
    if (self && self->internal_data) {
        water_plane_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->count = count;
        if (count > 0) {
            internal_data->planes = p_frame_data->allocator.allocate(sizeof(struct water_plane*) * count);
            kcopy_memory(internal_data->planes, planes, sizeof(struct water_plane*) * count);
        } else {
            internal_data->planes = 0;
        }
        return true;
    }
    return false;
}

b8 water_plane_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "water_plane";
    factory.create = water_plane_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
