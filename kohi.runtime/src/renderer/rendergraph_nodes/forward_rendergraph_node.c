#include "forward_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "strings/kstring.h"

// FIXME: Kinda dumb to have to include this to get MAX_SHADOW_CASCADE_COUNT...
#include "renderer/rendergraph_nodes/shadow_rendergraph_node.h"

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

typedef struct forward_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    /* forward_rendergraph_node_config config; */

    renderpass internal_renderpass;

    shader* pbr_shader;
    shader* terrain_shader;
    shader* colour_shader;
    // FIXME: Move debug shape rendering to another node.
    debug_shader_locations debug_locations;

    rendergraph_source* shadowmap_source;

    // Obtained from source.
    texture_map shadow_map;

    // Execution data

    u32 render_mode;
    viewport vp;

    u32 geometry_count;
    struct geometry_render_data* geometries;

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;

    u32 debug_geometry_count;
    struct geometry_render_data* debug_geometries;

    struct texture* irradiance_cube_texture;

    f32 cascade_splits[MAX_SHADOW_CASCADE_COUNT];
    mat4 directional_light_views[MAX_SHADOW_CASCADE_COUNT];
    mat4 directional_light_projections[MAX_SHADOW_CASCADE_COUNT];
} forward_rendergraph_node_internal_data;

b8 forward_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, struct rendergraph_node_config* config) {
    if (!self) {
        KERROR("forward_rendergraph_node_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("forward_rendergraph_node_create requires a valid configuration.");
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(forward_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;

    self->name = string_duplicate(config->name);

    // Setup sinks
    self->sink_count = 3;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    // Grab sink configs first. These are all required because they need source linkages.
    rendergraph_node_sink_config* colourbuffer_sink_config = 0;
    rendergraph_node_sink_config* depthbuffer_sink_config = 0;
    rendergraph_node_sink_config* shadow_sink_config = 0;
    for (u32 i = 0; i < config->sink_count; ++i) {
        rendergraph_node_sink_config* sink = &config->sinks[i];
        if (strings_equali("colourbuffer", sink->name)) {
            colourbuffer_sink_config = sink;
            break;
        } else if (strings_equali("depthbuffer", sink->name)) {
            depthbuffer_sink_config = sink;
            break;
        } else if (strings_equali("shadow", sink->name)) {
            shadow_sink_config = sink;
            break;
        }
    }

    // Colourbuffer sink
    if (!colourbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'colourbuffer'.");
        return false;
    } else {
        rendergraph_sink* colourbuffer_sink = &self->sinks[0];
        colourbuffer_sink->name = string_duplicate("colourbuffer");
        colourbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
        colourbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        colourbuffer_sink->configured_source_name = string_duplicate(colourbuffer_sink_config->source_name);
    }

    // Depthbuffer sink
    if (!depthbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'depthbuffer'.");
        return false;
    } else {
        rendergraph_sink* depthbuffer_sink = &self->sinks[1];
        depthbuffer_sink->name = string_duplicate("depthbuffer");
        depthbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
        depthbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        depthbuffer_sink->configured_source_name = string_duplicate(depthbuffer_sink_config->source_name);
    }

    // Shadow sink
    if (!depthbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'shadow'.");
        return false;
    } else {
        rendergraph_sink* shadow_sink = &self->sinks[2];
        shadow_sink->name = string_duplicate("shadow");
        shadow_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        shadow_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        shadow_sink->configured_source_name = string_duplicate(shadow_sink_config->source_name);
    }

    // Has two sources, one for the colourbuffer and one for the depthbuffer.
    self->source_count = 2;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* colourbuffer_source = &self->sources[0];
    colourbuffer_source->name = string_duplicate("colourbuffer");
    colourbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    colourbuffer_source->value.framebuffer_handle = k_handle_invalid();
    colourbuffer_source->is_bound = false;

    // Setup the colourbuffer source.
    rendergraph_source* depthbuffer_source = &self->sources[1];
    depthbuffer_source->name = string_duplicate("depthbuffer");
    depthbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    depthbuffer_source->value.framebuffer_handle = k_handle_invalid();
    depthbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = forward_rendergraph_node_initialize;
    self->destroy = forward_rendergraph_node_destroy;
    self->load_resources = forward_rendergraph_node_load_resources;
    self->execute = forward_rendergraph_node_execute;

    return true;
}

b8 forward_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Setup sinks and sources. Don't bind these yet.
    {
        // Sinks
        {
            self->sinks = darray_create(rendergraph_sink);
            // colourbuffer sink
            rendergraph_sink colourbuffer_sink = {0};
            colourbuffer_sink.name = string_duplicate("colourbuffer");
            colourbuffer_sink.type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
            darray_push(self->sinks, colourbuffer_sink);
            // depthbuffer sink
            rendergraph_sink depthbuffer_sink = {0};
            depthbuffer_sink.name = string_duplicate("depthbuffer");
            depthbuffer_sink.type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
            darray_push(self->sinks, depthbuffer_sink);
            // directional shadow sink
            rendergraph_sink directional_shadow_sink = {0};
            directional_shadow_sink.name = string_duplicate("directional_shadowmap");
            directional_shadow_sink.type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
            darray_push(self->sinks, directional_shadow_sink);
        }

        // Sources
        {
            self->sources = darray_create(rendergraph_source);
            // colourbuffer source
            rendergraph_source colourbuffer_source = {0};
            colourbuffer_source.name = string_duplicate("colourbuffer");
            colourbuffer_source.type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
            colourbuffer_source.value.framebuffer_handle = k_handle_invalid();
            colourbuffer_source.is_bound = false;
            darray_push(self->sources, colourbuffer_source);
            // depthbuffer source
            rendergraph_source depthbuffer_source = {0};
            depthbuffer_source.name = string_duplicate("depthbuffer");
            depthbuffer_source.type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
            depthbuffer_source.value.framebuffer_handle = k_handle_invalid();
            depthbuffer_source.is_bound = false;
            darray_push(self->sources, depthbuffer_source);
        }
    }

    // Renderpass config - forward.
    {
        renderpass_config forward_rendergraph_node_config = {0};
        forward_rendergraph_node_config.name = "Renderpass.World";
        forward_rendergraph_node_config.clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
        forward_rendergraph_node_config.attachment_count = 2;
        forward_rendergraph_node_config.attachment_configs = kallocate(sizeof(renderpass_attachment_config) * forward_rendergraph_node_config.attachment_count, MEMORY_TAG_ARRAY);

        // Colour attachment
        renderpass_attachment_config* attachment_colour = &forward_rendergraph_node_config.attachment_configs[0];
        attachment_colour->type = RENDERER_ATTACHMENT_TYPE_FLAG_COLOUR_BIT;
        attachment_colour->load_op = RENDERER_ATTACHMENT_LOAD_OPERATION_LOAD;
        attachment_colour->store_op = RENDERER_ATTACHMENT_STORE_OPERATION_STORE;
        attachment_colour->post_pass_use = RENDERER_ATTACHMENT_USE_COLOUR_ATTACHMENT;
        attachment_colour->clear_flags = 0;

        // Depth attachment
        renderpass_attachment_config* attachment_depth = &forward_rendergraph_node_config.attachment_configs[1];
        attachment_depth->type = RENDERER_ATTACHMENT_TYPE_FLAG_DEPTH_BIT;
        attachment_depth->load_op = RENDERER_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        attachment_depth->store_op = RENDERER_ATTACHMENT_STORE_OPERATION_STORE;
        attachment_depth->post_pass_use = RENDERER_ATTACHMENT_USE_DEPTH_STENCIL_ATTACHMENT;
        attachment_depth->clear_flags = 0;

        if (!renderer_renderpass_create(&forward_rendergraph_node_config, &internal_data->internal_renderpass)) {
            KERROR("Failed to create forward renderpass ");
            return false;
        }
    }

    // Load PBR shader
    const char* pbr_shader_name = "Shader.PBRMaterial";
    resource pbr_config_resource;
    if (!resource_system_load(pbr_shader_name, RESOURCE_TYPE_SHADER, 0, &pbr_config_resource)) {
        KERROR("Failed to load PBR shader resource.");
        return false;
    }
    shader_config* pbr_config = (shader_config*)pbr_config_resource.data;
    if (!shader_system_create(&internal_data->internal_renderpass, pbr_config)) {
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
    if (!shader_system_create(&internal_data->internal_renderpass, terrain_shader_config)) {
        KERROR("Failed to create terrain shader.");
        return false;
    }
    resource_system_unload(&terrain_shader_config_resource);
    // Save off a pointer to the terrain shader.
    internal_data->terrain_shader = shader_system_get(terrain_shader_name);

    // Load debug colour3d shader.
    // FIXME: Move to another node.
    const char* colour3d_shader_name = "Shader.Builtin.ColourShader3D";
    resource colour3d_shader_config_resource;
    if (!resource_system_load(colour3d_shader_name, RESOURCE_TYPE_SHADER, 0, &colour3d_shader_config_resource)) {
        KERROR("Failed to load colour3d shader resource.");
        return false;
    }
    shader_config* colour3d_shader_config = (shader_config*)colour3d_shader_config_resource.data;
    if (!shader_system_create(&internal_data->internal_renderpass, colour3d_shader_config)) {
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

b8 forward_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Ensure a source is hooked up to the shadowmap sinks.
    u32 sink_count = darray_length(self->sinks);

    // Make sure the current sink has a source hooked up to it.
    // TODO: sink/source verification should be done at the graph level.
    for (u32 i = 0; i < sink_count; ++i) {
        rendergraph_sink* sink = &self->sinks[i];
        // TODO: configurable?
        if (strings_equali(sink->name, "shadowmap")) {
            internal_data->shadowmap_source = sink->bound_source;
            break;
        }
    }
    if (!internal_data->shadowmap_source) {
        KERROR("Required '%s' source not hooked up to forward pass. Creation fails.", "shadowmap");
        return false;
    }

    // Need a texture map (i.e. sampler) to use the shadowmap source texture.
    texture_map* sm = &internal_data->shadow_map;
    sm->repeat_u = sm->repeat_v = sm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    sm->filter_minify = sm->filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
    // TODO: Might think about moving this elsewhere and handling in a more generic way.
    sm->texture = internal_data->shadowmap_source->value.t;
    sm->generation = INVALID_ID;

    if (!renderer_texture_map_resources_acquire(sm)) {
        KERROR("Failed to acquire texture map resources for shadow map in forward pass. Initialize failed.");
        return false;
    }

    return true;
}

b8 forward_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // LEFTOFF: refactor beloW

    // Bind the viewport
    renderer_active_viewport_set(&internal_data->vp);

    // FIXME: Get current framebuffer from window or w/e is hooked up.
    if (!renderer_renderpass_begin(&internal_data->internal_renderpass, &internal_data->colourbuffer)) {
        KERROR("Forward pass failed to start.");
        return false;
    }

    if (!material_system_irradiance_set(internal_data->irradiance_cube_texture)) {
        KERROR("Failed to set irradiance texture, check the properties of said texture.");
    }

    for (u8 i = 0; i < MAX_CASCADE_COUNT; ++i) {
        mat4 light_space = mat4_mul(internal_data->directional_light_views[i], internal_data->directional_light_projections[i]);
        material_system_directional_light_space_set(light_space, i);
        material_system_shadow_map_set(internal_data->shadowmap_source->textures[p_frame_data->render_target_index], i);
    }

    // Use the appropriate shader and apply the global uniforms.
    u32 terrain_count = internal_data->terrain_geometry_count;
    if (terrain_count > 0) {
        shader_system_use_by_id(internal_data->terrain_shader->id);
        if (internal_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
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
            if (internal_data->terrain_geometries[i].material) {
                m = internal_data->terrain_geometries[i].material;
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

            material_system_apply_local(m, &internal_data->terrain_geometries[i].model, p_frame_data);

            // Draw it.
            renderer_geometry_draw(&internal_data->terrain_geometries[i]);
        }
    }

    // Static geometries.
    u32 geometry_count = internal_data->geometry_count;
    if (geometry_count > 0) {
        // Update globals for material and PBR shaders.
        if (!shader_system_use_by_id(internal_data->pbr_shader->id)) {
            KERROR("Failed to use PBR shader. Render frame failed.");
            return false;
        }

        if (internal_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
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
        KERROR("Forward pass failed to end.");
        return false;
    }

    return true;
}

void forward_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            forward_rendergraph_node_internal_data* internal_data = self->internal_data;

            // Destroy the texture maps/samplers.
            for (u32 i = 0; i < internal_data->frame_count; ++i) {
                renderer_texture_map_resources_release(&internal_data->shadow_maps[i]);
            }

            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(forward_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}
