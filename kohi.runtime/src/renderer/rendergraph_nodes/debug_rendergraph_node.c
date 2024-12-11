#include "debug_rendergraph_node.h"

#include "core/engine.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"

typedef struct debug_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_shader_locations;

typedef struct debug_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;

    khandle colour_shader;
    debug_shader_locations debug_locations;

    struct kresource_texture* colourbuffer_texture;
    struct kresource_texture* depthbuffer_texture;

    viewport vp;
    mat4 view;
    mat4 projection;

    u32 geometry_count;
    geometry_render_data* geometries;
} debug_rendergraph_node_internal_data;

b8 debug_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self) {
        KERROR("debug_rendergraph_node_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("debug_rendergraph_node_create requires a valid configuration.");
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(debug_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    debug_rendergraph_node_internal_data* internal_data = self->internal_data;
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
            KWARN("Debug rendergraph node contains config for unknown sink '%s', which will be ignored.", sink->name);
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
        KERROR("Debug rendergraph node requires configuration for sink called 'colourbuffer'.");
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
        KERROR("Debug rendergraph node requires configuration for sink called 'depthbuffer'.");
        return false;
    }

    // Has one source, for the colourbuffer.
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
    self->initialize = debug_rendergraph_node_initialize;
    self->destroy = debug_rendergraph_node_destroy;
    self->load_resources = debug_rendergraph_node_load_resources;
    self->execute = debug_rendergraph_node_execute;

    return true;
}

b8 debug_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    debug_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load debug colour3d shader and get shader uniform locations.
    // Get a pointer to the shader.
    internal_data->colour_shader = shader_system_get(kname_create("Shader.Builtin.ColourShader3D"));
    internal_data->debug_locations.projection = shader_system_uniform_location(internal_data->colour_shader, kname_create("projection"));
    internal_data->debug_locations.view = shader_system_uniform_location(internal_data->colour_shader, kname_create("view"));
    internal_data->debug_locations.model = shader_system_uniform_location(internal_data->colour_shader, kname_create("model"));

    return true;
}

b8 debug_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    // Resolve framebuffer handle via bound source.
    debug_rendergraph_node_internal_data* internal_data = self->internal_data;
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

b8 debug_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    debug_rendergraph_node_internal_data* internal_data = self->internal_data;

    renderer_begin_debug_label(self->name, (vec3){0.5f, 1.0f, 0});

    if (internal_data->geometry_count > 0) {
        renderer_begin_rendering(internal_data->renderer, p_frame_data, internal_data->vp.rect, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, internal_data->depthbuffer_texture->renderer_texture_handle, 0);

        // Bind the viewport
        renderer_active_viewport_set(&internal_data->vp);

        shader_system_use(internal_data->colour_shader);

        // Per-frame data
        shader_system_uniform_set_by_location(internal_data->colour_shader, internal_data->debug_locations.projection, &internal_data->projection);
        shader_system_uniform_set_by_location(internal_data->colour_shader, internal_data->debug_locations.view, &internal_data->view);
        shader_system_apply_per_frame(internal_data->colour_shader);

        for (u32 i = 0; i < internal_data->geometry_count; ++i) {
            // NOTE: No instance-level uniforms to be set.
            geometry_render_data* render_data = &internal_data->geometries[i];
            shader_system_bind_draw_id(internal_data->colour_shader, render_data->draw_id);

            // Set model matrix.
            shader_system_uniform_set_by_location(internal_data->colour_shader, internal_data->debug_locations.model, &render_data->model);
            if (!shader_system_apply_per_draw(internal_data->colour_shader, render_data->draw_generation)) {
                KERROR("Failed to apply per-draw uniforms in debug shader. Geometry will not be drawn.");
                continue;
            }

            // Draw it.
            renderer_geometry_draw(render_data);
        }

        renderer_end_rendering(internal_data->renderer, p_frame_data);
    }

    renderer_end_debug_label();

    return true;
}

void debug_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            kfree(self->internal_data, sizeof(debug_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}

b8 debug_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v) {
    if (self && self->internal_data) {
        debug_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->vp = v;
        return true;
    }
    return false;
}

b8 debug_rendergraph_node_view_projection_set(struct rendergraph_node* self, mat4 view_matrix, vec3 view_pos, mat4 projection_matrix) {
    if (self && self->internal_data) {
        debug_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->view = view_matrix;
        internal_data->projection = projection_matrix;
        return true;
    }
    return false;
}

b8 debug_rendergraph_node_debug_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (self && self->internal_data) {
        debug_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->geometry_count = geometry_count;
        internal_data->geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
        kcopy_memory(internal_data->geometries, geometries, sizeof(geometry_render_data) * geometry_count);
        return true;
    }
    return false;
}

b8 debug_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "debug3d";
    factory.create = debug_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
