#include "scene_pass.h"

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

typedef struct scene_pass_internal_data {
    shader* material_shader;
    shader* terrain_shader;
    shader* colour_shader;
    debug_shader_locations debug_locations;
} scene_pass_internal_data;

b8 scene_pass_create(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(scene_pass_internal_data), MEMORY_TAG_RENDERER);

    return true;
}

b8 scene_pass_initialize(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }

    scene_pass_internal_data* internal_data = self->internal_data;

    // Renderpass config - scene.
    renderpass_config scene_pass_config = {0};
    scene_pass_config.name = "Renderpass.World";
    scene_pass_config.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    scene_pass_config.clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
    scene_pass_config.depth = 1.0f;
    scene_pass_config.stencil = 0;
    scene_pass_config.target.attachment_count = 2;
    scene_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * scene_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);
    scene_pass_config.render_target_count = renderer_window_attachment_count_get();

    // Colour attachment
    render_target_attachment_config* scene_target_colour = &scene_pass_config.target.attachments[0];
    scene_target_colour->type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    scene_target_colour->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    scene_target_colour->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    scene_target_colour->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    scene_target_colour->present_after = false;

    // Depth attachment
    render_target_attachment_config* scene_target_depth = &scene_pass_config.target.attachments[1];
    scene_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    scene_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    scene_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    scene_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    scene_target_depth->present_after = false;

    if (!renderer_renderpass_create(&scene_pass_config, &self->pass)) {
        KERROR("Failed to create scene renderpass ");
        return false;
    }

    // Load material shader
    const char* material_shader_name = "Shader.Builtin.Material";
    resource material_config_resource;
    if (!resource_system_load(material_shader_name, RESOURCE_TYPE_SHADER, 0, &material_config_resource)) {
        KERROR("Failed to load material shader resource.");
        return false;
    }
    shader_config* config = (shader_config*)material_config_resource.data;
    if (!shader_system_create(&self->pass, config)) {
        KERROR("Failed to create material shader.");
        return false;
    }
    resource_system_unload(&material_config_resource);
    // Save off a pointer to the material shader.
    internal_data->material_shader = shader_system_get(material_shader_name);

    // Load terrain shader.
    const char* terrain_shader_name = "Shader.Builtin.Terrain";
    resource terrain_shader_config_resource;
    if (!resource_system_load(terrain_shader_name, RESOURCE_TYPE_SHADER, 0, &terrain_shader_config_resource)) {
        KERROR("Failed to load terrain shader resource.");
        return false;
    }
    shader_config* terrain_shader_config = (shader_config*)terrain_shader_config_resource.data;
    if (!shader_system_create(&self->pass, terrain_shader_config)) {
        KERROR("Failed to create terrain shader.");
        return false;
    }
    resource_system_unload(&terrain_shader_config_resource);
    // Save off a pointer to the terrain shader.
    internal_data->terrain_shader = shader_system_get(terrain_shader_name);

    // Load debug colour3d shader.
    const char* colour3d_shader_name = "Shader.Builtin.ColourShader3D";
    resource colour3d_shader_config_resource;
    if (!resource_system_load(colour3d_shader_name, RESOURCE_TYPE_SHADER, 0, &colour3d_shader_config_resource)) {
        KERROR("Failed to load colour3d shader resource.");
        return false;
    }
    shader_config* colour3d_shader_config = (shader_config*)colour3d_shader_config_resource.data;
    if (!shader_system_create(&self->pass, colour3d_shader_config)) {
        KERROR("Failed to create colour3d shader.");
        return false;
    }
    resource_system_unload(&colour3d_shader_config_resource);

    // Save off a pointer to the colour shader.
    internal_data->colour_shader = shader_system_get(colour3d_shader_name);
    // Get colour3d shader uniform locations.
    {
        internal_data->debug_locations.projection = shader_system_uniform_index(internal_data->colour_shader, "projection");
        internal_data->debug_locations.view = shader_system_uniform_index(internal_data->colour_shader, "view");
        internal_data->debug_locations.model = shader_system_uniform_index(internal_data->colour_shader, "model");
    }

    return true;
}

b8 scene_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    if (!renderer_renderpass_begin(&self->pass, &self->pass.targets[p_frame_data->render_target_index])) {
        KERROR("scene pass failed to start.");
        return false;
    }

    scene_pass_internal_data* internal_data = self->internal_data;
    scene_pass_extended_data* ext_data = self->pass_data.ext_data;

    // Use the appropriate shader and apply the global uniforms.
    u32 terrain_count = ext_data->terrain_geometry_count;
    if (terrain_count > 0) {
        shader_system_use_by_id(internal_data->terrain_shader->id);

        // Apply globals
        if (!material_system_apply_global(internal_data->terrain_shader->id, p_frame_data, &self->pass_data.projection_matrix, &self->pass_data.view_matrix, &ext_data->ambient_colour, &self->pass_data.view_position, ext_data->render_mode)) {
            KERROR("Failed to use apply globals for terrain shader. Render frame failed.");
            return false;
        }

        for (u32 i = 0; i < terrain_count; ++i) {
            material* m = 0;
            if (ext_data->terrain_geometries[i].geometry->material) {
                m = ext_data->terrain_geometries[i].geometry->material;
            } else {
                m = material_system_get_default_terrain();
            }

            // Update the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times. It still needs to be bound
            // either way, so this check result gets passed to the backend which either
            // updates the internal shader bindings and binds them, or only binds them.
            // Also need to check against the renderer draw index.
            b8 needs_update = m->render_frame_number != p_frame_data->renderer_frame_number || m->render_draw_index != p_frame_data->draw_index;
            if (!material_system_apply_instance(m, p_frame_data, needs_update)) {
                KWARN("Failed to apply terrain material '%s'. Skipping draw.", m->name);
                continue;
            } else {
                // Sync the frame number and draw index.
                m->render_frame_number = p_frame_data->renderer_frame_number;
                m->render_draw_index = p_frame_data->draw_index;
            }

            // Apply the locals
            material_system_apply_local(m, &ext_data->terrain_geometries[i].model);

            // Draw it.
            renderer_geometry_draw(&ext_data->terrain_geometries[i]);
        }
    }

    // Static geometries.
    u32 geometry_count = ext_data->geometry_count;
    if (geometry_count > 0) {
        if (!shader_system_use_by_id(internal_data->material_shader->id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        // TODO: Find a generic way to request data such as ambient colour (which should be from a scene),
        // and mode (from the renderer)
        if (!material_system_apply_global(internal_data->material_shader->id, p_frame_data, &self->pass_data.projection_matrix, &self->pass_data.view_matrix, &ext_data->ambient_colour, &self->pass_data.view_position, ext_data->render_mode)) {
            KERROR("Failed to use apply globals for material shader. Render frame failed.");
            return false;
        }

        // Draw geometries.
        u32 count = ext_data->geometry_count;
        for (u32 i = 0; i < count; ++i) {
            material* m = 0;
            if (ext_data->geometries[i].geometry->material) {
                m = ext_data->geometries[i].geometry->material;
            } else {
                m = material_system_get_default();
            }

            // Update the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times. It still needs to be bound
            // either way, so this check result gets passed to the backend which either
            // updates the internal shader bindings and binds them, or only binds them.
            // Also need to check against the draw index.
            b8 needs_update = m->render_frame_number != p_frame_data->renderer_frame_number || m->render_draw_index != p_frame_data->draw_index;
            if (!material_system_apply_instance(m, p_frame_data, needs_update)) {
                KWARN("Failed to apply material '%s'. Skipping draw.", m->name);
                continue;
            } else {
                // Sync the frame number and draw index.
                m->render_frame_number = p_frame_data->renderer_frame_number;
                m->render_draw_index = p_frame_data->draw_index;
            }

            // Apply the locals
            material_system_apply_local(m, &ext_data->geometries[i].model);

            // Invert if needed
            if (ext_data->geometries[i].winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
            }

            // Draw it.
            renderer_geometry_draw(&ext_data->geometries[i]);

            // Change back if needed
            if (ext_data->geometries[i].winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
            }
        }
    }

    // Debug geometries (i.e. grids, lines, boxes, gizmos, etc.)
    // This goes through the same geometry system as anything else.
    u32 debug_geometry_count = ext_data->debug_geometry_count;
    if (debug_geometry_count > 0) {
        shader_system_use_by_id(internal_data->colour_shader->id);

        // Globals
        shader_system_uniform_set_by_index(internal_data->debug_locations.projection, &self->pass_data.projection_matrix);
        shader_system_uniform_set_by_index(internal_data->debug_locations.view, &self->pass_data.view_matrix);

        shader_system_apply_global(true);

        // Each geometry.
        for (u32 i = 0; i < debug_geometry_count; ++i) {
            // NOTE: No instance-level uniforms to be set.

            // Local
            shader_system_uniform_set_by_index(internal_data->debug_locations.model, &ext_data->debug_geometries[i].model);

            // Draw it.
            renderer_geometry_draw(&ext_data->debug_geometries[i]);
        }

        // HACK: This should be handled somehow, every frame, by the shader system.
        internal_data->colour_shader->render_frame_number = p_frame_data->renderer_frame_number;
    }

    if (!renderer_renderpass_end(&self->pass)) {
        KERROR("scene pass failed to end.");
        return false;
    }

    return true;
}

void scene_pass_destroy(struct rendergraph_pass* self) {
    if (self) {
        if (self->internal_data) {
            kfree(self->internal_data, sizeof(scene_pass_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}
