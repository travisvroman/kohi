#include "render_view_skybox.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "containers/darray.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"
#include "renderer/renderer_frontend.h"

typedef struct render_view_skybox_internal_data {
    u32 shader_id;
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

b8 render_view_skybox_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_skybox_internal_data), MEMORY_TAG_RENDERER);
        render_view_skybox_internal_data* data = self->internal_data;

        // Get either the custom shader override or the defined default.
        shader* s = shader_system_get(self->custom_shader_name ? self->custom_shader_name : "Shader.Builtin.Skybox");
        data->shader_id = s->id;
        data->projection_location = shader_system_uniform_index(s, "projection");
        data->view_location = shader_system_uniform_index(s, "view");
        data->cube_map_location = shader_system_uniform_index(s, "cube_texture");

        // TODO: Set from configuration.
        data->near_clip = 0.1f;
        data->far_clip = 1000.0f;
        data->fov = deg_to_rad(45.0f);

        // Default
        data->projection_matrix = mat4_perspective(data->fov, 1280 / 720.0f, data->near_clip, data->far_clip);
        data->world_camera = camera_system_get_default();
        return true;
    }
    KERROR("render_view_skybox_on_create - Requires a valid pointer to a view.");
    return false;
}
void render_view_skybox_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
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
            self->passes[i]->render_area.x = 0;
            self->passes[i]->render_area.y = 0;
            self->passes[i]->render_area.z = width;
            self->passes[i]->render_area.w = height;
        }
    }
}

b8 render_view_skybox_on_build_packet(const struct render_view* self, void* data, struct render_view_packet* out_packet) {
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
    out_packet->extended_data = skybox_data;
    return true;
}

b8 render_view_skybox_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index) {
    render_view_skybox_internal_data* data = self->internal_data;
    u32 shader_id = data->shader_id;

    skybox_packet_data* skybox_data = (skybox_packet_data*)packet->extended_data;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = self->passes[p];
        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_skybox_on_render pass index %u failed to start.", p);
            return false;
        }

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

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_skybox_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    return true;
}
