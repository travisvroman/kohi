#include "skybox_rendergraph_node.h"

#include "containers/darray.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
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

    // The internal renderer renderpass.
    renderpass internal_pass;

    rendergraph_source* colourbuffer_source;

    skybox* sb;
} skybox_pass_internal_data;

b8 skybox_rendergraph_node_create(struct rendergraph_node* self, const rendergraph_pass_config* config) {
    if (!self) {
        KERROR("skybox_pass_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("skybox_pass_create requires a valid configuration.");
        return false;
    }

    // Take a copy of the config.
    self->config = kallocate(sizeof(rendergraph_pass_config), MEMORY_TAG_RENDERER);
    kcopy_memory(self->config, config, sizeof(rendergraph_pass_config));

    // Setup internal data.
    self->internal_data = kallocate(sizeof(skybox_pass_internal_data), MEMORY_TAG_RENDERER);
    skybox_pass_internal_data* internal_data = self->internal_data;

    // LEFTOFF:
    // - Change rendergraph to gather required resources at the beginning
    //   of a frame (i.e. global.colourbuffer from current window's render target).
    // - Convert rendergraph passes to use configured sinks.
    // - Remove public functions to add sources, sinks and set sink linkages. This will be done via config.
    // - Remove specialized rendergraphs, will be replaced by "templates" (forward, editor, etc.)
    // - Remove all public functions from rendergraph passes regarding recreation of attachments.
    //   These should be handled internally.
    // - Remove concept of source render target types (i.e colour or depth) - not really needed.
    // - Remove concept of source origin (i.e. self, global, etc.)
    // - Add logic to resolve graph dependencies once all passes have been setup to resolve
    //   source/sink linkage by lookup (i.e '<pass_name>.<source_name>').
    // - Sources should automatically be added internally by the rendergraph pass, not externally
    //   as is done currently in specialized graphs.
    // - Rename rendergraph_pass -> rendergraph_node.

    // Add source(s), typically to be used as outputs for other passes to pick up.
    rendergraph_source source_colourbuffer = {0};
    source_colourbuffer.name = "colourbuffer";
    source_colourbuffer.type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    darray_push(self->sources, source_colourbuffer);

    // Keep an internal convenience pointer to the source.
    internal_data->colourbuffer_source = &self->sources[0];

    return true;
}

b8 skybox_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    skybox_pass_internal_data* internal_data = self->internal_data;

    // Setup the renderpass.
    renderpass_config skybox_pass_config = {0};
    skybox_pass_config.name = "Renderpass.Skybox";
    skybox_pass_config.clear_flags = 0; // NOTE: This pass no longer responsible for clears. // RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;
    skybox_pass_config.target.attachment_count = 1;
    // TODO: leaking this?
    skybox_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * skybox_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);

    // Color attachment.
    render_target_attachment_config* skybox_target_colour = &skybox_pass_config.target.attachments[0];
    skybox_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    skybox_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;

    // Only need to store if something is bound to the output source.
    if (internal_data->colourbuffer_source->is_bound) {
        skybox_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
        if (self->presents_colour) {
            skybox_target_colour->post_pass_use = RENDER_TARGET_ATTACHMENT_USE_COLOUR_PRESENT;
        } else {
            skybox_target_colour->post_pass_use = RENDER_TARGET_ATTACHMENT_USE_COLOUR_ATTACHMENT;
        }
    } else {
        skybox_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_DONT_CARE;
    }

    if (!renderer_renderpass_create(&skybox_pass_config, &internal_data->internal_pass)) {
        KERROR("Skybox rendergraph pass - Failed to create skybox renderpass ");
        return false;
    }

    // Render target using attachments.
    // TODO: Use bound colourbuffer size.
    renderer_render_target_create(
        target->attachment_count,
        target->attachments,
        &pass->pass,
        use_custom_size ? target->attachments[0].texture->width : width,
        use_custom_size ? target->attachments[0].texture->height : height,
        0,
        &pass->owned_render_targets[i]);

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

b8 skybox_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    skybox_pass_internal_data* internal_data = self->internal_data;

    // Bind the viewport
    renderer_active_viewport_set(self->pass_data.vp);

    // FIXME: How does this resolve it's global render target?

    if (!renderer_renderpass_begin(&self->pass, &self->pass.target)) {
        KERROR("skybox pass failed to start.");
        return false;
    }

    if (internal_data->sb) {
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
        shader_system_bind_instance(internal_data->sb->instance_id);
        if (!shader_system_uniform_set_by_location(internal_data->locations.cube_map_location, &internal_data->sb->cubemap)) {
            KERROR("Failed to apply skybox cube map uniform.");
            return false;
        }

        // FIXME: This likely will break things.
        b8 needs_update = true; // ext_data->sb->render_frame_number != p_frame_data->renderer_frame_number || ext_data->sb->draw_index != p_frame_data->draw_index;
        shader_system_apply_instance(needs_update, p_frame_data);

        // Draw it.
        geometry_render_data render_data = {};
        render_data.material = internal_data->sb->g->material;
        render_data.vertex_count = internal_data->sb->g->vertex_count;
        render_data.vertex_element_size = internal_data->sb->g->vertex_element_size;
        render_data.vertex_buffer_offset = internal_data->sb->g->vertex_buffer_offset;
        render_data.index_count = internal_data->sb->g->index_count;
        render_data.index_element_size = internal_data->sb->g->index_element_size;
        render_data.index_buffer_offset = internal_data->sb->g->index_buffer_offset;

        renderer_geometry_draw(&render_data);
    }

    if (!renderer_renderpass_end(&self->pass)) {
        KERROR("skybox pass failed to end.");
        return false;
    }

    return true;
}

void skybox_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(skybox_pass_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}

void skybox_rendergraph_node_set_skybox(struct rendergraph_node* self, struct skybox* sb) {
    if (self) {
        skybox_pass_internal_data* internal_data = self->internal_data;
        internal_data->sb = sb;
    }
}
