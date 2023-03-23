#include "render_view_skybox.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "resources/skybox.h"
#include "containers/darray.h"
#include "systems/resource_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"
#include "systems/render_view_system.h"
#include "renderer/renderer_frontend.h"
#include "memory/linear_allocator.h"

typedef struct render_view_skybox_internal_data {
    shader* s;
    f32 fov;
    f32 near_clip;
    f32 far_clip;
    mat4 projection_matrix;
    camera* world_camera;
    // uniform locations
    u16 projection_location;
    u16 view_location;
    u16 cube_map_location;
} render_view_skybox_internal_data;

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            render_view_system_regenerate_render_targets(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    return false;
}

b8 render_view_skybox_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_skybox_internal_data), MEMORY_TAG_RENDERER);
        render_view_skybox_internal_data* data = self->internal_data;

        // Builtin skybox shader.
        const char* shader_name = "Shader.Builtin.Skybox";
        resource config_resource;
        if (!resource_system_load(shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin skybox shader.");
            return false;
        }
        shader_config* config = (shader_config*)config_resource.data;
        // NOTE: Assuming the first pass since that's all this view has.
        if (!shader_system_create(&self->passes[0], config)) {
            KERROR("Failed to load builtin skybox shader.");
            return false;
        }

        resource_system_unload(&config_resource);
        // Get a pointer to the shader.
        data->s = shader_system_get(self->custom_shader_name ? self->custom_shader_name : shader_name);
        data->projection_location = shader_system_uniform_index(data->s, "projection");
        data->view_location = shader_system_uniform_index(data->s, "view");
        data->cube_map_location = shader_system_uniform_index(data->s, "cube_texture");

        // TODO: Set from configuration.
        data->near_clip = 0.1f;
        data->far_clip = 1000.0f;
        data->fov = deg_to_rad(45.0f);

        // Default
        data->projection_matrix = mat4_perspective(data->fov, 1280 / 720.0f, data->near_clip, data->far_clip);
        data->world_camera = camera_system_get_default();

        if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
            KERROR("Unable to listen for refresh required event, creation failed.");
            return false;
        }
        return true;
    }
    KERROR("render_view_skybox_on_create - Requires a valid pointer to a view.");
    return false;
}
void render_view_skybox_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        // Unregister from the event.
        event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

        // NOTE: shader is automatically destroyed by that system on shutdown.

        kfree(self->internal_data, sizeof(render_view_skybox_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}
void render_view_skybox_on_resize(struct render_view* self, u32 width, u32 height) {
    // Check if different. If so, regenerate projection matrix.
    if (width != self->width || height != self->height) {
        render_view_skybox_internal_data* data = self->internal_data;

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

b8 render_view_skybox_on_build_packet(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_skybox_on_build_packet requires valid pointer to view, packet, and data.");
        return false;
    }

    skybox_packet_data* skybox_data = (skybox_packet_data*)data;
    render_view_skybox_internal_data* internal_data = (render_view_skybox_internal_data*)self->internal_data;

    out_packet->view = self;

    // Set matrices, etc.
    out_packet->projection_matrix = internal_data->projection_matrix;
    out_packet->view_matrix = camera_view_get(internal_data->world_camera);
    out_packet->view_position = camera_position_get(internal_data->world_camera);

    // Just set the extended data to the skybox data
    // out_packet->extended_data = skybox_data;
    out_packet->extended_data = linear_allocator_allocate(frame_allocator, sizeof(skybox_packet_data));
    kcopy_memory(out_packet->extended_data, skybox_data, sizeof(skybox_packet_data));

    return true;
}

void render_view_skybox_on_destroy_packet(const struct render_view* self, struct render_view_packet* packet) {
    // Not much to do here, just zero out.
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_skybox_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index) {
    render_view_skybox_internal_data* data = self->internal_data;
    u32 shader_id = data->s->id;

    skybox_packet_data* skybox_data = (skybox_packet_data*)packet->extended_data;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = &self->passes[p];
        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_skybox_on_render pass index %u failed to start.", p);
            return false;
        }

        if (skybox_data && skybox_data->sb) {
            if (!shader_system_use_by_id(shader_id)) {
                KERROR("Failed to use skybox shader. Render frame failed.");
                return false;
            }

            // Get the view matrix, but zero out the position so the skybox stays put on screen.
            mat4 view_matrix = camera_view_get(data->world_camera);
            view_matrix.data[12] = 0.0f;
            view_matrix.data[13] = 0.0f;
            view_matrix.data[14] = 0.0f;

            // Apply globals
            // TODO: This is terrible. Need to bind by id.
            renderer_shader_bind_globals(shader_system_get_by_id(shader_id));
            if (!shader_system_uniform_set_by_index(data->projection_location, &packet->projection_matrix)) {
                KERROR("Failed to apply skybox projection uniform.");
                return false;
            }
            if (!shader_system_uniform_set_by_index(data->view_location, &view_matrix)) {
                KERROR("Failed to apply skybox view uniform.");
                return false;
            }
            shader_system_apply_global();

            // Instance
            shader_system_bind_instance(skybox_data->sb->instance_id);
            if (!shader_system_uniform_set_by_index(data->cube_map_location, &skybox_data->sb->cubemap)) {
                KERROR("Failed to apply skybox cube map uniform.");
                return false;
            }
            b8 needs_update = skybox_data->sb->render_frame_number != frame_number;
            shader_system_apply_instance(needs_update);

            // Sync the frame number.
            skybox_data->sb->render_frame_number = frame_number;

            // Draw it.
            geometry_render_data render_data = {};
            render_data.geometry = skybox_data->sb->g;
            renderer_draw_geometry(&render_data);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_skybox_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    return true;
}
