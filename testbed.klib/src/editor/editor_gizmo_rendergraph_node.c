#include "editor_gizmo_rendergraph_node.h"

#include "core/engine.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "renderer/viewport.h"
#include "editor_gizmo.h"
#include "math/kmath.h"
#include "systems/xform_system.h"

typedef struct debug_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_shader_locations;

typedef struct editor_gizmo_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;

    u32 colour_shader_id;
    shader* colour_shader;
    debug_shader_locations debug_locations;

    struct texture* colourbuffer_texture;

    viewport vp;
    mat4 view;
    mat4 projection;

    editor_gizmo* gizmo;
    b8 enabled;
} editor_gizmo_rendergraph_node_internal_data;

b8 editor_gizmo_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self) {
        KERROR("editor_gizmo_rendergraph_node_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("editor_gizmo_rendergraph_node_create requires a valid configuration.");
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(editor_gizmo_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;

    self->name = string_duplicate(config->name);

    // Has two sinks, one for the colourbuffer and one for depthbuffer.
    self->sink_count = 1;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    rendergraph_node_sink_config* colourbuffer_sink_config = 0;
    for (u32 i = 0; i < config->sink_count; ++i) {
        rendergraph_node_sink_config* sink = &config->sinks[i];
        if (strings_equali("colourbuffer", sink->name)) {
            colourbuffer_sink_config = sink;
        } else {
            KWARN("Editor gizmo rendergraph node contains config for unknown sink '%s', which will be ignored.", sink->name);
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

    // Has one source, for the colourbuffer.
    self->source_count = 1;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* colourbuffer_source = &self->sources[0];
    colourbuffer_source->name = string_duplicate("colourbuffer");
    colourbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    colourbuffer_source->value.t = 0;
    colourbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = editor_gizmo_rendergraph_node_initialize;
    self->destroy = editor_gizmo_rendergraph_node_destroy;
    self->load_resources = editor_gizmo_rendergraph_node_load_resources;
    self->execute = editor_gizmo_rendergraph_node_execute;

    return true;
}

b8 editor_gizmo_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load debug colour3d shader and get shader uniform locations.
    // Get a pointer to the shader.
    internal_data->colour_shader = shader_system_get("Shader.Builtin.ColourShader3D");
    internal_data->colour_shader_id = internal_data->colour_shader->id;
    internal_data->debug_locations.projection = shader_system_uniform_location(internal_data->colour_shader_id, "projection");
    internal_data->debug_locations.view = shader_system_uniform_location(internal_data->colour_shader_id, "view");
    internal_data->debug_locations.model = shader_system_uniform_location(internal_data->colour_shader_id, "model");

    return true;
}

b8 editor_gizmo_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    // Resolve framebuffer handle via bound source.
    editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
    if (self->sinks[0].bound_source) {
        internal_data->colourbuffer_texture = self->sinks[0].bound_source->value.t;
        self->sources[0].value.t = internal_data->colourbuffer_texture;
        self->sources[0].is_bound = true;
    } else {
        return false;
    }

    return true;
}

b8 editor_gizmo_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
    editor_gizmo* gizmo = internal_data->gizmo;

    renderer_begin_debug_label(self->name, (vec3){0.5f, 1.0f, 0.5});
    if (internal_data->enabled) {
        renderer_begin_rendering(internal_data->renderer, p_frame_data, internal_data->vp.rect, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, k_handle_invalid(), 0);

        // Bind the viewport
        renderer_active_viewport_set(&internal_data->vp);

        shader_system_use_by_id(internal_data->colour_shader->id);

        // Globals
        shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.projection, &internal_data->projection);
        shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.view, &internal_data->view);
        shader_system_apply_global(internal_data->colour_shader_id);

        if (gizmo) {
            editor_gizmo_render_frame_prepare(gizmo, p_frame_data);
        }

        geometry* g = &gizmo->mode_data[gizmo->mode].geo;

        // vec3 camera_pos = camera_position_get(c);
        // vec3 gizmo_pos = transform_position_get(&packet_data->gizmo->xform);
        // TODO: Should get this from the camera/viewport.
        // f32 fov = deg_to_rad(45.0f);
        // f32 dist = vec3_distance(camera_pos, gizmo_pos);

        // NOTE: Use the local transform of the gizmo since it won't ever be parented to anything.
        xform_calculate_local(gizmo->xform_handle);
        mat4 model = xform_local_get(gizmo->xform_handle);
        // f32 fixed_size = 0.1f;                            // TODO: Make this a configurable option for gizmo size.
        f32 scale_scalar = 1.0f;            // ((2.0f * ktan(fov * 0.5f)) * dist) * fixed_size;
        gizmo->scale_scalar = scale_scalar; // Keep a copy of this for hit detection.
        mat4 scale = mat4_scale((vec3){scale_scalar, scale_scalar, scale_scalar});
        model = mat4_mul(model, scale);

        geometry_render_data render_data = {0};
        render_data.model = model;
        render_data.material = g->material;
        render_data.vertex_count = g->vertex_count;
        render_data.vertex_buffer_offset = g->vertex_buffer_offset;
        render_data.index_count = g->index_count;
        render_data.index_buffer_offset = g->index_buffer_offset;
        render_data.unique_id = INVALID_ID;

        // Set model matrix.
        shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.model, &model);
        shader_system_apply_local(internal_data->colour_shader_id);

        // Draw it.
        renderer_geometry_draw(&render_data);

        renderer_end_rendering(internal_data->renderer, p_frame_data);
    }
    renderer_end_debug_label();

    return true;
}

void editor_gizmo_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            kfree(self->internal_data, sizeof(editor_gizmo_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}

b8 editor_gizmo_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v) {
    if (self && self->internal_data) {
        editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->vp = v;
        return true;
    }
    return false;
}

b8 editor_gizmo_rendergraph_node_view_projection_set(struct rendergraph_node* self, mat4 view_matrix, vec3 view_pos, mat4 projection_matrix) {
    if (self && self->internal_data) {
        editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->view = view_matrix;
        internal_data->projection = projection_matrix;
        return true;
    }
    return false;
}

b8 editor_gizmo_rendergraph_node_enabled_set(struct rendergraph_node* self, b8 enabled) {
    if (self && self->internal_data) {
        editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->enabled = enabled;
        return true;
    }
    return false;
}

b8 editor_gizmo_rendergraph_node_gizmo_set(struct rendergraph_node* self, editor_gizmo* gizmo) {
    if (self && self->internal_data) {
        editor_gizmo_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->gizmo = gizmo;
        return true;
    }
    return false;
}

b8 editor_gizmo_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "editor_gizmo";
    factory.create = editor_gizmo_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
