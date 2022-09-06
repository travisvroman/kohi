#include "render_view_ui.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "containers/darray.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "renderer/renderer_frontend.h"

typedef struct render_view_ui_internal_data {
    u32 shader_id;
    f32 near_clip;
    f32 far_clip;
    mat4 projection_matrix;
    mat4 view_matrix;
    // u32 render_mode;
} render_view_ui_internal_data;

b8 render_view_ui_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_ui_internal_data), MEMORY_TAG_RENDERER);
        render_view_ui_internal_data* data = self->internal_data;

        // Get either the custom shader override or the defined default.
        data->shader_id = shader_system_get_id(self->custom_shader_name ? self->custom_shader_name : "Shader.Builtin.UI");
        // TODO: Set from configuration.
        data->near_clip = -100.0f;
        data->far_clip = 100.0f;

        // Default
        data->projection_matrix = mat4_orthographic(0.0f, 1280.0f, 720.0f, 0.0f, data->near_clip, data->far_clip);
        data->view_matrix = mat4_identity();

        return true;
    }
    KERROR("render_view_ui_on_create - Requires a valid pointer to a view.");
    return false;
}

void render_view_ui_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        kfree(self->internal_data, sizeof(render_view_ui_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}

void render_view_ui_on_resize(struct render_view* self, u32 width, u32 height) {
    // Check if different. If so, regenerate projection matrix.
    if (width != self->width || height != self->height) {
        render_view_ui_internal_data* data = self->internal_data;

        self->width = width;
        self->height = height;
        data->projection_matrix = mat4_orthographic(0.0f, (f32)self->width, (f32)self->height, 0.0f, data->near_clip, data->far_clip);

        for (u32 i = 0; i < self->renderpass_count; ++i) {
            self->passes[i]->render_area.x = 0;
            self->passes[i]->render_area.y = 0;
            self->passes[i]->render_area.z = width;
            self->passes[i]->render_area.w = height;
        }
    }
}

b8 render_view_ui_on_build_packet(const struct render_view* self, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_ui_on_build_packet requires valid pointer to view, packet, and data.");
        return false;
    }

    mesh_packet_data* mesh_data = (mesh_packet_data*)data;
    render_view_ui_internal_data* internal_data = (render_view_ui_internal_data*)self->internal_data;

    out_packet->geometries = darray_create(geometry_render_data);
    out_packet->view = self;

    // Set matrices, etc.
    out_packet->projection_matrix = internal_data->projection_matrix;
    out_packet->view_matrix = internal_data->view_matrix;

    // Obtain all geometries from the current scene.
    // Iterate all meshes and add them to the packet's geometries collection
    for (u32 i = 0; i < mesh_data->mesh_count; ++i) {
        mesh* m = mesh_data->meshes[i];
        for (u32 j = 0; j < m->geometry_count; ++j) {
            geometry_render_data render_data;
            render_data.geometry = m->geometries[j];
            render_data.model = transform_get_world(&m->transform);
            darray_push(out_packet->geometries, render_data);
            out_packet->geometry_count++;
        }
    }

    return true;
}

void render_view_ui_on_destroy_packet(const struct render_view* self, struct render_view_packet* packet) {
    darray_destroy(packet->geometries);
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_ui_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index) {
    render_view_ui_internal_data* data = self->internal_data;
    u32 shader_id = data->shader_id;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = self->passes[p];
        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_ui_on_render pass index %u failed to start.", p);
            return false;
        }

        if (!shader_system_use_by_id(shader_id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!material_system_apply_global(shader_id, frame_number, &packet->projection_matrix, &packet->view_matrix, 0, 0, 0)) {
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
            KERROR("render_view_ui_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    return true;
}
