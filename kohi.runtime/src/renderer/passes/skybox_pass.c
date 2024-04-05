#include "skybox_pass.h"

#include "memory/kmemory.h"
#include "logger.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "resources/skybox.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

typedef struct skybox_shader_locations {
    u16 projection_location;
    u16 view_location;
    u16 cube_map_location;
} skybox_shader_locations;

typedef struct skybox_pass_internal_data {
    shader* s;
    skybox_shader_locations locations;
} skybox_pass_internal_data;

b8 skybox_pass_create(struct rendergraph_pass* self, void* config) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(skybox_pass_internal_data), MEMORY_TAG_RENDERER);
    self->pass_data.ext_data = kallocate(sizeof(skybox_pass_extended_data), MEMORY_TAG_RENDERER);

    return true;
}

b8 skybox_pass_initialize(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    skybox_pass_internal_data* internal_data = self->internal_data;

    // Setup the renderpass.
    // Renderpass config - SKYBOX.
    renderpass_config skybox_pass_config = {0};
    skybox_pass_config.name = "Renderpass.Skybox";
    skybox_pass_config.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    skybox_pass_config.clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;
    skybox_pass_config.depth = 1.0f;
    skybox_pass_config.stencil = 0;
    skybox_pass_config.target.attachment_count = 1;
    // TODO: leaking this?
    skybox_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * skybox_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);
    skybox_pass_config.render_target_count = renderer_window_attachment_count_get();

    // Color attachment.
    render_target_attachment_config* skybox_target_colour = &skybox_pass_config.target.attachments[0];
    skybox_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    skybox_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    skybox_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    skybox_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    skybox_target_colour->present_after = false;

    if (!renderer_renderpass_create(&skybox_pass_config, &self->pass)) {
        KERROR("Skybox rendergraph pass - Failed to create skybox renderpass ");
        return false;
    }

    // Load skybox shader.
    const char* skybox_shader_name = "Shader.Builtin.Skybox";
    resource skybox_shader_config_resource;
    if (!resource_system_load(skybox_shader_name, RESOURCE_TYPE_SHADER, 0, &skybox_shader_config_resource)) {
        KERROR("Failed to load skybox shader resource.");
        return false;
    }
    shader_config* skybox_shader_config = (shader_config*)skybox_shader_config_resource.data;
    if (!shader_system_create(&self->pass, skybox_shader_config)) {
        KERROR("Failed to create skybox shader.");
        return false;
    }

    resource_system_unload(&skybox_shader_config_resource);
    // Get a pointer to the shader.
    internal_data->s = shader_system_get(skybox_shader_name);
    internal_data->locations.projection_location = shader_system_uniform_location(internal_data->s, "projection");
    internal_data->locations.view_location = shader_system_uniform_location(internal_data->s, "view");
    internal_data->locations.cube_map_location = shader_system_uniform_location(internal_data->s, "cube_texture");

    return true;
}

b8 skybox_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    skybox_pass_internal_data* internal_data = self->internal_data;

    // Bind the viewport
    renderer_active_viewport_set(self->pass_data.vp);

    if (!renderer_renderpass_begin(&self->pass, &self->pass.targets[p_frame_data->render_target_index])) {
        KERROR("skybox pass failed to start.");
        return false;
    }

    skybox_pass_extended_data* ext_data = self->pass_data.ext_data;
    if (ext_data->sb) {
        shader_system_use_by_id(internal_data->s->id);

        // Get the view matrix, but zero out the position so the skybox stays put on screen.
        mat4 view_matrix = self->pass_data.view_matrix;
        view_matrix.data[12] = 0.0f;
        view_matrix.data[13] = 0.0f;
        view_matrix.data[14] = 0.0f;

        // Apply globals
        renderer_shader_bind_globals(internal_data->s);
        if (!shader_system_uniform_set_by_location(internal_data->locations.projection_location, &self->pass_data.projection_matrix)) {
            KERROR("Failed to apply skybox projection uniform.");
            return false;
        }
        if (!shader_system_uniform_set_by_location(internal_data->locations.view_location, &view_matrix)) {
            KERROR("Failed to apply skybox view uniform.");
            return false;
        }
        shader_system_apply_global(true, p_frame_data);

        // Instance
        shader_system_bind_instance(ext_data->sb->instance_id);
        if (!shader_system_uniform_set_by_location(internal_data->locations.cube_map_location, &ext_data->sb->cubemap)) {
            KERROR("Failed to apply skybox cube map uniform.");
            return false;
        }
        b8 needs_update = ext_data->sb->render_frame_number != p_frame_data->renderer_frame_number || ext_data->sb->draw_index != p_frame_data->draw_index;
        shader_system_apply_instance(needs_update, p_frame_data);

        // Sync the frame number and draw index.
        ext_data->sb->render_frame_number = p_frame_data->renderer_frame_number;
        ext_data->sb->draw_index = p_frame_data->draw_index;

        // Draw it.
        geometry_render_data render_data = {};
        render_data.material = ext_data->sb->g->material;
        render_data.vertex_count = ext_data->sb->g->vertex_count;
        render_data.vertex_element_size = ext_data->sb->g->vertex_element_size;
        render_data.vertex_buffer_offset = ext_data->sb->g->vertex_buffer_offset;
        render_data.index_count = ext_data->sb->g->index_count;
        render_data.index_element_size = ext_data->sb->g->index_element_size;
        render_data.index_buffer_offset = ext_data->sb->g->index_buffer_offset;

        renderer_geometry_draw(&render_data);
    }

    if (!renderer_renderpass_end(&self->pass)) {
        KERROR("skybox pass failed to end.");
        return false;
    }

    return true;
}

void skybox_pass_destroy(struct rendergraph_pass* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(skybox_pass_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}
