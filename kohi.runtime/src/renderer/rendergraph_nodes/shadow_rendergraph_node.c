#include "shadow_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
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
    // FIXME: not used - delete?
    k_handle framebuffer_handle;
} cascade_resources;

typedef struct shadow_shader_instance_data {
    u64 render_frame_number;
    u8 render_draw_index;
} shadow_shader_instance_data;

typedef struct shadow_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    shadow_rendergraph_node_config config;

    shader* s;
    u32 shader_id;
    shadow_shader_locations locations;

    // Custom projection matrix for shadow pass.
    viewport camera_viewport;

    // The depth texture used for the directional light shadow.
    kresource_texture* depth_texture;

    // One per cascade.
    cascade_resources cascade_resources[MAX_SHADOW_CASCADE_COUNT];

    // Track instance updates per frame
    b8* instance_updated;
    u32 instance_count;
    // Default map to be used when materials aren't available.
    kresource_texture_map default_colour_map;
    u32 default_instance_id;
    u64 default_instance_frame_number;

    // Track instance data per instance. darray
    shadow_shader_instance_data* instances;

    // Separate shader/instance info for terrains.
    shader* ts;
    u32 terrain_shader_id;
    shadow_shader_locations terrain_locations;

    const struct directional_light* light;
    // Per-cascade data.
    shadow_cascade_data cascade_data[MAX_SHADOW_CASCADE_COUNT];

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;
    u32 geometry_count;
    struct geometry_render_data* geometries;

} shadow_rendergraph_node_internal_data;

static b8 deserialize_config(const char* source_str, shadow_rendergraph_node_config* out_config);
/* static void destroy_config(shadow_rendergraph_node_config* config); */

b8 shadow_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self || !config) {
        KERROR("shadow_map_pass_create requires both a pointer to self and a valid config");
        return false;
    }

    self->internal_data = kallocate(sizeof(shadow_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->texture_system = engine_systems_get()->texture_system;
    if (!deserialize_config(config->config_str, &internal_data->config)) {
        KERROR("Failed to deserialize configuration for shadow_rendergraph_node. Node creation failed.");
        return false;
    }

    // Has one source, for the shadowmap.
    self->source_count = 1;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* shadowmap_source = &self->sources[0];
    shadowmap_source->name = string_duplicate("shadowmap");
    shadowmap_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    shadowmap_source->value.t = 0;
    shadowmap_source->is_bound = false;

    // Function pointers.
    self->initialize = shadow_rendergraph_node_initialize;
    self->destroy = shadow_rendergraph_node_destroy;
    self->load_resources = shadow_rendergraph_node_load_resources;
    self->execute = shadow_rendergraph_node_execute;

    return true;
}

b8 shadow_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load shadowmap shader. Attempt to to get the already-loaded shader if it doesn't exist.
    internal_data->s = shader_system_get("Shader.Shadowmap");
    if (!internal_data->s) {
        KERROR("Shader for shadow rendergraph node failed to load. See logs for details.");
        return false;
    }
    internal_data->shader_id = internal_data->s->id;
    internal_data->locations.projections_location = shader_system_uniform_location(internal_data->shader_id, "projections");
    internal_data->locations.views_location = shader_system_uniform_location(internal_data->shader_id, "views");
    internal_data->locations.model_location = shader_system_uniform_location(internal_data->shader_id, "model");
    internal_data->locations.cascade_index_location = shader_system_uniform_location(internal_data->shader_id, "cascade_index");
    internal_data->locations.colour_map_location = shader_system_uniform_location(internal_data->shader_id, "colour_map");

    // Terrain shadowmap shader.
    internal_data->ts = shader_system_get("Shader.ShadowmapTerrain");
    if (!internal_data->ts) {
        KERROR("Failed to load shader for shadowmap rendergraph node (terrain)");
        return false;
    }

    internal_data->terrain_shader_id = internal_data->ts->id;
    internal_data->terrain_locations.projections_location = shader_system_uniform_location(internal_data->terrain_shader_id, "projections");
    internal_data->terrain_locations.views_location = shader_system_uniform_location(internal_data->terrain_shader_id, "views");
    internal_data->terrain_locations.model_location = shader_system_uniform_location(internal_data->terrain_shader_id, "model");
    internal_data->terrain_locations.cascade_index_location = shader_system_uniform_location(internal_data->terrain_shader_id, "cascade_index");

    return true;
}

b8 shadow_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Static meshes.
    {
        // Create a texture map to be used across the board for the diffuse/albedo transparency sample.
        internal_data->default_colour_map.mip_levels = 1;
        internal_data->default_colour_map.generation = INVALID_ID_U8;
        internal_data->default_colour_map.repeat_u = internal_data->default_colour_map.repeat_v = internal_data->default_colour_map.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
        internal_data->default_colour_map.filter_minify = internal_data->default_colour_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;

        // Grab the default texture for the default texture map.
        internal_data->default_colour_map.texture = texture_system_get_default_kresource_diffuse_texture(internal_data->texture_system);

        // Acquire resources for the default texture map.
        if (!renderer_kresource_texture_map_resources_acquire(internal_data->renderer, &internal_data->default_colour_map)) {
            KERROR("Failed to acquire texture map resources for default colour map in shadowmap pass.");
            return false;
        }

        // Reserve an instance id for the default "material" to render to.
        {
            kresource_texture_map* maps[1] = {&internal_data->default_colour_map};
            /* shader* s = internal_data->s; */
            /* u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index; */
            shader_instance_resource_config instance_resource_config = {0};
            // Map count for this type is known.
            shader_instance_uniform_texture_config colour_texture = {0};
            /* colour_texture.uniform_location = atlas_location; */
            colour_texture.kresource_texture_map_count = 1;
            colour_texture.kresource_texture_maps = maps;

            instance_resource_config.uniform_config_count = 1;
            instance_resource_config.uniform_configs = &colour_texture;
            renderer_shader_instance_resources_acquire(internal_data->renderer, internal_data->s, &instance_resource_config, &internal_data->default_instance_id);
        }
    }

    // NOTE: Setup a default viewport. The only component that is used for this is the underlying
    // viewport rect, but is required to be set by the renderer before beginning a renderpass.
    // The projection matrix within this is not used, therefore the fov and clip planes do not matter.
    vec4 viewport_rect = {0, 0, internal_data->config.resolution, internal_data->config.resolution};
    if (!viewport_create(viewport_rect, 0.0f, 0.0f, 0.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC, &internal_data->camera_viewport)) {
        KERROR("Failed to create viewport for shadow map pass.");
        return false;
    }

    // Create the depth attachment for the directional light shadow.
    // This should take renderer buffering into account.
    internal_data->depth_texture = texture_system_request_depth_arrayed(kname_create("__shadow_rg_node_shadowmap__"), internal_data->config.resolution, internal_data->config.resolution, MAX_SHADOW_CASCADE_COUNT);
    if (!internal_data->depth_texture) {
        KERROR("Failed to request layered shadow map texture for shadow rendergraph node.");
        return false;
    }

    return true;
}

b8 shadow_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    renderer_begin_debug_label("shadow rendergraph node", (vec3){1.0f, 0.0f, 0.0f});

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Clear the image first.
    renderer_clear_depth_stencil(engine_systems_get()->renderer_system, internal_data->depth_texture->renderer_texture_handle);

    // One renderpass per cascade - directional light.
    for (u32 p = 0; p < MAX_SHADOW_CASCADE_COUNT; ++p) {
        const char* label_text = string_format("shadow_rendergraph_cascade_%u", p);
        renderer_begin_debug_label(label_text, (vec3){1.0f - (p * 0.2f), 0.0f, 0.0f});
        string_free(label_text);

        rect_2d render_area = (rect_2d){0, 0, internal_data->config.resolution, internal_data->config.resolution};
        renderer_begin_rendering(internal_data->renderer, p_frame_data, render_area, 0, 0, internal_data->depth_texture->renderer_texture_handle, p);

        // Bind the internal viewport - do not use one provided in pass data.
        renderer_active_viewport_set(&internal_data->camera_viewport);

        // Use the standard shadowmap shader.
        shader_system_use_by_id(internal_data->s->id);

        // Apply globals, once per cascade.
        // LEFTOFF: Why is this being looped through *again*? This is already in a loop above.
        // Is it because we can only update the globals once?
        b8 needs_update = p == 0;
        if (needs_update) {
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->shader_id, internal_data->locations.projections_location, i, &internal_data->cascade_data[i].projection)) {
                    KERROR("Failed to apply shadowmap projection uniform.");
                    return false;
                }
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->shader_id, internal_data->locations.views_location, i, &internal_data->cascade_data[i].view)) {
                    KERROR("Failed to apply shadowmap view uniform.");
                    return false;
                }
            }
        }
        shader_system_apply_global(internal_data->shader_id);

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
                kresource_texture_map* maps[1] = {&internal_data->default_colour_map};
                /* shader* s = internal_data->s; */
                /* u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index; */
                shader_instance_resource_config instance_resource_config = {0};
                // Map count for this type is known.
                shader_instance_uniform_texture_config colour_texture = {0};
                /* colour_texture.uniform_location = atlas_location; */
                colour_texture.kresource_texture_map_count = 1;
                colour_texture.kresource_texture_maps = maps;

                instance_resource_config.uniform_config_count = 1;
                instance_resource_config.uniform_configs = &colour_texture;
                renderer_shader_instance_resources_acquire(internal_data->renderer, internal_data->s, &instance_resource_config, &instance_id);

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
                kresource_texture_map* bind_map = 0;

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
                } else {
                    // Use the default instance.
                    bind_id = internal_data->default_instance_id;
                    // Use the default colour map.
                    bind_map = &internal_data->default_colour_map;
                }

                // Use the bindings.
                shader_system_bind_instance(internal_data->shader_id, bind_id);
                if (!shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->locations.colour_map_location, bind_map)) {
                    KERROR("Failed to apply shadowmap color_map uniform to static geometry.");
                    return false;
                }
                shader_system_apply_instance(internal_data->shader_id);

                // Apply the locals
                shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->locations.model_location, &g->model);
                shader_system_uniform_set_by_location(internal_data->shader_id, internal_data->locations.cascade_index_location, &p);
                shader_system_apply_local(internal_data->shader_id);

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
            shader_system_use_by_id(internal_data->terrain_shader_id);

            // Apply globals, once per cascade.
            if (needs_update) {
                for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                    // NOTE: using the internal projection matrix, not one passed in.
                    if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.projections_location, i, &internal_data->cascade_data[i].projection)) {
                        KERROR("Failed to apply terrain shadowmap projection uniform.");
                        return false;
                    }
                    if (!shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.views_location, i, &internal_data->cascade_data[i].view)) {
                        KERROR("Failed to apply terrain shadowmap view uniform.");
                        return false;
                    }
                }
            }
            shader_system_apply_global(internal_data->terrain_shader_id);

            for (u32 i = 0; i < internal_data->terrain_geometry_count; ++i) {
                geometry_render_data* terrain = &internal_data->terrain_geometries[i];

                // Apply the locals
                shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.model_location, &terrain->model);
                shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.cascade_index_location, &p);
                shader_system_apply_local(internal_data->terrain_shader_id);

                // Draw it.
                renderer_geometry_draw(terrain);
            }
        }

        renderer_end_rendering(internal_data->renderer, p_frame_data);

        renderer_end_debug_label();

    } // End cascade pass

    // Prepare the image to be sampled from.
    renderer_texture_prepare_for_sampling(internal_data->renderer, internal_data->depth_texture->renderer_texture_handle, internal_data->depth_texture->flags);

    renderer_end_debug_label();

    return true;
}

void shadow_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->internal_data) {
            shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

            texture_system_release_resource(internal_data->depth_texture);

            renderer_kresource_texture_map_resources_release(internal_data->renderer, &internal_data->default_colour_map);

            renderer_shader_instance_resources_release(internal_data->renderer, internal_data->s, internal_data->default_instance_id);

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

b8 shadow_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "shadow";
    factory.create = shadow_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}

static b8 deserialize_config(const char* source_str, shadow_rendergraph_node_config* out_config) {
    if (!source_str || !string_length(source_str) || !out_config) {
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(source_str, &tree)) {
        KERROR("Failed to parse config for shadow_rendergraph_node.");
        return false;
    }

    i64 resolution;
    b8 result = kson_object_property_value_get_int(&tree.root, "resolution", &resolution);
    if (!result) {
        // Use a default resolution if not defined.
        resolution = 1024;
    }
    out_config->resolution = (u16)resolution;

    kson_tree_cleanup(&tree);

    return result;
}
