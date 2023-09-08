#include "editor_pass.h"

#include "core/kmemory.h"
#include "core/logger.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

typedef struct debug_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_shader_locations;

typedef struct editor_pass_internal_data {
    shader* colour_shader;
    debug_shader_locations debug_locations;
} editor_pass_internal_data;

b8 editor_pass_create(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(editor_pass_internal_data), MEMORY_TAG_RENDERER);

    return true;
}

b8 editor_pass_initialize(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    editor_pass_internal_data* internal_data = self->internal_data;

    // Renderpass config.
    renderpass_config editor_pass_config = {0};
    editor_pass_config.name = "Renderpass.Testbed.EditorWorld";
    editor_pass_config.clear_colour = (vec4){0.0f, 0.0f, 0.0f, 1.0f};
    editor_pass_config.clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
    editor_pass_config.depth = 1.0f;
    editor_pass_config.stencil = 0;
    editor_pass_config.target.attachment_count = 2;
    editor_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * editor_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);
    editor_pass_config.render_target_count = renderer_window_attachment_count_get();

    // Colour attachment
    render_target_attachment_config* editor_target_colour = &editor_pass_config.target.attachments[0];
    editor_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    editor_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    editor_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    editor_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    editor_target_colour->present_after = false;

    // Depth attachment
    render_target_attachment_config* editor_target_depth = &editor_pass_config.target.attachments[1];
    editor_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    editor_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    editor_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    editor_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    editor_target_depth->present_after = false;

    if (!renderer_renderpass_create(&editor_pass_config, &self->pass)) {
        KERROR("Failed to create editor renderpass.");
        return false;
    }

    // Load debug colour3d shader and get shader uniform locations.
    const char* colour3d_shader_name = "Shader.Builtin.ColourShader3D";
    internal_data->colour_shader = shader_system_get(colour3d_shader_name);
    if (!internal_data->colour_shader) {
        KERROR("Unable to get colour3d shader!");
        return false;
    }
    internal_data->debug_locations.projection = shader_system_uniform_index(internal_data->colour_shader, "projection");
    internal_data->debug_locations.view = shader_system_uniform_index(internal_data->colour_shader, "view");
    internal_data->debug_locations.model = shader_system_uniform_index(internal_data->colour_shader, "model");

    return true;
}

b8 editor_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    editor_pass_internal_data* internal_data = self->internal_data;
    editor_pass_extended_data* ext_data = self->pass_data.ext_data;

    // Bind the viewport
    renderer_active_viewport_set(self->pass_data.vp);

    if (!renderer_renderpass_begin(&self->pass, &self->pass.targets[p_frame_data->render_target_index])) {
        KERROR("editor renderpass failed to start.");
        return false;
    }

    shader_system_use_by_id(internal_data->colour_shader->id);

    renderer_shader_bind_globals(internal_data->colour_shader);
    // Globals
    b8 needs_update = p_frame_data->renderer_frame_number != internal_data->colour_shader->render_frame_number || internal_data->colour_shader->draw_index != p_frame_data->draw_index;
    if (needs_update) {
        shader_system_uniform_set_by_index(internal_data->debug_locations.projection, &self->pass_data.projection_matrix);
        shader_system_uniform_set_by_index(internal_data->debug_locations.view, &self->pass_data.view_matrix);
    }
    shader_system_apply_global(needs_update);

    // Sync frame number and draw index.
    internal_data->colour_shader->render_frame_number = p_frame_data->renderer_frame_number;
    internal_data->colour_shader->draw_index = p_frame_data->draw_index;

    for (u32 i = 0; i < ext_data->debug_geometry_count; ++i) {
        // NOTE: No instance-level uniforms to be set.
        geometry_render_data* render_data = &ext_data->debug_geometries[i];

        // Set model matrix.
        shader_system_uniform_set_by_index(internal_data->debug_locations.model, &render_data->model);

        // Draw it.
        renderer_geometry_draw(render_data);
    }

    if (!renderer_renderpass_end(&self->pass)) {
        KERROR("editor renderpass failed to end.");
        return false;
    }

    return true;
}

void editor_pass_destroy(struct rendergraph_pass* self) {
    if (self) {
        if (self->internal_data) {
            kfree(self->internal_data, sizeof(editor_pass_internal_data), MEMORY_TAG_RENDERER);
        }
    }
}
