#include "render_view_world.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "memory/linear_allocator.h"
#include "containers/darray.h"
#include "systems/resource_system.h"
#include "systems/material_system.h"
#include "systems/render_view_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"
#include "renderer/renderer_frontend.h"

typedef struct render_view_world_internal_data {
    shader* s;
    f32 fov;
    f32 near_clip;
    f32 far_clip;
    mat4 projection_matrix;
    camera* world_camera;
    vec4 ambient_colour;
    u32 render_mode;
} render_view_world_internal_data;

/** @brief A private structure used to sort geometry by distance from the camera. */
typedef struct geometry_distance {
    /** @brief The geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

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
            render_view_system_regenerate_render_targets(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}

b8 render_view_world_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_world_internal_data), MEMORY_TAG_RENDERER);
        render_view_world_internal_data* data = self->internal_data;

        // TODO: move to material system and get a reference here instead.
        // Builtin material shader.
        const char* shader_name = "Shader.Builtin.Material";
        resource config_resource;
        if (!resource_system_load(shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin material shader.");
            return false;
        }
        shader_config* config = (shader_config*)config_resource.data;
        // NOTE: Assuming the first pass since that's all this view has.
        if (!shader_system_create(&self->passes[0], config)) {
            KERROR("Failed to load builtin material shader.");
            return false;
        }
        resource_system_unload(&config_resource);

        // Get either the custom shader override or the defined default.
        data->s = shader_system_get(self->custom_shader_name ? self->custom_shader_name : shader_name);
        // TODO: Set from configuration.
        data->near_clip = 0.1f;
        data->far_clip = 1000.0f;
        data->fov = deg_to_rad(45.0f);

        // Default
        data->projection_matrix = mat4_perspective(data->fov, 1280 / 720.0f, data->near_clip, data->far_clip);

        data->world_camera = camera_system_get_default();

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
        render_view_world_internal_data* data = self->internal_data;

        self->width = width;
        self->height = height;
        f32 aspect = (f32)self->width / self->height;
        data->projection_matrix = mat4_perspective(data->fov, aspect, data->near_clip, data->far_clip);

        for (u32 i = 0; i < self->renderpass_count; ++i) {
            self->passes[i].render_area.x = 0;
            self->passes[i].render_area.y = 0;
            self->passes[i].render_area.z = width;
            self->passes[i].render_area.w = height;
        }
    }
}

b8 render_view_world_on_build_packet(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_world_on_build_packet requires valid pointer to view, packet, and data.");
        return false;
    }

    geometry_render_data* geometry_data = (geometry_render_data*)data;
    render_view_world_internal_data* internal_data = (render_view_world_internal_data*)self->internal_data;

    out_packet->geometries = darray_create(geometry_render_data);
    out_packet->view = self;

    // Set matrices, etc.
    out_packet->projection_matrix = internal_data->projection_matrix;
    out_packet->view_matrix = camera_view_get(internal_data->world_camera);
    out_packet->view_position = camera_position_get(internal_data->world_camera);
    out_packet->ambient_colour = internal_data->ambient_colour;

    // Obtain all geometries from the current scene.

    geometry_distance* geometry_distances = darray_create(geometry_distance);

    u32 geometry_data_count = darray_length(geometry_data);
    for (u32 i = 0; i < geometry_data_count; ++i) {
        geometry_render_data* g_data = &geometry_data[i];
        if(!g_data->geometry) {
            continue;
        }
        
        // TODO: Add something to material to check for transparency.
        if ((g_data->geometry->material->diffuse_map.texture->flags & TEXTURE_FLAG_HAS_TRANSPARENCY) == 0) {
            // Only add meshes with _no_ transparency.
            darray_push(out_packet->geometries, geometry_data[i]);
            out_packet->geometry_count++;
        } else {
            // For meshes _with_ transparency, add them to a separate list to be sorted by distance later.
            // Get the center, extract the global position from the model matrix and add it to the center,
            // then calculate the distance between it and the camera, and finally save it to a list to be sorted.
            // NOTE: This isn't perfect for translucent meshes that intersect, but is enough for our purposes now.
            vec3 center = vec3_transform(g_data->geometry->center, g_data->model);
            f32 distance = vec3_distance(center, internal_data->world_camera->position);

            geometry_distance gdist;
            gdist.distance = kabs(distance);
            gdist.g = geometry_data[i];

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

    // Clean up.
    darray_destroy(geometry_distances);

    return true;
}

void render_view_world_on_destroy_packet(const struct render_view* self, struct render_view_packet* packet) {
    darray_destroy(packet->geometries);
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_world_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index) {
    render_view_world_internal_data* data = self->internal_data;
    u32 shader_id = data->s->id;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = &self->passes[p];
        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_world_on_render pass index %u failed to start.", p);
            return false;
        }

        if (!shader_system_use_by_id(shader_id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        // TODO: Find a generic way to request data such as ambient colour (which should be from a scene),
        // and mode (from the renderer)
        if (!material_system_apply_global(shader_id, frame_number, &packet->projection_matrix, &packet->view_matrix, &packet->ambient_colour, &packet->view_position, data->render_mode)) {
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
            b8 needs_update = m->render_frame_number != frame_number;
            if (!material_system_apply_instance(m, needs_update)) {
                KWARN("Failed to apply material '%s'. Skipping draw.", m->name);
                continue;
            } else {
                // Sync the frame number.
                m->render_frame_number = frame_number;
            }

            // Apply the locals
            material_system_apply_local(m, &packet->geometries[i].model);

            // Draw it.
            renderer_draw_geometry(&packet->geometries[i]);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_world_on_render pass index %u failed to end.", p);
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
