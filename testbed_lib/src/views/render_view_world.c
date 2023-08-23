#include "render_view_world.h"

#include "containers/darray.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "resources/skybox.h"
#include "resources/terrain.h"
#include "systems/camera_system.h"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/render_view_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

typedef struct debug_colour_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_colour_shader_locations;

typedef struct skybox_shader_locations {
    u16 projection_location;
    u16 view_location;
    u16 cube_map_location;
} skybox_shader_locations;

typedef struct render_view_world_internal_data {
    shader* material_shader;
    shader* skybox_shader;
    shader* terrain_shader;
    shader* colour_shader;

    vec4 ambient_colour;
    u32 render_mode;

    debug_colour_shader_locations debug_locations;
    skybox_shader_locations skybox_locations;

} render_view_world_internal_data;

/** @brief A private structure used to sort geometry by distance from the camera. */
typedef struct geometry_distance {
    /** @brief The geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

typedef struct material_info {
    vec4 diffuse_colour;
    float shininess;
    vec3 padding;
} material_info;

/**
 * @brief A private, recursive, in-place sort function for geometry_distance structures.
 *
 * @param arr The array of geometry_distance structures to be sorted.
 * @param low_index The low index to start the sort from (typically 0)
 * @param high_index The high index to end with (typically the array length - 1)
 * @param ascending True to sort in ascending order; otherwise descending.
 */
static void quick_sort(geometry_distance arr[], i32 low_index, i32 high_index, b8 ascending);

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }
    render_view_world_internal_data* data = (render_view_world_internal_data*)self->internal_data;
    if (!data) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_SET_RENDER_MODE: {
            i32 mode = context.data.i32[0];
            switch (mode) {
                default:
                case RENDERER_VIEW_MODE_DEFAULT:
                    KDEBUG("Renderer mode set to default.");
                    data->render_mode = RENDERER_VIEW_MODE_DEFAULT;
                    break;
                case RENDERER_VIEW_MODE_LIGHTING:
                    KDEBUG("Renderer mode set to lighting.");
                    data->render_mode = RENDERER_VIEW_MODE_LIGHTING;
                    break;
                case RENDERER_VIEW_MODE_NORMALS:
                    KDEBUG("Renderer mode set to normals.");
                    data->render_mode = RENDERER_VIEW_MODE_NORMALS;
                    break;
            }
            return true;
        }
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            render_view_system_render_targets_regenerate(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}

b8 render_view_world_on_registered(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_world_internal_data), MEMORY_TAG_RENDERER);
        render_view_world_internal_data* data = self->internal_data;

        // TODO: move to material system and get a reference here instead.
        // Builtin material shader.
        const char* material_shader_name = "Shader.Builtin.Material";
        resource material_config_resource;
        if (!resource_system_load(material_shader_name, RESOURCE_TYPE_SHADER, 0, &material_config_resource)) {
            KERROR("Failed to load builtin material shader resource.");
            return false;
        }
        shader_config* config = (shader_config*)material_config_resource.data;
        // NOTE: Second pass is the world pass.
        if (!shader_system_create(&self->passes[1], config)) {
            KERROR("Failed to load builtin material shader.");
            return false;
        }
        resource_system_unload(&material_config_resource);
        // Save off a pointer to the material shader.
        data->material_shader = shader_system_get(material_shader_name);

        // Load skybox shader.
        const char* skybox_shader_name = "Shader.Builtin.Skybox";
        resource skybox_shader_config_resource;
        if (!resource_system_load(skybox_shader_name, RESOURCE_TYPE_SHADER, 0, &skybox_shader_config_resource)) {
            KERROR("Failed to load builtin skybox shader.");
            return false;
        }
        shader_config* skybox_shader_config = (shader_config*)skybox_shader_config_resource.data;
        // NOTE: FIRST pass is the skybox pass.
        if (!shader_system_create(&self->passes[0], skybox_shader_config)) {
            KERROR("Failed to load builtin skybox shader.");
            return false;
        }

        resource_system_unload(&skybox_shader_config_resource);
        // Get a pointer to the shader.
        data->skybox_shader = shader_system_get(skybox_shader_name);
        data->skybox_locations.projection_location = shader_system_uniform_index(data->skybox_shader, "projection");
        data->skybox_locations.view_location = shader_system_uniform_index(data->skybox_shader, "view");
        data->skybox_locations.cube_map_location = shader_system_uniform_index(data->skybox_shader, "cube_texture");

        // Load terrain shader.
        const char* terrain_shader_name = "Shader.Builtin.Terrain";
        resource terrain_shader_config_resource;
        if (!resource_system_load(terrain_shader_name, RESOURCE_TYPE_SHADER, 0, &terrain_shader_config_resource)) {
            KERROR("Failed to load builtin terrain shader resource.");
            return false;
        }
        shader_config* terrain_shader_config = (shader_config*)terrain_shader_config_resource.data;
        // NOTE: Second pass is the world pass.
        if (!shader_system_create(&self->passes[1], terrain_shader_config)) {
            KERROR("Failed to load builtin terrain shader.");
            return false;
        }
        resource_system_unload(&terrain_shader_config_resource);
        // Save off a pointer to the terrain shader.
        data->terrain_shader = shader_system_get(terrain_shader_name);

        // Load debug colour3d shader.
        // TODO: move builtin shaders to the shader system itself.
        const char* colour3d_shader_name = "Shader.Builtin.ColourShader3D";
        resource colour3d_shader_config_resource;
        if (!resource_system_load(colour3d_shader_name, RESOURCE_TYPE_SHADER, 0, &colour3d_shader_config_resource)) {
            KERROR("Failed to load builtin colour3d shader resource.");
            return false;
        }
        shader_config* colour3d_shader_config = (shader_config*)colour3d_shader_config_resource.data;
        // NOTE: Second pass is the world pass.
        if (!shader_system_create(&self->passes[1], colour3d_shader_config)) {
            KERROR("Failed to load builtin colour3d shader.");
            return false;
        }
        resource_system_unload(&colour3d_shader_config_resource);

        // Save off a pointer to the colour shader.
        data->colour_shader = shader_system_get(colour3d_shader_name);
        // Get colour3d shader uniform locations.
        {
            data->debug_locations.projection = shader_system_uniform_index(data->colour_shader, "projection");
            data->debug_locations.view = shader_system_uniform_index(data->colour_shader, "view");
            data->debug_locations.model = shader_system_uniform_index(data->colour_shader, "model");
        }

        // TODO: Obtain from scene
        data->ambient_colour = (vec4){0.25f, 0.25f, 0.25f, 1.0f};

        // Listen for mode changes.
        if (!event_register(EVENT_CODE_SET_RENDER_MODE, self, render_view_on_event)) {
            KERROR("Unable to listen for render mode set event, creation failed.");
            return false;
        }

        if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
            KERROR("Unable to listen for refresh required event, creation failed.");
            return false;
        }
        return true;
    }

    KERROR("render_view_world_on_create - Requires a valid pointer to a view.");
    return false;
}

void render_view_world_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        event_unregister(EVENT_CODE_SET_RENDER_MODE, self, render_view_on_event);

        // Unregister from the event.
        event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

        kfree(self->internal_data, sizeof(render_view_world_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}

void render_view_world_on_resize(struct render_view* self, u32 width, u32 height) {
    // Check if different. If so, regenerate projection matrix.
    if (width != self->width || height != self->height) {
        // render_view_world_internal_data* data = self->internal_data;

        self->width = width;
        self->height = height;
    }
}

b8 render_view_world_on_packet_build(const struct render_view* self, struct frame_data* frame_data, viewport* v, camera* c, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_world_on_build_packet requires valid pointer to view, packet, and data.");
        return false;
    }

    render_view_world_data* world_data = (render_view_world_data*)data;
    render_view_world_internal_data* internal_data = (render_view_world_internal_data*)self->internal_data;

    // TODO: Use frame allocator.
    out_packet->geometries = darray_create(geometry_render_data);
    out_packet->terrain_geometries = darray_create(geometry_render_data);
    out_packet->debug_geometries = darray_create(geometry_render_data);
    out_packet->view = self;
    out_packet->vp = v;

    // Set matrices, etc.
    out_packet->projection_matrix = v->projection;
    out_packet->view_matrix = camera_view_get(c);
    out_packet->view_position = camera_position_get(c);
    out_packet->ambient_colour = internal_data->ambient_colour;

    // Skybox data.
    out_packet->skybox_data = world_data->skybox_data;

    // Obtain all geometries from the current scene.

    geometry_distance* geometry_distances = darray_create(geometry_distance);

    u32 geometry_data_count = darray_length(world_data->world_geometries);
    for (u32 i = 0; i < geometry_data_count; ++i) {
        geometry_render_data* g_data = &world_data->world_geometries[i];
        if (!g_data->geometry) {
            continue;
        }

        // TODO: Add something to material to check for transparency.
        b8 has_transparency = false;
        if (g_data->geometry->material->type == MATERIAL_TYPE_PHONG) {
            // Check diffuse map (slot 0).
            has_transparency = ((g_data->geometry->material->maps[0].texture->flags & TEXTURE_FLAG_HAS_TRANSPARENCY) == 0);
        }

        if (has_transparency) {
            // Only add meshes with _no_ transparency.
            darray_push(out_packet->geometries, world_data->world_geometries[i]);
            out_packet->geometry_count++;
        } else {
            // For meshes _with_ transparency, add them to a separate list to be sorted by distance later.
            // Get the center, extract the global position from the model matrix and add it to the center,
            // then calculate the distance between it and the camera, and finally save it to a list to be sorted.
            // NOTE: This isn't perfect for translucent meshes that intersect, but is enough for our purposes now.
            vec3 center = vec3_transform(g_data->geometry->center, 1.0f, g_data->model);
            f32 distance = vec3_distance(center, c->position);

            geometry_distance gdist;
            gdist.distance = kabs(distance);
            gdist.g = world_data->world_geometries[i];

            darray_push(geometry_distances, gdist);
        }
    }

    // Sort the distances
    u32 geometry_count = darray_length(geometry_distances);
    quick_sort(geometry_distances, 0, geometry_count - 1, false);

    // Add them to the packet geometry.
    for (u32 i = 0; i < geometry_count; ++i) {
        darray_push(out_packet->geometries, geometry_distances[i].g);
        out_packet->geometry_count++;
    }

    u32 terrain_count = darray_length(world_data->terrain_geometries);
    for (u32 i = 0; i < terrain_count; ++i) {
        darray_push(out_packet->terrain_geometries, world_data->terrain_geometries[i]);
        out_packet->terrain_geometry_count++;
    }

    // Debug geometries.
    u32 debug_geometry_count = darray_length(world_data->debug_geometries);
    for (u32 i = 0; i < debug_geometry_count; ++i) {
        darray_push(out_packet->debug_geometries, world_data->debug_geometries[i]);
        out_packet->debug_geometry_count++;
    }

    // Clean up.
    darray_destroy(geometry_distances);

    return true;
}

void render_view_world_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet) {
    darray_destroy(packet->geometries);
    darray_destroy(packet->terrain_geometries);
    darray_destroy(packet->debug_geometries);
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_world_on_render(const struct render_view* self, const struct render_view_packet* packet, struct frame_data* p_frame_data) {
    render_view_world_internal_data* data = self->internal_data;

    // Bind the viewport
    renderer_active_viewport_set(packet->vp);

    // Skybox renderpass.
    {
        renderpass* pass = &self->passes[0];
        if (!renderer_renderpass_begin(pass, &pass->targets[p_frame_data->render_target_index])) {
            KERROR("render_view_world_on_render skybox pass failed to start.");
            return false;
        }

        // Skybox first.
        if (packet->skybox_data.sb) {
            shader_system_use_by_id(data->skybox_shader->id);

            // Get the view matrix, but zero out the position so the skybox stays put on screen.
            mat4 view_matrix = packet->view_matrix;
            view_matrix.data[12] = 0.0f;
            view_matrix.data[13] = 0.0f;
            view_matrix.data[14] = 0.0f;

            // Apply globals
            renderer_shader_bind_globals(data->skybox_shader);
            if (!shader_system_uniform_set_by_index(data->skybox_locations.projection_location, &packet->projection_matrix)) {
                KERROR("Failed to apply skybox projection uniform.");
                return false;
            }
            if (!shader_system_uniform_set_by_index(data->skybox_locations.view_location, &view_matrix)) {
                KERROR("Failed to apply skybox view uniform.");
                return false;
            }
            shader_system_apply_global(true);

            // Instance
            shader_system_bind_instance(packet->skybox_data.sb->instance_id);
            if (!shader_system_uniform_set_by_index(data->skybox_locations.cube_map_location, &packet->skybox_data.sb->cubemap)) {
                KERROR("Failed to apply skybox cube map uniform.");
                return false;
            }
            b8 needs_update = packet->skybox_data.sb->render_frame_number != p_frame_data->renderer_frame_number || packet->skybox_data.sb->draw_index != p_frame_data->draw_index;
            shader_system_apply_instance(needs_update);

            // Sync the frame number and draw index.
            packet->skybox_data.sb->render_frame_number = p_frame_data->renderer_frame_number;
            packet->skybox_data.sb->draw_index = p_frame_data->draw_index;

            // Draw it.
            geometry_render_data render_data = {};
            render_data.geometry = packet->skybox_data.sb->g;
            renderer_geometry_draw(&render_data);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_world_on_render skybox pass failed to end.");
            return false;
        }
    }

    // World renderpass.
    {
        renderpass* pass = &self->passes[1];
        if (!renderer_renderpass_begin(pass, &pass->targets[p_frame_data->render_target_index])) {
            KERROR("render_view_world_on_render world pass failed to start.");
            return false;
        }
        // Use the appropriate shader and apply the global uniforms.
        u32 terrain_count = packet->terrain_geometry_count;
        if (terrain_count > 0) {
            shader_system_use_by_id(data->terrain_shader->id);

            // Apply globals
            // TODO: Find a generic way to request data such as ambient colour (which should be from a scene),
            // and mode (from the renderer)
            if (!material_system_apply_global(data->terrain_shader->id, p_frame_data, &packet->projection_matrix, &packet->view_matrix, &packet->ambient_colour, &packet->view_position, data->render_mode)) {
                KERROR("Failed to use apply globals for terrain shader. Render frame failed.");
                return false;
            }

            for (u32 i = 0; i < terrain_count; ++i) {
                material* m = 0;
                if (packet->terrain_geometries[i].geometry->material) {
                    m = packet->terrain_geometries[i].geometry->material;
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
                material_system_apply_local(m, &packet->terrain_geometries[i].model);

                // Draw it.
                renderer_geometry_draw(&packet->terrain_geometries[i]);
            }
        }

        // Static geometries.
        u32 geometry_count = packet->geometry_count;
        if (geometry_count > 0) {
            if (!shader_system_use_by_id(data->material_shader->id)) {
                KERROR("Failed to use material shader. Render frame failed.");
                return false;
            }

            // Apply globals
            // TODO: Find a generic way to request data such as ambient colour (which should be from a scene),
            // and mode (from the renderer)
            if (!material_system_apply_global(data->material_shader->id, p_frame_data, &packet->projection_matrix, &packet->view_matrix, &packet->ambient_colour, &packet->view_position, data->render_mode)) {
                KERROR("Failed to use apply globals for material shader. Render frame failed.");
                return false;
            }

            // Draw geometries.
            u32 count = packet->geometry_count;
            for (u32 i = 0; i < count; ++i) {
                material* m = 0;
                if (packet->geometries[i].geometry->material) {
                    m = packet->geometries[i].geometry->material;
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
                material_system_apply_local(m, &packet->geometries[i].model);

                // Invert if needed
                if (packet->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
                }

                // Draw it.
                renderer_geometry_draw(&packet->geometries[i]);

                // Change back if needed
                if (packet->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
                }
            }
        }

        // Debug geometries (i.e. grids, lines, boxes, gizmos, etc.)
        // This goes through the same geometry system as anything else.
        u32 debug_geometry_count = packet->debug_geometry_count;
        if (debug_geometry_count > 0) {
            shader_system_use_by_id(data->colour_shader->id);

            // Globals
            shader_system_uniform_set_by_index(data->debug_locations.projection, &packet->projection_matrix);
            shader_system_uniform_set_by_index(data->debug_locations.view, &packet->view_matrix);

            shader_system_apply_global(true);

            // Each geometry.
            for (u32 i = 0; i < debug_geometry_count; ++i) {
                // NOTE: No instance-level uniforms to be set.

                // Local
                shader_system_uniform_set_by_index(data->debug_locations.model, &packet->debug_geometries[i].model);

                // Draw it.
                renderer_geometry_draw(&packet->debug_geometries[i]);
            }

            // HACK: This should be handled somehow, every frame, by the shader system.
            data->colour_shader->render_frame_number = p_frame_data->renderer_frame_number;
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_world_on_render world pass failed to end.");
            return false;
        }
    }

    return true;
}

// Quicksort for geometry_distance

static void swap(geometry_distance* a, geometry_distance* b) {
    geometry_distance temp = *a;
    *a = *b;
    *b = temp;
}

static i32 partition(geometry_distance arr[], i32 low_index, i32 high_index, b8 ascending) {
    geometry_distance pivot = arr[high_index];
    i32 i = (low_index - 1);

    for (i32 j = low_index; j <= high_index - 1; ++j) {
        if (ascending) {
            if (arr[j].distance < pivot.distance) {
                ++i;
                swap(&arr[i], &arr[j]);
            }
        } else {
            if (arr[j].distance > pivot.distance) {
                ++i;
                swap(&arr[i], &arr[j]);
            }
        }
    }
    swap(&arr[i + 1], &arr[high_index]);
    return i + 1;
}

static void quick_sort(geometry_distance arr[], i32 low_index, i32 high_index, b8 ascending) {
    if (low_index < high_index) {
        i32 partition_index = partition(arr, low_index, high_index, ascending);

        // Independently sort elements before and after the partition index.
        quick_sort(arr, low_index, partition_index - 1, ascending);
        quick_sort(arr, partition_index + 1, high_index, ascending);
    }
}
