#include "shadow_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "defines.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "strings/kstring.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

typedef struct shadow_map_shader_locations {
    u16 projections_location;
    u16 views_location;
    u16 model_location;
    u32 cascade_index_location;
    u16 colour_map_location;
} shadow_map_shader_locations;

typedef struct cascade_resources {
    render_target target;
} cascade_resources;

typedef struct shadow_shader_instance_data {
    u64 render_frame_number;
    u8 render_draw_index;
} shadow_shader_instance_data;

typedef struct shadow_map_pass_internal_data {
    struct renderer_system_state* renderer;
    shadow_map_pass_config config;

    shader* s;
    shadow_map_shader_locations locations;

    // Custom projection matrix for shadow pass.
    viewport camera_viewport;

    // The depth texture used for the directional light shadow.
    texture depth_texture;

    // One per cascade.
    cascade_resources cascades[MAX_CASCADE_COUNT];

    // Track instance updates per frame
    b8* instance_updated;
    u32 instance_count;
    // Default map to be used when materials aren't available.
    texture_map default_colour_map;
    u32 default_instance_id;
    u64 default_instance_frame_number;
    u8 default_instance_draw_index;

    // Track instance data per instance. darray
    shadow_shader_instance_data* instances;

    // Separate shader/instance info for terrains.
    shader* ts;
    shadow_map_shader_locations terrain_locations;
    texture_map default_terrain_colour_map;
    u32 terrain_instance_id;
    u64 terrain_instance_frame_number;
    u8 terrain_instance_draw_index;
} shadow_map_pass_internal_data;

b8 shadow_rendergraph_node_create(struct rendergraph_node* self, void* config) {
    if (!self || !config) {
        KERROR("shadow_map_pass_create requires both a pointer to self and a valid config");
        return false;
    }

    self->internal_data = kallocate(sizeof(shadow_map_pass_internal_data), MEMORY_TAG_RENDERER);
    shadow_map_pass_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->config = *((shadow_map_pass_config*)config);

    self->pass_data.ext_data = kallocate(sizeof(shadow_map_pass_extended_data), MEMORY_TAG_RENDERER);

    // Custom function pointers.
    self->source_populate = shadow_map_pass_source_populate;

    return true;
}

b8 shadow_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    shadow_map_pass_internal_data* internal_data = self->internal_data;

    // Create the depth attachment for the directional light shadow.
    // This should take renderer buffering into account.
    texture_flag_bits flags = TEXTURE_FLAG_DEPTH | TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING;
    renderer_texture_resources_acquire(
        internal_data->renderer, "shadowmap_node_texture", TEXTURE_TYPE_2D_ARRAY, internal_data->config.resolution, internal_data->config.resolution,
        4, 1, MAX_CASCADE_COUNT, flags, &internal_data->depth_texture.renderer_texture_handle);

    // Setup the renderpass.
    renderpass_config shadowmap_pass_config = {0};
    shadowmap_pass_config.name = "Renderpass.Shadowmap";
    shadowmap_pass_config.clear_flags = 0;
    shadowmap_pass_config.target.attachment_count = 1;
    shadowmap_pass_config.target.attachments = kallocate(sizeof(render_target_attachment_config) * shadowmap_pass_config.target.attachment_count, MEMORY_TAG_ARRAY);

    // Depth attachment.
    render_target_attachment_config* shadowpass_target_depth = &shadowmap_pass_config.target.attachments[0];
    shadowpass_target_depth->type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    shadowpass_target_depth->source = RENDER_TARGET_ATTACHMENT_SOURCE_SELF; // This owns the attachment.
    shadowpass_target_depth->load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    shadowpass_target_depth->store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    shadowpass_target_depth->present_after = true;

    if (!renderer_renderpass_create(&shadowmap_pass_config, &self->pass)) {
        KERROR("Shadowmap rendergraph pass - Failed to create shadow map renderpass.");
        return false;
    }

    // Load shadowmap shader. Attempt to to get the already-loaded shader if it doesn't exist.
    const char* shadowmap_shader_name = "Shader.Shadowmap";
    internal_data->s = shader_system_get(shadowmap_shader_name);
    if (!internal_data->s) {
        KTRACE("Shader '%s' doesn't exist. Attempting to load it...", shadowmap_shader_name);
        resource shadowmap_shader_config_resource;
        if (!resource_system_load(shadowmap_shader_name, RESOURCE_TYPE_SHADER, 0, &shadowmap_shader_config_resource)) {
            KERROR("Failed to load shadowmap shader resource.");
            return false;
        }
        shader_config* shadowmap_shader_config = (shader_config*)shadowmap_shader_config_resource.data;
        if (!shader_system_create(&self->pass, shadowmap_shader_config)) {
            KERROR("Failed to create shadowmap shader.");
            return false;
        }

        resource_system_unload(&shadowmap_shader_config_resource);
        // Get a pointer to the shader.
        internal_data->s = shader_system_get(shadowmap_shader_name);
    } else {
        KTRACE("Shader '%s' already exists, using it.", shadowmap_shader_name);
    }
    internal_data->locations.projections_location = shader_system_uniform_location(internal_data->s, "projections");
    internal_data->locations.views_location = shader_system_uniform_location(internal_data->s, "views");
    internal_data->locations.model_location = shader_system_uniform_location(internal_data->s, "model");
    internal_data->locations.cascade_index_location = shader_system_uniform_location(internal_data->s, "cascade_index");
    internal_data->locations.colour_map_location = shader_system_uniform_location(internal_data->s, "colour_map");

    // Terrain shadowmap shader.
    const char* terrain_shadowmap_shader_name = "Shader.ShadowmapTerrain";
    internal_data->ts = shader_system_get(terrain_shadowmap_shader_name);
    if (!internal_data->ts) {
        KTRACE("Shader '%s' doesn't exist. Attempting to load it...", terrain_shadowmap_shader_name);
        resource terrain_shadowmap_shader_config_resource;
        if (!resource_system_load(terrain_shadowmap_shader_name, RESOURCE_TYPE_SHADER, 0, &terrain_shadowmap_shader_config_resource)) {
            KERROR("Failed to load terrain shadowmap shader resource.");
            return false;
        }
        shader_config* terrain_shadowmap_shader_config = (shader_config*)terrain_shadowmap_shader_config_resource.data;
        if (!shader_system_create(&self->pass, terrain_shadowmap_shader_config)) {
            KERROR("Failed to create terrain shadowmap shader.");
            return false;
        }

        resource_system_unload(&terrain_shadowmap_shader_config_resource);
        // Get a pointer to the shader.
        internal_data->ts = shader_system_get(terrain_shadowmap_shader_name);
    } else {
        KTRACE("Shader '%s' already exists, using it.", terrain_shadowmap_shader_name);
    }

    internal_data->terrain_locations.projections_location = shader_system_uniform_location(internal_data->ts, "projections");
    internal_data->terrain_locations.views_location = shader_system_uniform_location(internal_data->ts, "views");
    internal_data->terrain_locations.model_location = shader_system_uniform_location(internal_data->ts, "model");
    internal_data->terrain_locations.cascade_index_location = shader_system_uniform_location(internal_data->ts, "cascade_index");
    internal_data->terrain_locations.colour_map_location = shader_system_uniform_location(internal_data->ts, "colour_map");

    return true;
}

b8 shadow_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }
    shadow_map_pass_internal_data* internal_data = self->internal_data;

    // Create a texture map to be used across the board for the diffuse/albedo transparency sample.
    internal_data->default_colour_map.mip_levels = 1;
    internal_data->default_colour_map.generation = INVALID_ID;
    internal_data->default_colour_map.repeat_u = internal_data->default_colour_map.repeat_v = internal_data->default_colour_map.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    internal_data->default_colour_map.filter_minify = internal_data->default_colour_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;

    // Grab the default texture for the default texture map.
    internal_data->default_colour_map.texture = texture_system_get_default_diffuse_texture();

    // Create a texture map to be used across the board for the diffuse/albedo transparency sample.
    internal_data->default_terrain_colour_map.mip_levels = 1;
    internal_data->default_terrain_colour_map.generation = INVALID_ID;
    internal_data->default_terrain_colour_map.repeat_u = internal_data->default_terrain_colour_map.repeat_v = internal_data->default_terrain_colour_map.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    internal_data->default_terrain_colour_map.filter_minify = internal_data->default_terrain_colour_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;

    // Grab the default texture for the default terrain texture map.
    internal_data->default_terrain_colour_map.texture = texture_system_get_default_diffuse_texture();

    // Acquire resources for the default texture map.
    if (!renderer_texture_map_resources_acquire(&internal_data->default_colour_map)) {
        KERROR("Failed to acquire texture map resources for default colour map in shadowmap pass.");
        return false;
    }

    // Acquire resources for the default terrain texture map.
    if (!renderer_texture_map_resources_acquire(&internal_data->default_terrain_colour_map)) {
        KERROR("Failed to acquire texture map resources for default terrain colour map in shadowmap pass.");
        return false;
    }

    // Reserve an instance id for the default "material" to render to.
    {
        texture_map* maps[1] = {&internal_data->default_colour_map};
        shader* s = internal_data->s;
        u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
        shader_instance_resource_config instance_resource_config = {0};
        // Map count for this type is known.
        shader_instance_uniform_texture_config colour_texture = {0};
        colour_texture.uniform_location = atlas_location;
        colour_texture.texture_map_count = 1;
        colour_texture.texture_maps = maps;

        instance_resource_config.uniform_config_count = 1;
        instance_resource_config.uniform_configs = &colour_texture;
        renderer_shader_instance_resources_acquire(internal_data->s, &instance_resource_config, &internal_data->default_instance_id);
    }

    // Reserve an instance id for the default "material" to render to.
    {
        texture_map* terrain_maps[1] = {&internal_data->default_terrain_colour_map};
        shader* s = internal_data->ts;
        u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
        shader_instance_resource_config instance_resource_config = {0};
        // Map count for this type is known.
        shader_instance_uniform_texture_config colour_texture = {0};
        colour_texture.uniform_location = atlas_location;
        colour_texture.texture_map_count = 1;
        colour_texture.texture_maps = terrain_maps;

        instance_resource_config.uniform_config_count = 1;
        instance_resource_config.uniform_configs = &colour_texture;

        renderer_shader_instance_resources_acquire(internal_data->ts, &instance_resource_config, &internal_data->terrain_instance_id);
    }

    // NOTE: Setup a default viewport. The only component that is used for this is the underlying
    // viewport rect, but is required to be set by the renderer before beginning a renderpass.
    // The projection matrix within this is not used, therefore the fov and clip planes do not matter.
    vec4 viewport_rect = {0, 0, internal_data->config.resolution, internal_data->config.resolution};
    if (!viewport_create(viewport_rect, 0.0f, 0.0f, 0.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC, &internal_data->camera_viewport)) {
        KERROR("Failed to create viewport for shadow map pass.");
        return false;
    }

    // Create the depth attachment for the directional light.
    // Each cascade uses one layer of the depth texture.
    for (u16 i = 0; i < MAX_CASCADE_COUNT; ++i) {
        cascade_resources* cascade = &internal_data->cascades[i];

        render_target* target = &cascade->target;
        target->attachment_count = 1;
        // 2 attachments per target, (colour, depth)
        target->attachments = kallocate(sizeof(render_target_attachment) * target->attachment_count, MEMORY_TAG_ARRAY);
        target->attachments[0].type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
        target->attachments[0].source = RENDER_TARGET_ATTACHMENT_SOURCE_SELF;
        target->attachments[0].texture = &internal_data->depth_texture;
        target->attachments[0].present_after = true;
        target->attachments[0].load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
        target->attachments[0].store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;

        // Create the underlying render target.
        if (!renderer_render_target_create(
                target->attachment_count,
                target->attachments,
                &i, // Since using a layered image, the cascade index is also the layer index.
                true,
                &self->pass,
                target)) {
            KERROR("Failed to create render target backing resources in shadow map pass.");
            return false;
        }
    }

    // NOTE: This is from the "attachment_populate" that used to exist.
    // TODO: This hook-up might need to be made elsewhere.
    // Also note that this is already assigned above, so might not be needed.
    /* shadow_map_pass_internal_data* internal_data = self->internal_data;
    if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_DEPTH) {
        attachment->texture = &internal_data->depth_textures[0];
        return true;
    } */

    return true;
}

b8 shadow_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    shadow_map_pass_internal_data* internal_data = self->internal_data;
    shadow_map_pass_extended_data* ext_data = self->pass_data.ext_data;

    // Bind the internal viewport - do not use one provided in pass data.
    renderer_active_viewport_set(&internal_data->camera_viewport);

    // One renderpass per cascade - directional light.
    for (u32 p = 0; p < MAX_CASCADE_COUNT; ++p) {

        if (!renderer_renderpass_begin(&self->pass, &internal_data->cascades[p].target)) {
            KERROR("Shadowmap pass failed to start.");
            return false;
        }

        // Use the standard shadowmap shader.
        shader_system_use_by_id(internal_data->s->id);

        // Apply globals, once per cascade.
        b8 needs_update = p == 0;
        if (needs_update) {
            renderer_shader_bind_globals(internal_data->s);
            for (u32 i = 0; i < MAX_CASCADE_COUNT; ++i) {
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->locations.projections_location, i, &ext_data->cascades[i].projection)) {
                    KERROR("Failed to apply shadowmap projection uniform.");
                    return false;
                }
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->locations.views_location, i, &ext_data->cascades[i].view)) {
                    KERROR("Failed to apply shadowmap view uniform.");
                    return false;
                }
            }
        }
        shader_system_apply_global(needs_update, p_frame_data);

        u32 geometry_count = ext_data->geometry_count;
        u32 terrain_geometry_count = ext_data->terrain_geometry_count;

        // Verify enough instance resources for this frame.
        // This is done by taking the highest material instance id
        // and using that for the count. This will ensure enough resources
        // are present for the frame, and also allows for a quick mapping to
        // a shader instance for texture binding, as well as keeping track
        // of instance updates per frame.
        u32 highest_id = 0;
        for (u32 i = 0; i < geometry_count; ++i) {
            material* m = ext_data->geometries[i].material;
            if (m->internal_id > highest_id) {
                // NOTE: +1 to account for the first id being taken by the default instance.
                highest_id = m->internal_id + 1;
            }
        }
        // Terrains will be slightly different since a texture sample isn't
        // really needed since terrains are never transparent. Therefore, only
        // one more instance is needed, which can use the same default white
        // texture as a sample.
        highest_id++;

        if (highest_id > internal_data->instance_count) {
            if (internal_data->instances) {
                darray_destroy(internal_data->instances);
            }
            internal_data->instances = darray_reserve(shadow_shader_instance_data, highest_id + 1);
            // Get more resources if needed, starting at the previous high point.
            for (u32 i = internal_data->instance_count; i < highest_id; i++) {
                u32 instance_id;

                // Use the same map for all.
                texture_map* maps[1] = {&internal_data->default_colour_map};
                shader* s = internal_data->s;
                u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
                shader_instance_resource_config instance_resource_config = {0};
                // Map count for this type is known.
                shader_instance_uniform_texture_config colour_texture = {0};
                colour_texture.uniform_location = atlas_location;
                colour_texture.texture_map_count = 1;
                colour_texture.texture_maps = maps;

                instance_resource_config.uniform_config_count = 1;
                instance_resource_config.uniform_configs = &colour_texture;
                renderer_shader_instance_resources_acquire(internal_data->s, &instance_resource_config, &instance_id);

                shadow_shader_instance_data* instance = &internal_data->instances[instance_id];
                instance->render_frame_number = INVALID_ID_U64;
                instance->render_draw_index = INVALID_ID_U8;
            }
            internal_data->instance_count = highest_id;
        }

        // Static geometries.
        for (u32 i = 0; i < geometry_count; ++i) {
            geometry_render_data* g = &ext_data->geometries[i];

            u32 bind_id = INVALID_ID;
            texture_map* bind_map = 0;
            // FIXME: These should be removed.
            /* u64* render_number = 0;
            u8* draw_index = 0; */

            // Decide what bindings to use.
            if (g->material && g->material->maps) {
                // Use current material's internal id.
                // NOTE: +1 to account for the first id being taken by the default instance.
                bind_id = g->material->internal_id + 1;
                // Use the current material's diffuse/albedo map.
                bind_map = &g->material->maps[0];
                // NOTE: can't update the _material's_ frame number/draw index because it still needs to be
                // used for the actual scene render.
                /* shadow_shader_instance_data* instance = &internal_data->instances[g->material->internal_id + 1]; */
                // FIXME: These should be removed.
                /* render_number = &instance->render_frame_number;
                draw_index = &instance->render_draw_index; */
            } else {
                // Use the default instance.
                bind_id = internal_data->default_instance_id;
                // Use the default colour map.
                bind_map = &internal_data->default_colour_map;
                // FIXME: These should be removed.
                /* render_number = &internal_data->default_instance_frame_number;
                draw_index = &internal_data->default_instance_draw_index; */
            }

            // FIXME: Commenting this out will likely break this pass.
            b8 needs_update = true; //*render_number != p_frame_data->renderer_frame_number || *draw_index != p_frame_data->draw_index;

            // Use the bindings.
            shader_system_bind_instance(bind_id);
            if (!shader_system_uniform_set_by_location(internal_data->locations.colour_map_location, bind_map)) {
                KERROR("Failed to apply shadowmap color_map uniform to static geometry.");
                return false;
            }
            shader_system_apply_instance(needs_update, p_frame_data);

            // Sync the frame number and draw index.
            // FIXME: These should be removed.
            /* *render_number = p_frame_data->renderer_frame_number;
             *draw_index = p_frame_data->draw_index; */

            // Apply the locals
            shader_system_bind_local();
            shader_system_uniform_set_by_location(internal_data->locations.model_location, &g->model);
            shader_system_uniform_set_by_location(internal_data->locations.cascade_index_location, &p);
            shader_system_apply_local(p_frame_data);

            // Invert if needed
            if (ext_data->geometries[i].winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
            }

            // Draw it.
            renderer_geometry_draw(g);

            // Change back if needed
            if (ext_data->geometries[i].winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
            }
        }

        // Terrain - use the special terrain shadowmap shader.
        shader_system_use_by_id(internal_data->ts->id);

        // Apply globals, once per cascade.
        renderer_shader_bind_globals(internal_data->ts);
        if (needs_update) {
            for (u32 i = 0; i < MAX_CASCADE_COUNT; ++i) {
                // NOTE: using the internal projection matrix, not one passed in.
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_locations.projections_location, i, &ext_data->cascades[i].projection)) {
                    KERROR("Failed to apply terrain shadowmap projection uniform.");
                    return false;
                }
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_locations.views_location, i, &ext_data->cascades[i].view)) {
                    KERROR("Failed to apply terrain shadowmap view uniform.");
                    return false;
                }
            }
        }
        shader_system_apply_global(needs_update, p_frame_data);

        for (u32 i = 0; i < terrain_geometry_count; ++i) {
            geometry_render_data* terrain = &ext_data->terrain_geometries[i];

            // Just draw these using the default instance and texture map.
            texture_map* bind_map = &internal_data->default_terrain_colour_map;
            // FIXME: These should be removed.
            /* u64* render_number = &internal_data->terrain_instance_frame_number;
            u8* draw_index = &internal_data->terrain_instance_draw_index; */

            // FIXME: Commenting this out will likely break this pass.
            b8 needs_update = true; //*render_number != p_frame_data->renderer_frame_number || *draw_index != p_frame_data->draw_index;

            shader_system_bind_instance(internal_data->terrain_instance_id);
            if (!shader_system_uniform_set_by_location(internal_data->terrain_locations.colour_map_location, bind_map)) {
                KERROR("Failed to apply shadowmap color_map uniform to terrain geometry.");
                return false;
            }
            shader_system_apply_instance(needs_update, p_frame_data);

            // Sync the frame number and draw index.
            // FIXME: These should be removed.
            /* *render_number = p_frame_data->renderer_frame_number;
             *draw_index = p_frame_data->draw_index; */

            // Apply the locals
            shader_system_bind_local();
            shader_system_uniform_set_by_location(internal_data->terrain_locations.model_location, &terrain->model);
            shader_system_uniform_set_by_location(internal_data->terrain_locations.cascade_index_location, &p);
            shader_system_apply_local(p_frame_data);

            // Draw it.
            renderer_geometry_draw(terrain);
        }

        if (!renderer_renderpass_end(&self->pass)) {
            KERROR("Shadowmap pass failed to end.");
            return false;
        }

    } // End cascade pass

    return true;
}

void shadow_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            shadow_map_pass_internal_data* internal_data = self->internal_data;

            // Renderpass attachments.
            for (u32 i = 0; i < MAX_CASCADE_COUNT; ++i) {
                cascade_resources* cascade = &internal_data->cascades[i];

                // Create the underlying render target.
                renderer_render_target_destroy(&cascade->target, true);
            }

            renderer_texture_resources_release(internal_data->renderer, internal_data->depth_texture.renderer_texture_handle);

            renderer_texture_map_resources_release(&internal_data->default_colour_map);
            renderer_texture_map_resources_release(&internal_data->default_terrain_colour_map);
            renderer_shader_instance_resources_release(internal_data->s, internal_data->default_instance_id);
            renderer_shader_instance_resources_release(internal_data->ts, internal_data->terrain_instance_id);

            // Destroy the extended data.
            if (self->pass_data.ext_data) {
                kfree(self->pass_data.ext_data, sizeof(shadow_map_pass_extended_data), MEMORY_TAG_RENDERER);
                self->pass_data.ext_data = 0;
            }

            // Destroy the pass.
            renderer_renderpass_destroy(&self->pass);
            kfree(self->internal_data, sizeof(shadow_map_pass_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}
