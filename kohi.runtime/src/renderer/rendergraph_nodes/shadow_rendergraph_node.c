#include "shadow_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

typedef struct shadow_shader_locations {
    u16 projections_location;
    u16 views_location;
    u16 model_location;
    u32 cascade_index_location;
    u16 colour_map_location;
} shadow_shader_locations;

typedef struct cascade_resources {
    k_handle framebuffer_handle;
} cascade_resources;

typedef struct shadow_shader_instance_data {
    u64 render_frame_number;
    u8 render_draw_index;
} shadow_shader_instance_data;

typedef struct shadow_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    shadow_rendergraph_node_config config;

    renderpass internal_renderpass;

    shader* s;
    shadow_shader_locations locations;

    // Custom projection matrix for shadow pass.
    viewport camera_viewport;

    // The depth texture used for the directional light shadow.
    texture depth_texture;

    // One per cascade.
    cascade_resources cascade_resources[MAX_SHADOW_CASCADE_COUNT];

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
    shadow_shader_locations terrain_locations;
    texture_map default_terrain_colour_map;
    u32 terrain_instance_id;
    u64 terrain_instance_frame_number;
    u8 terrain_instance_draw_index;

    const struct directional_light* light;
    // Per-cascade data.
    shadow_cascade_data cascade_data[MAX_SHADOW_CASCADE_COUNT];

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;
    u32 geometry_count;
    struct geometry_render_data* geometries;

} shadow_rendergraph_node_internal_data;

b8 shadow_rendergraph_node_create(struct rendergraph_node* self, void* config) {
    if (!self || !config) {
        KERROR("shadow_map_pass_create requires both a pointer to self and a valid config");
        return false;
    }

    self->internal_data = kallocate(sizeof(shadow_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->config = *((shadow_rendergraph_node_config*)config);

    return true;
}

b8 shadow_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Create the depth attachment for the directional light shadow.
    // This should take renderer buffering into account.
    texture_flag_bits flags = TEXTURE_FLAG_DEPTH | TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING;
    renderer_texture_resources_acquire(
        internal_data->renderer, "shadowmap_node_texture", TEXTURE_TYPE_2D_ARRAY, internal_data->config.resolution, internal_data->config.resolution,
        4, 1, MAX_SHADOW_CASCADE_COUNT, flags, &internal_data->depth_texture.renderer_texture_handle);

    // Setup the renderpass.
    renderpass_config shadowmap_pass_config = {0};
    shadowmap_pass_config.name = "Renderpass.Shadowmap";
    shadowmap_pass_config.attachment_count = 1;
    shadowmap_pass_config.attachment_configs = kallocate(sizeof(renderpass_attachment_config) * shadowmap_pass_config.attachment_count, MEMORY_TAG_ARRAY);

    // Depth attachment.
    renderpass_attachment_config* shadowpass_target_depth = &shadowmap_pass_config.attachment_configs[0];
    shadowpass_target_depth->type = RENDERER_ATTACHMENT_TYPE_FLAG_DEPTH_BIT;
    shadowpass_target_depth->load_op = RENDERER_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    shadowpass_target_depth->store_op = RENDERER_ATTACHMENT_STORE_OPERATION_STORE;
    // Attachment will be read by a shader in another pass.
    shadowpass_target_depth->post_pass_use = RENDERER_ATTACHMENT_USE_DEPTH_STENCIL_SHADER_READ;

    if (!renderer_renderpass_create(&shadowmap_pass_config, &internal_data->internal_renderpass)) {
        KERROR("Shadow rendergraph node - Failed to create shadow internal renderpass.");
        return false;
    }

    // Load shadowmap shader. Attempt to to get the already-loaded shader if it doesn't exist.
    const char* shadowmap_shader_name = "Shader.Shadowmap";
    internal_data->s = shader_system_get(shadowmap_shader_name);
    if (!internal_data->s) {
        KTRACE("Shader '%s' doesn't exist. Attempting to load it...", shadowmap_shader_name);
        resource shadowmap_shader_config_resource;
        if (!resource_system_load(shadowmap_shader_name, RESOURCE_TYPE_SHADER, 0, &shadowmap_shader_config_resource)) {
            KERROR("Failed to load shadow shader resource.");
            return false;
        }
        shader_config* shadowmap_shader_config = (shader_config*)shadowmap_shader_config_resource.data;
        if (!shader_system_create(&internal_data->internal_renderpass, shadowmap_shader_config)) {
            KERROR("Failed to create shadow shader.");
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
            KERROR("Failed to load terrain shadow shader resource.");
            return false;
        }
        shader_config* terrain_shadowmap_shader_config = (shader_config*)terrain_shadowmap_shader_config_resource.data;
        if (!shader_system_create(&internal_data->internal_renderpass, terrain_shadowmap_shader_config)) {
            KERROR("Failed to create terrain shadow shader.");
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
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

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
    for (u16 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        cascade_resources* cascade = &internal_data->cascade_resources[i];

        framebuffer_config fb_config = {0};
        fb_config.attachment_count = 1;
        fb_config.attachments = kallocate(sizeof(framebuffer_attachment_config) * fb_config.attachment_count, MEMORY_TAG_ARRAY);
        fb_config.attachments[0].type = RENDERER_ATTACHMENT_TYPE_FLAG_DEPTH_BIT;
        fb_config.attachments[0].target = &internal_data->depth_texture;

        if (!renderer_framebuffer_create(internal_data->renderer, &fb_config, &cascade->framebuffer_handle)) {
            KERROR("Failed to create cascase framebuffer in shadow rendergraph node.");
            return false;
        }
    }

    return true;
}

b8 shadow_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Bind the internal viewport - do not use one provided in pass data.
    renderer_active_viewport_set(&internal_data->camera_viewport);

    // One renderpass per cascade - directional light.
    for (u32 p = 0; p < MAX_SHADOW_CASCADE_COUNT; ++p) {

        if (!renderer_renderpass_begin(&internal_data->internal_renderpass, internal_data->cascade_resources[p].framebuffer_handle)) {
            KERROR("Shadowmap pass failed to start.");
            return false;
        }

        // Use the standard shadowmap shader.
        shader_system_use_by_id(internal_data->s->id);

        // Apply globals, once per cascade.
        // LEFTOFF: Why is this being looped through *again*? This is already in a loop above.
        // Is it because we can only update the globals once?
        b8 needs_update = p == 0;
        if (needs_update) {
            renderer_shader_bind_globals(internal_data->s);
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->locations.projections_location, i, &internal_data->cascade_data[i].projection)) {
                    KERROR("Failed to apply shadowmap projection uniform.");
                    return false;
                }
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->locations.views_location, i, &internal_data->cascade_data[i].view)) {
                    KERROR("Failed to apply shadowmap view uniform.");
                    return false;
                }
            }
        }
        shader_system_apply_global(needs_update, p_frame_data);

        // Verify enough instance resources for this frame.
        // This is done by taking the highest material instance id
        // and using that for the count. This will ensure enough resources
        // are present for the frame, and also allows for a quick mapping to
        // a shader instance for texture binding, as well as keeping track
        // of instance updates per frame.
        u32 highest_id = 0;
        for (u32 i = 0; i < internal_data->geometry_count; ++i) {
            material* m = internal_data->geometries[i].material;
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
        {
            for (u32 i = 0; i < internal_data->geometry_count; ++i) {
                geometry_render_data* g = &internal_data->geometries[i];

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
                if (internal_data->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
                }

                // Draw it.
                renderer_geometry_draw(g);

                // Change back if needed
                if (internal_data->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
                }
            }
        }

        // Terrain - use the special terrain shadowmap shader.
        {
            shader_system_use_by_id(internal_data->ts->id);

            // Apply globals, once per cascade.
            renderer_shader_bind_globals(internal_data->ts);
            if (needs_update) {
                for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                    // NOTE: using the internal projection matrix, not one passed in.
                    if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_locations.projections_location, i, &internal_data->cascade_data[i].projection)) {
                        KERROR("Failed to apply terrain shadowmap projection uniform.");
                        return false;
                    }
                    if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_locations.views_location, i, &internal_data->cascade_data[i].view)) {
                        KERROR("Failed to apply terrain shadowmap view uniform.");
                        return false;
                    }
                }
            }
            shader_system_apply_global(needs_update, p_frame_data);

            for (u32 i = 0; i < internal_data->terrain_geometry_count; ++i) {
                geometry_render_data* terrain = &internal_data->terrain_geometries[i];

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
        }

        if (!renderer_renderpass_end(&internal_data->internal_renderpass)) {
            KERROR("Shadow pass failed to end.");
            return false;
        }

    } // End cascade pass

    return true;
}

void shadow_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

            // Renderpass attachments.
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                cascade_resources* cascade = &internal_data->cascade_resources[i];

                // Destroy the framebuffer.
                renderer_framebuffer_destroy(internal_data->renderer, &cascade->framebuffer_handle);
            }

            renderer_texture_resources_release(internal_data->renderer, internal_data->depth_texture.renderer_texture_handle);

            renderer_texture_map_resources_release(&internal_data->default_colour_map);
            renderer_texture_map_resources_release(&internal_data->default_terrain_colour_map);
            renderer_shader_instance_resources_release(internal_data->s, internal_data->default_instance_id);
            renderer_shader_instance_resources_release(internal_data->ts, internal_data->terrain_instance_id);

            // Destroy the pass.
            renderer_renderpass_destroy(&internal_data->internal_renderpass);

            // Internal data.
            kfree(self->internal_data, sizeof(shadow_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
            self->internal_data = 0;
        }
    }
}

b8 shadow_rendergraph_node_directional_light_set(struct rendergraph_node* self, const struct directional_light* light) {
    if (!self) {
        KERROR("shadow_rendergraph_node_directional_light_set requires a valid pointer to a rendergraph_node.");
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->light = light;
    return true;
}

b8 shadow_rendergraph_node_cascade_data_set(struct rendergraph_node* self, shadow_cascade_data data, u8 cascade_index) {
    if (!self) {
        KERROR("shadow_rendergraph_node_cascade_data_set requires a valid pointer to a rendergraph_node.");
        return false;
    }

    if (cascade_index > MAX_SHADOW_CASCADE_COUNT - 1) {
        KERROR("shadow_rendergraph_node_cascade_data_set index out of range. Expected [0-%d] but got %d.", MAX_SHADOW_CASCADE_COUNT - 1, cascade_index);
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->cascade_data[cascade_index] = data;
    return true;
}

b8 shadow_rendergraph_node_static_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (!self) {
        KERROR("shadow_rendergraph_node_static_geometries_set requires a valid pointer to a rendergraph_node.");
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Take a copy of the array. Note that this only lasts for the frame.
    internal_data->geometry_count = geometry_count;
    internal_data->geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
    kcopy_memory(internal_data->geometries, geometries, sizeof(geometry_render_data) * geometry_count);

    return false;
}

b8 shadow_rendergraph_node_terrain_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (!self) {
        KERROR("shadow_rendergraph_node_static_terrain_geometries_set requires a valid pointer to a rendergraph_node.");
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Take a copy of the array. Note that this only lasts for the frame.
    internal_data->terrain_geometry_count = geometry_count;
    internal_data->terrain_geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
    kcopy_memory(internal_data->terrain_geometries, geometries, sizeof(geometry_render_data) * geometry_count);

    return false;
}
