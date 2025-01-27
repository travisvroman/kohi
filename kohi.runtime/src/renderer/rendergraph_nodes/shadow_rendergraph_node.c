#include "shadow_rendergraph_node.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include <runtime_defines.h>

// Locations of uniforms within the static mesh shader.
typedef struct shadow_staticmesh_shader_locations {
    u16 view_projections;
    u16 model;
    u16 cascade_index;
    u16 base_colour_texture;
    u16 base_colour_sampler;
} shadow_staticmesh_shader_locations;

typedef struct shadow_shader_group_data {
    khandle base_material;
    u32 group_id;
} shadow_shader_group_data;

typedef struct shader_per_draw_data {
    u32 draw_id;
} shader_per_draw_data;

typedef struct shadow_terrain_shader_locations {
    u16 view_projections;
    u16 model;
    u16 cascade_index;
} shadow_terrain_shader_locations;

typedef struct shadow_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct material_system_state* material_system;
    shadow_rendergraph_node_config config;

    // Custom projection matrix for shadow pass.
    viewport camera_viewport;

    // The depth texture used for the directional light shadow.
    kresource_texture* depth_texture;

    // Static mesh shader and locations.
    khandle shadow_staticmesh_shader;
    shadow_staticmesh_shader_locations staticmesh_shader_locations;

    // A pointer to the default base colour texture to be used when rendering opaque static meshes.
    kresource_texture* default_base_colour_texture;
    // Holds the id for the default static mesh shader group.
    shadow_shader_group_data default_group;

    // Track per-group data. darray
    shadow_shader_group_data* staticmesh_groups;

    // Track per-draw data. darray
    shader_per_draw_data* staticmesh_per_draw_data;

    // Separate shader/instance info for terrains.
    khandle shadow_terrain_shader;
    shadow_terrain_shader_locations terrain_shader_locations;

    // Track per-draw data. darray
    shader_per_draw_data* terrain_per_draw_data;

    const struct directional_light* light;
    // Per-cascade data.
    shadow_cascade_data cascade_data[MATERIAL_MAX_SHADOW_CASCADES];

    // Collection of static meshes geometries to be rendered for a frame. Reset every frame. Uses frame allocator.
    u32 static_mesh_geometry_count;
    struct geometry_render_data* static_mesh_geometries;

    // Collection of terrain geometries to be rendered for a frame. Reset every frame. Uses frame allocator.
    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;

} shadow_rendergraph_node_internal_data;

static b8 deserialize_config(const char* source_str, shadow_rendergraph_node_config* out_config);

b8 shadow_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self || !config) {
        KERROR("shadow_map_pass_create requires both a pointer to self and a valid config");
        return false;
    }

    self->internal_data = kallocate(sizeof(shadow_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->texture_system = engine_systems_get()->texture_system;
    internal_data->material_system = engine_systems_get()->material_system;
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

    internal_data->staticmesh_groups = darray_create(shadow_shader_group_data);
    internal_data->staticmesh_per_draw_data = darray_create(shader_per_draw_data);
    internal_data->terrain_per_draw_data = darray_create(shader_per_draw_data);

    return true;
}

b8 shadow_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load static mesh shadowmap shader.
    internal_data->shadow_staticmesh_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_SHADOW_STATICMESH), kname_create(PACKAGE_NAME_RUNTIME));
    if (khandle_is_invalid(internal_data->shadow_staticmesh_shader)) {
        KERROR("Static mesh shadow shader for shadow rendergraph node failed to load. See logs for details.");
        return false;
    }
    internal_data->staticmesh_shader_locations.view_projections = shader_system_uniform_location(internal_data->shadow_staticmesh_shader, kname_create("view_projections"));
    internal_data->staticmesh_shader_locations.model = shader_system_uniform_location(internal_data->shadow_staticmesh_shader, kname_create("model"));
    internal_data->staticmesh_shader_locations.cascade_index = shader_system_uniform_location(internal_data->shadow_staticmesh_shader, kname_create("cascade_index"));
    internal_data->staticmesh_shader_locations.base_colour_texture = shader_system_uniform_location(internal_data->shadow_staticmesh_shader, kname_create("base_colour_texture"));
    internal_data->staticmesh_shader_locations.base_colour_sampler = shader_system_uniform_location(internal_data->shadow_staticmesh_shader, kname_create("base_colour_sampler"));

    // Load terrain shadowmap shader.
    internal_data->shadow_terrain_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_SHADOW_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
    if (khandle_is_invalid(internal_data->shadow_terrain_shader)) {
        KERROR("Static terrain shader for shadow rendergraph node failed to load. See logs for details.");
        return false;
    }

    internal_data->terrain_shader_locations.view_projections = shader_system_uniform_location(internal_data->shadow_terrain_shader, kname_create("view_projections"));
    internal_data->terrain_shader_locations.model = shader_system_uniform_location(internal_data->shadow_terrain_shader, kname_create("model"));
    internal_data->terrain_shader_locations.cascade_index = shader_system_uniform_location(internal_data->shadow_terrain_shader, kname_create("cascade_index"));

    return true;
}

b8 shadow_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }
    shadow_rendergraph_node_internal_data* internal_data = self->internal_data;

    // NOTE: For static meshes, the alpha of transparent materials needs to be taken into
    // account when casting shadows. This means these each need a distinct group per distinct material.
    // Fully-opaque objects can be rendered using the same default opaque texture, and thus can all
    // be rendered under the same group.
    // Since terrains will never be transparent, they can all be rendered without using a texture at all.

    internal_data->default_base_colour_texture = texture_system_request(kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME), INVALID_KNAME, 0, 0);
    if (!internal_data->default_base_colour_texture) {
        KERROR("Failed to load default base colour texture when initializing shadow rendergraph node.");
        return false;
    }

    if (!shader_system_shader_group_acquire(internal_data->shadow_staticmesh_shader, &internal_data->default_group.group_id)) {
        KERROR("Failed to obtain group shader resources when initializing shadow rendergraph node.");
        return false;
    }

    // NOTE: Setup a default viewport. The only component that is used for this is the underlying
    // viewport rect, but is required to be set by the renderer before beginning a renderpass.
    // The projection matrix within this is not used, therefore the fov and clip planes do not matter.
    vec4 viewport_rect = {0, 0, internal_data->config.resolution, internal_data->config.resolution};
    if (!viewport_create(viewport_rect, 0.0f, 0.0f, 100.0f, RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC, &internal_data->camera_viewport)) {
        KERROR("Failed to create viewport for shadow map pass.");
        return false;
    }

    // Create the depth attachment for the directional light shadow.
    // This should take renderer buffering into account.
    internal_data->depth_texture = texture_system_request_depth_arrayed(kname_create("__shadow_rg_node_shadowmap__"), internal_data->config.resolution, internal_data->config.resolution, MATERIAL_MAX_SHADOW_CASCADES, false, true);
    if (!internal_data->depth_texture) {
        KERROR("Failed to request layered shadow map texture for shadow rendergraph node.");
        return false;
    }
    // Bind it to the source.
    rendergraph_source* shadowmap_source = &self->sources[0];
    shadowmap_source->value.t = internal_data->depth_texture;

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
    for (u32 p = 0; p < MATERIAL_MAX_SHADOW_CASCADES; ++p) {
        {
            const char* label_text = string_format("shadow_rendergraph_cascade_%u", p);
            renderer_begin_debug_label(label_text, (vec3){0.8f - (p * 0.1f), 0.0f, 0.0f});
            string_free(label_text);
        }

        rect_2d render_area = (rect_2d){0, 0, internal_data->config.resolution, internal_data->config.resolution};
        renderer_begin_rendering(internal_data->renderer, p_frame_data, render_area, 0, 0, internal_data->depth_texture->renderer_texture_handle, p);

        // Bind the internal viewport - do not use one provided in pass data.
        renderer_active_viewport_set(&internal_data->camera_viewport);

        // Apply per-frame updates first.
        {
            renderer_begin_debug_label("shadow_rendergraph_staticmesh_per_frame", (vec3){1.0f, 0.0f, 0.0f});

            // Use the standard shadowmap shader.
            shader_system_use(internal_data->shadow_staticmesh_shader);
            shader_system_bind_frame(internal_data->shadow_staticmesh_shader);

            for (u32 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->shadow_staticmesh_shader, internal_data->staticmesh_shader_locations.view_projections, i, &internal_data->cascade_data[i].view_projection)) {
                    KERROR("Failed to apply static mesh shadowmap projection uniform (index=%u).", i);
                    return false;
                }
            }
            // Apply per-frame uniforms.
            shader_system_apply_per_frame(internal_data->shadow_staticmesh_shader);

            renderer_end_debug_label();
        }

        // Reset material handle group data for all entries. _NOT_ the group_ids though!
        {
            u32 group_count = darray_length(internal_data->staticmesh_groups);
            for (u32 g = 0; g < group_count; ++g) {
                shadow_shader_group_data* group = &internal_data->staticmesh_groups[g];
                group->base_material.handle_index = INVALID_ID;
            }
        }

        // Ensure there are enough static mesh per-draw resources for the frame.
        {
            i64 required_per_draw_count = internal_data->static_mesh_geometry_count;
            u32 current_per_draw_count = darray_length(internal_data->staticmesh_per_draw_data);
            i64 per_draw_diff = current_per_draw_count - required_per_draw_count;
            if (per_draw_diff < 0) {
                // Add the new entries for the difference, requesting draw resources along the way.
                for (u32 i = current_per_draw_count; i < required_per_draw_count; ++i) {
                    shader_per_draw_data new_per_draw = {0};
                    if (!shader_system_shader_per_draw_acquire(internal_data->shadow_staticmesh_shader, &new_per_draw.draw_id)) {
                        KERROR("Failed to acquire per-draw resources from the static mesh shadow shader. See logs for details.");
                        return false;
                    }
                    darray_push(internal_data->staticmesh_per_draw_data, new_per_draw);
                }
            }
        }

        // Prepare - Obtain enough shader resources for the frame. Do this by obtaining the count of unique
        // (but transparent) materials.
        for (u32 i = 0; i < internal_data->static_mesh_geometry_count; ++i) {
            geometry_render_data* geometry = &internal_data->static_mesh_geometries[i];
            material_instance mat_inst = geometry->material;
            shadow_shader_group_data* selected_group = 0;
            shader_per_draw_data* selected_per_draw = &internal_data->staticmesh_per_draw_data[i];
            b8 using_default = false;
            if (material_flag_get(internal_data->material_system, mat_inst.material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT)) {

                // Search the existing group data to see if this group has already been handled.
                u32 group_index = INVALID_ID;
                u32 group_count = darray_length(internal_data->staticmesh_groups);
                for (u32 g = 0; g < group_count; ++g) {
                    shadow_shader_group_data* group = &internal_data->staticmesh_groups[g];
                    if (group->base_material.handle_index == mat_inst.material.handle_index) {
                        // Exists already, move on to the next material.
                        group_index = g;
                        break;
                    }
                }

                if (group_index == INVALID_ID) {
                    // A unique material has been found. If a shader group already exists at this index, move on.
                    // If not, request group resources, and save it off.
                    // Find an "empty" slot, i.e. one with the group->base_material.handle_index = INVALID_ID.
                    for (u32 g = 0; g < group_count; ++g) {
                        shadow_shader_group_data* group = &internal_data->staticmesh_groups[g];
                        if (group->base_material.handle_index == INVALID_ID) {
                            // Found an empty slot. Use it, but don't request group resources.
                            group->base_material = mat_inst.material;

                            group_index = g;
                            break;
                        }
                    }

                    // If still not found, create a new entry (requesting group resources) and push into the darray.
                    if (group_index == INVALID_ID) {
                        shadow_shader_group_data new_group = {0};
                        new_group.base_material = mat_inst.material;
                        if (!shader_system_shader_group_acquire(internal_data->shadow_staticmesh_shader, &new_group.group_id)) {
                            KERROR("Failed to obtain group resources for rendering a transparent material. See logs for details.");
                            return false;
                        }
                        group_index = group_count;
                        darray_push(internal_data->staticmesh_groups, new_group);
                    }
                }

                selected_group = &internal_data->staticmesh_groups[group_index];
            } else {
                // For non-transparent materials, use the "default" group.
                selected_group = &internal_data->default_group;
                using_default = true;
            }

            // Update group uniforms.
            if (!shader_system_bind_group(internal_data->shadow_staticmesh_shader, selected_group->group_id)) {
                KERROR("Failed to bind static mesh shadow group id %u", selected_group->group_id);
                return false;
            }

            // Bind the appropriate texture.
            kresource_texture* base_colour_texture = using_default ? internal_data->default_base_colour_texture : material_texture_get(internal_data->material_system, selected_group->base_material, MATERIAL_TEXTURE_INPUT_BASE_COLOUR);
            if (!base_colour_texture) {
                // Failsafe in case the given material doesn't have a base colour texture.
                base_colour_texture = internal_data->default_base_colour_texture;
            }

            // Since this can (and likely will) change every frame, set this every time.
            if (!shader_system_uniform_set_by_location(internal_data->shadow_staticmesh_shader, internal_data->staticmesh_shader_locations.base_colour_texture, base_colour_texture)) {
                KERROR("Failed to apply static mesh shadowmap base_colour_texture uniform to static geometry.");
                return false;
            }

            if (!shader_system_apply_per_group(internal_data->shadow_staticmesh_shader)) {
                KERROR("Failed to apply static mesh shadowmap group id %u", selected_group->group_id);
                return false;
            }

            // Update per-draw uniforms.
            shader_system_bind_draw_id(internal_data->shadow_staticmesh_shader, selected_per_draw->draw_id);
            shader_system_uniform_set_by_location(internal_data->shadow_staticmesh_shader, internal_data->staticmesh_shader_locations.model, &geometry->model);
            shader_system_uniform_set_by_location(internal_data->shadow_staticmesh_shader, internal_data->staticmesh_shader_locations.cascade_index, &p);
            shader_system_apply_per_draw(internal_data->shadow_staticmesh_shader);

            // Invert if needed
            if (geometry->winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
            }

            // Draw it.
            renderer_geometry_draw(geometry);

            // Change back if needed
            if (geometry->winding_inverted) {
                renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
            }
        }

        // Terrain - use the terrain shadowmap shader.
        //
        // per-frame Terrain shadowmap shader
        {
            shader_system_use(internal_data->shadow_terrain_shader);

            shader_system_bind_frame(internal_data->shadow_terrain_shader);

            for (u32 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
                // NOTE: using the internal projection matrix, not one passed in.
                if (!shader_system_uniform_set_by_location_arrayed(internal_data->shadow_terrain_shader, internal_data->terrain_shader_locations.view_projections, i, &internal_data->cascade_data[i].view_projection)) {
                    KERROR("Failed to apply terrain shadowmap projection uniform (index=%u).", i);
                    return false;
                }
            }
            shader_system_apply_per_frame(internal_data->shadow_terrain_shader);
        }

        // Ensure there are enough terrain per-draw resources for the frame.
        {
            i64 required_per_draw_count = internal_data->terrain_geometry_count;
            u32 current_per_draw_count = darray_length(internal_data->terrain_per_draw_data);
            i64 per_draw_diff = current_per_draw_count - required_per_draw_count;
            if (per_draw_diff < 0) {
                // Add the new entries for the difference, requesting draw resources along the way.
                for (u32 i = current_per_draw_count; i < required_per_draw_count; ++i) {
                    shader_per_draw_data new_per_draw = {0};
                    if (!shader_system_shader_per_draw_acquire(internal_data->shadow_terrain_shader, &new_per_draw.draw_id)) {
                        KERROR("Failed to acquire per-draw resources from the terrain shadow shader. See logs for details.");
                        return false;
                    }
                    darray_push(internal_data->terrain_per_draw_data, new_per_draw);
                }
            }
        }

        {

            for (u32 i = 0; i < internal_data->terrain_geometry_count; ++i) {
                geometry_render_data* terrain = &internal_data->terrain_geometries[i];
                shader_per_draw_data* selected_per_draw = &internal_data->staticmesh_per_draw_data[i];

                // Apply the locals
                shader_system_bind_draw_id(internal_data->shadow_terrain_shader, selected_per_draw->draw_id);
                shader_system_uniform_set_by_location(internal_data->shadow_terrain_shader, internal_data->terrain_shader_locations.model, &terrain->model);
                shader_system_uniform_set_by_location(internal_data->shadow_terrain_shader, internal_data->terrain_shader_locations.cascade_index, &p);
                shader_system_apply_per_draw(internal_data->shadow_terrain_shader);

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
            texture_system_release_resource(internal_data->default_base_colour_texture);

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

    if (cascade_index > MATERIAL_MAX_SHADOW_CASCADES - 1) {
        KERROR("shadow_rendergraph_node_cascade_data_set index out of range. Expected [0-%d] but got %d.", MATERIAL_MAX_SHADOW_CASCADES - 1, cascade_index);
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
    internal_data->static_mesh_geometry_count = geometry_count;
    internal_data->static_mesh_geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
    kcopy_memory(internal_data->static_mesh_geometries, geometries, sizeof(geometry_render_data) * geometry_count);

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
