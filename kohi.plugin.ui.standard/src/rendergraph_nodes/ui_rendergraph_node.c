#include "ui_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "standard_ui_system.h"
#include "strings/kstring.h"
#include "systems/shader_system.h"

typedef struct ui_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
    u16 diffuse_map;
    u16 properties;
} ui_shader_locations;

typedef struct sui_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
    u16 properties;
    u16 diffuse_map;
} sui_shader_locations;

typedef struct ui_pass_internal_data {
    struct renderer_system_state* renderer;
    u32 shader_id;
    shader* sui_shader; // standard ui // TODO: different render pass?
    sui_shader_locations sui_locations;

    struct texture* colourbuffer_texture;
    struct texture_map* ui_atlas;
    standard_ui_render_data render_data;

    struct viewport* vp;
    mat4 view;
    mat4 projection;
} ui_pass_internal_data;

b8 ui_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const rendergraph_node_config* config) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(ui_pass_internal_data), MEMORY_TAG_RENDERER);
    ui_pass_internal_data* internal_data = self->internal_data;

    internal_data->renderer = engine_systems_get()->renderer_system;

    self->name = string_duplicate(config->name);

    // Has one sink, for the colourbuffer.
    self->sink_count = 1;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    rendergraph_node_sink_config* sink_config = 0;
    for (u32 i = 0; i < config->sink_count; ++i) {
        rendergraph_node_sink_config* sink = &config->sinks[i];
        if (strings_equali("colourbuffer", sink->name)) {
            sink_config = sink;
            break;
        }
    }

    if (sink_config) {
        // Setup the colourbuffer sink.
        rendergraph_sink* colourbuffer_sink = &self->sinks[0];
        colourbuffer_sink->name = string_duplicate("colourbuffer");
        colourbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        colourbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        colourbuffer_sink->configured_source_name = string_duplicate(sink_config->source_name);
    } else {
        KERROR("UI rendergraph node requires configuration for sink called 'colourbuffer'.");
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
    self->initialize = ui_rendergraph_node_initialize;
    self->load_resources = ui_rendergraph_node_load_resources;
    self->destroy = ui_rendergraph_node_destroy;
    self->execute = ui_rendergraph_node_execute;

    return true;
}

b8 ui_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    ui_pass_internal_data* internal_data = self->internal_data;

    // Load the StandardUI shader.

    // Get either the custom shader override or the defined default.
    internal_data->sui_shader = shader_system_get("Shader.StandardUI");
    internal_data->shader_id = internal_data->sui_shader->id;
    internal_data->sui_locations.projection = shader_system_uniform_location(internal_data->shader_id, "projection");
    internal_data->sui_locations.view = shader_system_uniform_location(internal_data->shader_id, "view");
    internal_data->sui_locations.properties = shader_system_uniform_location(internal_data->shader_id, "properties");
    internal_data->sui_locations.model = shader_system_uniform_location(internal_data->shader_id, "model");
    internal_data->sui_locations.diffuse_map = shader_system_uniform_location(internal_data->shader_id, "diffuse_texture");

    return true;
}

b8 ui_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    // Resolve framebuffer handle via bound source.
    ui_pass_internal_data* internal_data = self->internal_data;
    if (self->sinks[0].bound_source) {
        internal_data->colourbuffer_texture = self->sinks[0].bound_source->value.t;
        return true;
    }

    return false;
}

b8 ui_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    ui_pass_internal_data* internal_data = self->internal_data;

    // Bind the viewport
    renderer_active_viewport_set(internal_data->vp);

    renderer_set_depth_test_enabled(false);

    renderer_begin_rendering(internal_data->renderer, p_frame_data, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, k_handle_invalid());

    // Renderables
    if (!shader_system_use_by_id(internal_data->sui_shader->id)) {
        KERROR("Failed to use StandardUI shader. Render frame failed.");
        return false;
    }

    // Apply globals.

    shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.projection, &internal_data->projection);
    shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.view, &internal_data->view);
    shader_system_apply_global(internal_data->shader_id);

    u32 renderable_count = darray_length(internal_data->render_data.renderables);
    for (u32 i = 0; i < renderable_count; ++i) {
        standard_ui_renderable* renderable = &internal_data->render_data.renderables[i];

        // Render clipping mask geometry if it exists.
        if (renderable->clip_mask_render_data) {
            // Enable writing, disable test.
            renderer_set_stencil_test_enabled(true);
            renderer_set_depth_test_enabled(false);
            renderer_set_stencil_reference((u32)renderable->clip_mask_render_data->unique_id);
            renderer_set_stencil_write_mask(0xFF);
            renderer_set_stencil_op(
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_COMPARE_OP_ALWAYS);

            shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.model, &renderable->clip_mask_render_data->model);
            shader_system_apply_local(internal_data->shader_id);
            // Draw the clip mask geometry.
            renderer_geometry_draw(renderable->clip_mask_render_data);

            // Disable writing, enable test.
            renderer_set_stencil_write_mask(0x00);
            renderer_set_stencil_test_enabled(true);
            renderer_set_stencil_compare_mask(0xFF);
            renderer_set_stencil_op(
                RENDERER_STENCIL_OP_KEEP,
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_STENCIL_OP_KEEP,
                RENDERER_COMPARE_OP_EQUAL);
        } else {
            renderer_set_stencil_write_mask(0x00);
            renderer_set_stencil_test_enabled(false);
        }

        // Apply instance
        shader_system_bind_instance(internal_data->shader_id, *renderable->instance_id);
        // NOTE: Expand this to a structure if more data is needed.
        shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.properties, &renderable->render_data.diffuse_colour);
        texture_map* atlas = renderable->atlas_override ? renderable->atlas_override : internal_data->ui_atlas;
        shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.diffuse_map, atlas);
        shader_system_apply_instance(internal_data->shader_id);

        // Apply local
        shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->sui_locations.model, &renderable->render_data.model);
        shader_system_apply_local(internal_data->shader_id);

        // Draw
        renderer_geometry_draw(&renderable->render_data);

        // Turn off stencil tests if they were on.
        if (renderable->clip_mask_render_data) {
            // Turn off stencil testing.
            renderer_set_stencil_test_enabled(false);
            renderer_set_stencil_op(
                RENDERER_STENCIL_OP_KEEP,
                RENDERER_STENCIL_OP_KEEP,
                RENDERER_STENCIL_OP_KEEP,
                RENDERER_COMPARE_OP_ALWAYS);
        }
    }

    renderer_end_rendering(internal_data->renderer, p_frame_data);

    return true;
}

void ui_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            kfree(self->internal_data, sizeof(ui_pass_internal_data), MEMORY_TAG_RENDERER);
        }
    }
}

void ui_rendergraph_node_set_atlas(struct rendergraph_node* self, texture_map* atlas) {
    if (self) {
        ui_pass_internal_data* internal_data = self->internal_data;
        internal_data->ui_atlas = atlas;
    }
}

void ui_rendergraph_node_set_render_data(struct rendergraph_node* self, standard_ui_render_data render_data) {
    if (self) {
        ui_pass_internal_data* internal_data = self->internal_data;
        internal_data->render_data = render_data;
    }
}

void ui_rendergraph_node_set_viewport_and_matrices(struct rendergraph_node* self, struct viewport* vp, mat4 view, mat4 projection) {
    if (self) {
        if (self->internal_data) {
            ui_pass_internal_data* internal_data = self->internal_data;
            internal_data->vp = vp;
            internal_data->view = view;
            internal_data->projection = projection;
        }
    }
}

b8 ui_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "standard_ui";
    factory.create = ui_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
