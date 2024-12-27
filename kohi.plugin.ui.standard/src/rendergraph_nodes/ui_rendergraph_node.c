#include "ui_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "standard_ui_system.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/shader_system.h"
#include <standard_ui_defines.h>

typedef struct sui_shader_locations {
    u16 sui_frame_ubo;
    u16 sui_group_ubo;
    u16 atlas_texture;
    u16 atlas_sampler;
    u16 sui_draw_ubo;
} sui_shader_locations;

typedef struct sui_per_frame_ubo {
    mat4 projection;
    mat4 view;
} sui_per_frame_ubo;

typedef struct sui_per_group_ubo {
    vec4 diffuse_colour;
} sui_per_group_ubo;

typedef struct sui_per_draw_ubo {
    mat4 model;
} sui_per_draw_ubo;

typedef struct ui_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    khandle sui_shader; // standard ui // TODO: different render pass?
    sui_shader_locations sui_locations;

    kresource_texture* colourbuffer_texture;
    kresource_texture* depthbuffer_texture;
    kresource_texture* ui_atlas;
    standard_ui_render_data render_data;

    viewport vp;
    mat4 view;
    mat4 projection;
} ui_rendergraph_node_internal_data;

b8 ui_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const rendergraph_node_config* config) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(ui_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    ui_rendergraph_node_internal_data* internal_data = self->internal_data;

    internal_data->renderer = engine_systems_get()->renderer_system;

    self->name = string_duplicate(config->name);

    // Two sinks, one for colour and one for depth.
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
        KERROR("UI rendergraph node requires configuration for sink called 'colourbuffer'.");
        return false;
    }

    if (depthbuffer_sink_config) {
        // Setup the depthbuffer sink.
        rendergraph_sink* depthbuffer_sink = &self->sinks[1];
        depthbuffer_sink->name = string_duplicate("depthbuffer");
        depthbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        depthbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        depthbuffer_sink->configured_source_name = string_duplicate(depthbuffer_sink_config->source_name);
    } else {
        KERROR("UI rendergraph node requires configuration for sink called 'depthbuffer'.");
        return false;
    }

    // Two sources, one for colour and the second for depth/stencil.
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

    ui_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load the StandardUI shader.

    // Get either the custom shader override or the defined default.
    internal_data->sui_shader = shader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));
    internal_data->sui_locations.sui_frame_ubo = shader_system_uniform_location(internal_data->sui_shader, kname_create("sui_frame_ubo"));
    internal_data->sui_locations.sui_group_ubo = shader_system_uniform_location(internal_data->sui_shader, kname_create("sui_group_ubo"));
    internal_data->sui_locations.atlas_texture = shader_system_uniform_location(internal_data->sui_shader, kname_create("atlas_texture"));
    internal_data->sui_locations.atlas_sampler = shader_system_uniform_location(internal_data->sui_shader, kname_create("atlas_sampler"));
    internal_data->sui_locations.sui_draw_ubo = shader_system_uniform_location(internal_data->sui_shader, kname_create("sui_draw_ubo"));

    return true;
}

b8 ui_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    // Resolve framebuffer handle via bound source.
    ui_rendergraph_node_internal_data* internal_data = self->internal_data;
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

b8 ui_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    ui_rendergraph_node_internal_data* internal_data = self->internal_data;

    renderer_begin_debug_label(self->name, (vec3){0.5f, 0.5f, 0.5});

    renderer_begin_rendering(internal_data->renderer, p_frame_data, internal_data->vp.rect, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, internal_data->depthbuffer_texture->renderer_texture_handle, 0);

    // Bind the viewport
    renderer_active_viewport_set(&internal_data->vp);

    // Set various state overrides.
    renderer_set_depth_test_enabled(false);
    renderer_set_depth_write_enabled(false);

    // Renderables
    if (!shader_system_use(internal_data->sui_shader)) {
        KERROR("Failed to use StandardUI shader. Render frame failed.");
        return false;
    }

    // Apply per-frame data.
    {
        shader_system_bind_frame(internal_data->sui_shader);
        sui_per_frame_ubo frame_ubo_data = {0};
        frame_ubo_data.projection = internal_data->projection;
        frame_ubo_data.view = internal_data->view;
        shader_system_uniform_set_by_location(internal_data->sui_shader, internal_data->sui_locations.sui_frame_ubo, &frame_ubo_data);
        shader_system_apply_per_frame(internal_data->sui_shader);
    }

    u32 renderable_count = darray_length(internal_data->render_data.renderables);
    for (u32 i = 0; i < renderable_count; ++i) {
        standard_ui_renderable* renderable = &internal_data->render_data.renderables[i];

        // Render clipping mask geometry if it exists.
        if (renderable->clip_mask_render_data) {
            renderer_begin_debug_label("clip_mask", (vec3){0, 1, 0});
            // Enable writing, disable test.
            renderer_set_stencil_test_enabled(true);
            renderer_set_depth_test_enabled(false);
            renderer_set_depth_write_enabled(false);
            renderer_set_stencil_reference((u32)renderable->clip_mask_render_data->unique_id);
            renderer_set_stencil_write_mask(0xFF);
            renderer_set_stencil_op(
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_STENCIL_OP_REPLACE,
                RENDERER_COMPARE_OP_ALWAYS);

            renderer_clear_depth_set(internal_data->renderer, 1.0f);
            renderer_clear_stencil_set(internal_data->renderer, 0.0f);

            {
                shader_system_bind_draw_id(internal_data->sui_shader, *renderable->per_draw_id);
                sui_per_draw_ubo draw_data = {0};
                draw_data.model = renderable->clip_mask_render_data->model;
                shader_system_uniform_set_by_location(internal_data->sui_shader, internal_data->sui_locations.sui_draw_ubo, &draw_data);
                shader_system_apply_per_draw(internal_data->sui_shader);
            }

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
            renderer_end_debug_label();
        } else {
            renderer_set_stencil_write_mask(0x00);
            renderer_set_stencil_test_enabled(false);
        }

        // Apply group
        // LEFTOFF: try eliminating the group and just putting the diffuse_colour in the per-draw instead (where it probably should be anyway).
        // Will need to remove group references from the sui controls.
        {
            shader_system_bind_group(internal_data->sui_shader, *renderable->group_id);
            // Set UBO data
            sui_per_group_ubo group_data = {0};
            group_data.diffuse_colour = renderable->render_data.diffuse_colour;
            shader_system_uniform_set_by_location(internal_data->sui_shader, internal_data->sui_locations.sui_group_ubo, &group_data);
            // Atlas texture
            kresource_texture* atlas = renderable->atlas_override ? renderable->atlas_override : internal_data->ui_atlas;
            shader_system_uniform_set_by_location(internal_data->sui_shader, internal_data->sui_locations.atlas_texture, atlas);

            shader_system_apply_per_group(internal_data->sui_shader);
        }

        // Apply per-draw
        {
            shader_system_bind_draw_id(internal_data->sui_shader, *renderable->per_draw_id);
            sui_per_draw_ubo draw_data = {0};
            draw_data.model = renderable->render_data.model;
            shader_system_uniform_set_by_location(internal_data->sui_shader, internal_data->sui_locations.sui_draw_ubo, &draw_data);
            shader_system_apply_per_draw(internal_data->sui_shader);
        }

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

    renderer_end_debug_label();

    return true;
}

void ui_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            kfree(self->internal_data, sizeof(ui_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
        }
    }
}

void ui_rendergraph_node_set_atlas(struct rendergraph_node* self, kresource_texture* atlas) {
    if (self) {
        ui_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->ui_atlas = atlas;
    }
}

void ui_rendergraph_node_set_render_data(struct rendergraph_node* self, standard_ui_render_data render_data) {
    if (self) {
        ui_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->render_data = render_data;
    }
}

void ui_rendergraph_node_set_viewport_and_matrices(struct rendergraph_node* self, viewport vp, mat4 view, mat4 projection) {
    if (self) {
        if (self->internal_data) {
            ui_rendergraph_node_internal_data* internal_data = self->internal_data;
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
