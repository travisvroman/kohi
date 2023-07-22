
#include "render_view_editor_world.h"

#include "containers/darray.h"
#include "core/event.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "defines.h"
#include "editor/editor_gizmo.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "memory/linear_allocator.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.inl"
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

typedef struct render_view_editor_world_internal_data {
    shader* s;
    f32 fov;
    f32 near_clip;
    f32 far_clip;
    mat4 projection_matrix;
    camera* world_camera;

    debug_colour_shader_locations debug_locations;

    // TODO: This should NOT be owned by the view, but by an editor viewport.
    // This is just here for now until the editor is built.
    editor_gizmo gizmo;

} render_view_editor_world_internal_data;

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }
    render_view_editor_world_internal_data* data = (render_view_editor_world_internal_data*)self->internal_data;
    if (!data) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            render_view_system_render_targets_regenerate(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}

b8 render_view_editor_world_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_editor_world_internal_data), MEMORY_TAG_RENDERER);
        render_view_editor_world_internal_data* data = self->internal_data;

        // Load debug colour3d shader.
        // Get colour3d shader uniform locations.
        {
            const char* colour3d_shader_name = "Shader.Builtin.ColourShader3D";
            data->s = shader_system_get(colour3d_shader_name);
            if (!data->s) {
                KERROR("Unable to get colour3d shader!");
                return false;
            }
            data->debug_locations.projection = shader_system_uniform_index(data->s, "projection");
            data->debug_locations.view = shader_system_uniform_index(data->s, "view");
            data->debug_locations.model = shader_system_uniform_index(data->s, "model");
        }

        // Get either the custom shader override or the defined default.
        // TODO: Set from configuration.
        data->near_clip = 0.1f;
        data->far_clip = 4000.0f;
        data->fov = deg_to_rad(45.0f);

        // Default
        data->projection_matrix = mat4_perspective(data->fov, 1280 / 720.0f, data->near_clip, data->far_clip);

        data->world_camera = camera_system_get_default();

        // Load up the gizmo. TODO: This should be moved to the editor.
        if (!editor_gizmo_create(&data->gizmo)) {
            KERROR("Failed to create editor gizmo!");
            return false;
        }
        if (editor_gizmo_initialize(&data->gizmo)) {
            KERROR("Failed to initialize editor gizmo!");
            return false;
        }
        if (editor_gizmo_load(&data->gizmo)) {
            KERROR("Failed to load editor gizmo!");
            return false;
        }

        if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
            KERROR("Unable to listen for refresh required event, creation failed.");
            return false;
        }
        return true;
    }

    KERROR("render_view_editor_world_on_create - Requires a valid pointer to a view.");
    return false;
}

void render_view_editor_world_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        // Unregister from the event.
        event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

        kfree(self->internal_data, sizeof(render_view_editor_world_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}

void render_view_editor_world_on_resize(struct render_view* self, u32 width, u32 height) {
    // Check if different. If so, regenerate projection matrix.
    if (width != self->width || height != self->height) {
        render_view_editor_world_internal_data* data = self->internal_data;

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

b8 render_view_editor_world_on_packet_build(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_editor_world_on_build_packet requires valid pointer to view, packet, and data.");
        return false;
    }

    // TODO: generally we would generate a packet here, but since this owns the gizmo, skip this for now.

    return true;
}

void render_view_editor_world_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet) {
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_editor_world_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index, const struct frame_data* p_frame_data) {
    render_view_editor_world_internal_data* data = self->internal_data;
    // u32 shader_id = data->s->id;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = &self->passes[p];
        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_editor_world_on_render pass index %u failed to start.", p);
            return false;
        }

        shader* s = shader_system_get("Shader.Builtin.ColourShader3D");
        if (!s) {
            KERROR("Unable to obtain colour3d shader.");
            return false;
        }
        shader_system_use_by_id(s->id);

        // Globals
        shader_system_uniform_set_by_index(data->debug_locations.projection, &packet->projection_matrix);
        shader_system_uniform_set_by_index(data->debug_locations.view, &packet->view_matrix);

        shader_system_apply_global();

        // TODO: Just drawing the gizmo here manually for now.
        geometry_render_data grd = {0};
        grd.model = transform_world_get(&data->gizmo.xform);
        grd.geometry = &data->gizmo.mode_data[data->gizmo.mode].geo;
        grd.unique_id = INVALID_ID;

        shader_system_uniform_set_by_index(data->debug_locations.model, &grd.model);
        renderer_geometry_draw(&grd);

        // Each geometry.
        // for (u32 i = 0; i < debug_geometry_count; ++i) {
        // NOTE: No instance-level uniforms to be set.

        // Local
        // shader_system_uniform_set_by_index(data->debug_locations.model, &packet->debug_geometries[i].model);

        // Draw it.
        // renderer_geometry_draw(&packet->debug_geometries[i]);
        // }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_editor_world_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    return true;
}
