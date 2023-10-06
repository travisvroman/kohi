#include "ui_pass.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "math/transform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "resources/ui_text.h"
#include "standard_ui_system.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
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
    shader* s;
    shader* sui_shader;  // standard ui // TODO: different render pass?
    ui_shader_locations locations;
    sui_shader_locations sui_locations;
} ui_pass_internal_data;

b8 ui_pass_create(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(ui_pass_internal_data), MEMORY_TAG_RENDERER);
    self->pass_data.ext_data = kallocate(sizeof(ui_pass_extended_data), MEMORY_TAG_RENDERER);

    return true;
}

b8 ui_pass_initialize(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    ui_pass_internal_data* internal_data = self->internal_data;

    // Renderpass config
    renderpass_config ui_pass_config;
    ui_pass_config.name = "Renderpass.UI";
    ui_pass_config.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    ui_pass_config.clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
    ui_pass_config.depth = 1.0f;
    ui_pass_config.stencil = 0;
    ui_pass_config.target.attachment_count = 1;
    ui_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * ui_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);
    ui_pass_config.render_target_count = renderer_window_attachment_count_get();

    render_target_attachment_config* ui_target_attachment = &ui_pass_config.target.attachments[0];
    // Colour attachment.
    ui_target_attachment->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    ui_target_attachment->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    ui_target_attachment->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    ui_target_attachment->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    ui_target_attachment->present_after = true;

    if (!renderer_renderpass_create(&ui_pass_config, &self->pass)) {
        KERROR("Failed to create UI renderpass.");
        return false;
    }

    // Load the shader.
    const char* shader_name = "Shader.Builtin.UI";
    resource config_resource;
    if (!resource_system_load(shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
        KERROR("Failed to load UI shader resource.");
        return false;
    }
    shader_config* config = (shader_config*)config_resource.data;
    // NOTE: Assuming the first pass since that's all this view has.
    if (!shader_system_create(&self->pass, config)) {
        KERROR("Failed to create UI shader.");
        return false;
    }
    resource_system_unload(&config_resource);

    // Get either the custom shader override or the defined default.
    internal_data->s = shader_system_get(shader_name);
    internal_data->locations.diffuse_map = shader_system_uniform_index(internal_data->s, "diffuse_texture");
    internal_data->locations.properties = shader_system_uniform_index(internal_data->s, "properties");
    internal_data->locations.model = shader_system_uniform_index(internal_data->s, "model");

    // Load the StandardUI shader.
    const char* sui_shader_name = "Shader.StandardUI";
    resource sui_config_resource;
    if (!resource_system_load(sui_shader_name, RESOURCE_TYPE_SHADER, 0, &sui_config_resource)) {
        KERROR("Failed to load StandardUI shader resource.");
        return false;
    }
    shader_config* sui_config = (shader_config*)sui_config_resource.data;
    // NOTE: Assuming the first pass since that's all this view has.
    if (!shader_system_create(&self->pass, sui_config)) {
        KERROR("Failed to create StandardUI shader.");
        return false;
    }
    resource_system_unload(&sui_config_resource);

    // Get either the custom shader override or the defined default.
    internal_data->sui_shader = shader_system_get(sui_shader_name);
    internal_data->sui_locations.projection = shader_system_uniform_index(internal_data->sui_shader, "projection");
    internal_data->sui_locations.view = shader_system_uniform_index(internal_data->sui_shader, "view");
    internal_data->sui_locations.properties = shader_system_uniform_index(internal_data->sui_shader, "properties");
    internal_data->sui_locations.model = shader_system_uniform_index(internal_data->sui_shader, "model");
    internal_data->sui_locations.diffuse_map = shader_system_uniform_index(internal_data->sui_shader, "diffuse_texture");

    return true;
}

b8 ui_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    ui_pass_internal_data* internal_data = self->internal_data;
    ui_pass_extended_data* ext_data = self->pass_data.ext_data;

    // Bind the viewport
    renderer_active_viewport_set(self->pass_data.vp);

    if (!renderer_renderpass_begin(&self->pass, &self->pass.targets[p_frame_data->render_target_index])) {
        KERROR("UI renderpass failed to start.");
        return false;
    }

    if (!shader_system_use_by_id(internal_data->s->id)) {
        KERROR("Failed to use shader. Render frame failed.");
        return false;
    }

    // Apply globals
    if (!material_system_apply_global(internal_data->s->id, p_frame_data, &self->pass_data.projection_matrix, &self->pass_data.view_matrix, 0, 0, 0)) {
        KERROR("Failed to use apply globals for shader. Render frame failed.");
        return false;
    }

    // Draw geometries.
    for (u32 i = 0; i < ext_data->geometry_count; ++i) {
        material* m = 0;
        if (ext_data->geometries[i].geometry->material) {
            m = ext_data->geometries[i].geometry->material;
        } else {
            m = material_system_get_default_ui();
        }

        // Update the material if it hasn't already been this frame. This keeps the
        // same material from being updated multiple times. It still needs to be bound
        // either way, so this check result gets passed to the backend which either
        // updates the internal shader bindings and binds them, or only binds them.
        b8 needs_update = m->render_frame_number != p_frame_data->renderer_frame_number;
        if (!material_system_apply_instance(m, p_frame_data, needs_update)) {
            KWARN("Failed to apply material '%s'. Skipping draw.", m->name);
            continue;
        } else {
            // Sync the frame number.
            m->render_frame_number = p_frame_data->renderer_frame_number;
        }

        // Apply the locals
        material_system_apply_local(m, &ext_data->geometries[i].model);

        // Draw it.
        renderer_geometry_draw(&ext_data->geometries[i]);
    }

    // Draw bitmap text
    for (u32 i = 0; i < ext_data->ui_text_count; ++i) {
        ui_text* text = ext_data->texts[i];
        shader_system_bind_instance(text->instance_id);

        if (!shader_system_uniform_set_by_index(internal_data->locations.diffuse_map, &text->data->atlas)) {
            KERROR("Failed to apply bitmap font diffuse map uniform.");
            return false;
        }

        // TODO: font colour.
        static vec4 white_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};  // white
        if (!shader_system_uniform_set_by_index(internal_data->locations.properties, &white_colour)) {
            KERROR("Failed to apply bitmap font diffuse colour uniform.");
            return false;
        }
        b8 needs_update = text->render_frame_number != p_frame_data->renderer_frame_number || text->draw_index != p_frame_data->draw_index;
        shader_system_apply_instance(needs_update);

        // Sync the frame number and draw index.
        text->render_frame_number = p_frame_data->renderer_frame_number;
        text->draw_index = p_frame_data->draw_index;

        // Apply the locals
        mat4 model = transform_world_get(&text->transform);
        if (!shader_system_uniform_set_by_index(internal_data->locations.model, &model)) {
            KERROR("Failed to apply model matrix for text");
        }

        ui_text_draw(text);
    }

    // Renderables
    if (!shader_system_use_by_id(internal_data->sui_shader->id)) {
        KERROR("Failed to use StandardUI shader. Render frame failed.");
        return false;
    }

    // Apply globals.
    shader_system_uniform_set_by_index(internal_data->sui_locations.projection, &self->pass_data.projection_matrix);
    shader_system_uniform_set_by_index(internal_data->sui_locations.view, &self->pass_data.view_matrix);
    shader_system_apply_global(true);

    // Sync the frame number.
    internal_data->sui_shader->render_frame_number = p_frame_data->renderer_frame_number;

    u32 renderable_count = darray_length(ext_data->sui_render_data.renderables);
    for (u32 i = 0; i < renderable_count; ++i) {
        standard_ui_renderable* renderable = &ext_data->sui_render_data.renderables[i];

        // Apply instance
        b8 needs_update = *renderable->frame_number != p_frame_data->renderer_frame_number || *renderable->draw_index != p_frame_data->draw_index;
        shader_system_bind_instance(*renderable->instance_id);
        // NOTE: Expand this to a structure if more data is needed.
        shader_system_uniform_set_by_index(internal_data->sui_locations.properties, &renderable->render_data.diffuse_colour);
        shader_system_uniform_set_by_index(internal_data->sui_locations.diffuse_map, ext_data->sui_render_data.ui_atlas);
        shader_system_apply_instance(needs_update);

        // Sync the frame number.
        *renderable->frame_number = p_frame_data->renderer_frame_number;
        *renderable->draw_index = p_frame_data->draw_index;

        // Apply local
        shader_system_uniform_set_by_index(internal_data->sui_locations.model, &renderable->render_data.model);

        // Draw
        renderer_geometry_draw(&renderable->render_data);
    }

    if (!renderer_renderpass_end(&self->pass)) {
        KERROR("UI renderpass failed to end.");
        return false;
    }

    return true;
}

void ui_pass_destroy(struct rendergraph_pass* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(ui_pass_internal_data), MEMORY_TAG_RENDERER);
        }
    }
}
