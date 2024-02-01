#include "scene_pass.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"
#include "renderer/passes/shadow_map_pass.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "resources/resource_types.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

typedef struct debug_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_shader_locations;

typedef struct scene_pass_internal_data {
    shader* pbr_shader;
    shader* terrain_shader;
    shader* colour_shader;
    debug_shader_locations debug_locations;

    rendergraph_source* shadowmap_source;
    // One per frame.
    u32 frame_count;

    // One per frame.
    texture_map* shadow_maps;
} scene_pass_internal_data;

b8 scene_pass_create(struct rendergraph_pass* self, void* config) {
    if (!self) {
        return false;
    }

    self->internal_data = kallocate(sizeof(scene_pass_internal_data), MEMORY_TAG_RENDERER);
    self->pass_data.ext_data = kallocate(sizeof(scene_pass_extended_data), MEMORY_TAG_RENDERER);

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

    // Load PBR shader
    const char* pbr_shader_name = "Shader.PBRMaterial";
    resource pbr_config_resource;
    if (!resource_system_load(pbr_shader_name, RESOURCE_TYPE_SHADER, 0, &pbr_config_resource)) {
        KERROR("Failed to load PBR shader resource.");
        return false;
    }
    shader_config* pbr_config = (shader_config*)pbr_config_resource.data;
    if (!shader_system_create(&self->pass, pbr_config)) {
        KERROR("Failed to create PBR shader.");
        return false;
    }
    resource_system_unload(&pbr_config_resource);
    // Save off a pointer to the PBR shader.
    internal_data->pbr_shader = shader_system_get(pbr_shader_name);

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
        internal_data->debug_locations.projection = shader_system_uniform_location(internal_data->colour_shader, "projection");
        internal_data->debug_locations.view = shader_system_uniform_location(internal_data->colour_shader, "view");
        internal_data->debug_locations.model = shader_system_uniform_location(internal_data->colour_shader, "model");
    }

    return true;
}

b8 scene_pass_load_resources(struct rendergraph_pass* self) {
    if (!self) {
        return false;
    }
    scene_pass_internal_data* internal_data = self->internal_data;

    // Ensure a source is hooked up to the shadowmap sinks.
    u32 sink_count = darray_length(self->sinks);

    // Make sure the current sink has a source hooked up to it.
    for (u32 i = 0; i < sink_count; ++i) {
        rendergraph_sink* sink = &self->sinks[i];
        if (strings_equali(sink->name, "shadowmap")) {
            internal_data->shadowmap_source = sink->bound_source;
            break;
        }
    }
    if (!internal_data->shadowmap_source) {
        KERROR("Required '%s' source not hooked up to scene pass. Creation fails.", "shadowmap");
        return false;
    }

    // Need a texture map (i.e. sampler) to use the shadowmap source textures. One per frame.
    internal_data->frame_count = renderer_window_attachment_count_get();
    internal_data->shadow_maps = kallocate(sizeof(texture_map) * internal_data->frame_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < internal_data->frame_count; ++i) {
        texture_map* sm = &internal_data->shadow_maps[i];
        sm->repeat_u = sm->repeat_v = sm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
        sm->filter_minify = sm->filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
        sm->texture = internal_data->shadowmap_source->textures[i];
        sm->generation = INVALID_ID;

        if (!renderer_texture_map_resources_acquire(sm)) {
            KERROR("Failed to acquire texture map resources for shadow map in scene pass. Initialize failed.");
            return false;
        }
    }

    return true;
}

b8 scene_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    // Bind the viewport
    renderer_active_viewport_set(self->pass_data.vp);

    if (!renderer_renderpass_begin(&self->pass, &self->pass.targets[p_frame_data->render_target_index])) {
        KERROR("scene pass failed to start.");
        return false;
    }

    scene_pass_internal_data* internal_data = self->internal_data;
    scene_pass_extended_data* ext_data = self->pass_data.ext_data;

    if (!material_system_irradiance_set(ext_data->irradiance_cube_texture)) {
        KERROR("Failed to set irradiance texture, check the properties of said texture.");
    }

    for (u8 i = 0; i < MAX_CASCADE_COUNT; ++i) {
        mat4 light_space = mat4_mul(ext_data->directional_light_views[i], ext_data->directional_light_projections[i]);
        material_system_directional_light_space_set(light_space, i);
        material_system_shadow_map_set(internal_data->shadowmap_source->textures[p_frame_data->render_target_index], i);
    }

    // Use the appropriate shader and apply the global uniforms.
    u32 terrain_count = ext_data->terrain_geometry_count;
    if (terrain_count > 0) {
        shader_system_use_by_id(internal_data->terrain_shader->id);
        if (ext_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
            shader_system_set_wireframe(internal_data->terrain_shader, true);
        } else {
            shader_system_set_wireframe(internal_data->terrain_shader, false);
        }

        // Apply globals
        if (!material_system_apply_global(internal_data->terrain_shader->id, p_frame_data, &self->pass_data.projection_matrix, &self->pass_data.view_matrix, &ext_data->cascade_splits, &self->pass_data.view_position, ext_data->render_mode)) {
            KERROR("Failed to use apply globals for terrain shader. Render frame failed.");
            return false;
        }

        for (u32 i = 0; i < terrain_count; ++i) {
            material* m = 0;
            if (ext_data->terrain_geometries[i].material) {
                m = ext_data->terrain_geometries[i].material;
            } else {
                m = material_system_get_default_terrain();
            }

            // Update the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times. It still needs to be bound
            // either way, so this check result gets passed to the backend which either
            // updates the internal shader bindings and binds them, or only binds them.
            // Also need to check against the renderer draw index.
            // TODO: At least for now, the entire terrain shares one material, so a lot of this
            // should probably be moved to global (i.e. texture maps and surface properties), but
            // leave lighting at the instance level.
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

            material_system_apply_local(m, &ext_data->terrain_geometries[i].model, p_frame_data);

            // Draw it.
            renderer_geometry_draw(&ext_data->terrain_geometries[i]);
        }
    }

    // Static geometries.
    u32 geometry_count = ext_data->geometry_count;
    if (geometry_count > 0) {
        // Update globals for material and PBR shaders.
        if (!shader_system_use_by_id(internal_data->pbr_shader->id)) {
            KERROR("Failed to use PBR shader. Render frame failed.");
            return false;
        }

        if (ext_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
            shader_system_set_wireframe(internal_data->pbr_shader, true);
        } else {
            shader_system_set_wireframe(internal_data->pbr_shader, false);
        }

        // Apply globals
        if (!material_system_apply_global(internal_data->pbr_shader->id, p_frame_data, &self->pass_data.projection_matrix, &self->pass_data.view_matrix, &ext_data->cascade_splits, &self->pass_data.view_position, ext_data->render_mode)) {
            KERROR("Failed to use apply globals for PBR shader. Render frame failed.");
            return false;
        }

        u32 current_material_id = INVALID_ID - 1;
        // Draw geometries.
        u32 count = ext_data->geometry_count;
        for (u32 i = 0; i < count; ++i) {
            material* m = 0;
            if (ext_data->geometries[i].material) {
                m = ext_data->geometries[i].material;
            } else {
                m = material_system_get_default();
            }

            // Only rebind/update the material if it's a new material. Duplicates can reuse the already-bound material.
            if (m->internal_id != current_material_id) {
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
                current_material_id = m->id;
            }

            // Apply the locals
            material_system_apply_local(m, &ext_data->geometries[i].model, p_frame_data);

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
        renderer_shader_bind_globals(internal_data->colour_shader);
        shader_system_uniform_set_by_location(internal_data->debug_locations.projection, &self->pass_data.projection_matrix);
        shader_system_uniform_set_by_location(internal_data->debug_locations.view, &self->pass_data.view_matrix);

        shader_system_apply_global(true, p_frame_data);

        // Each geometry.
        for (u32 i = 0; i < debug_geometry_count; ++i) {
            // NOTE: No instance-level uniforms to be set.

            // Local
            shader_system_bind_local();
            shader_system_uniform_set_by_location(internal_data->debug_locations.model, &ext_data->debug_geometries[i].model);
            shader_system_apply_local(p_frame_data);

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
            scene_pass_internal_data* internal_data = self->internal_data;

            // Destroy the texture maps/samplers.
            for (u32 i = 0; i < internal_data->frame_count; ++i) {
                renderer_texture_map_resources_release(&internal_data->shadow_maps[i]);
            }

            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(scene_pass_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}
