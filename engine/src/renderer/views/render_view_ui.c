#include "render_view_ui.h"

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
#include "renderer/renderer_frontend.h"
#include "resources/ui_text.h"

typedef struct render_view_ui_internal_data {
    shader* s;
    f32 near_clip;
    f32 far_clip;
    mat4 projection_matrix;
    mat4 view_matrix;
    u16 diffuse_map_location;
    u16 diffuse_colour_location;
    u16 model_location;
    // u32 render_mode;
} render_view_ui_internal_data;

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            render_view_system_render_targets_regenerate(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    return false;
}


b8 render_view_ui_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_ui_internal_data), MEMORY_TAG_RENDERER);
        render_view_ui_internal_data* data = self->internal_data;

        const char* shader_name = "Shader.Builtin.UI";
        resource config_resource;
        if (!resource_system_load(shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin UI shader.");
            return false;
        }
        shader_config* config = (shader_config*)config_resource.data;
        // NOTE: Assuming the first pass since that's all this view has.
        if (!shader_system_create(&self->passes[0], config)) {
            KERROR("Failed to load builtin UI shader.");
            return false;
        }
        resource_system_unload(&config_resource);

        // Get either the custom shader override or the defined default.
        data->s = shader_system_get(self->custom_shader_name ? self->custom_shader_name : shader_name);
        data->diffuse_map_location = shader_system_uniform_index(data->s, "diffuse_texture");
        data->diffuse_colour_location = shader_system_uniform_index(data->s, "diffuse_colour");
        data->model_location = shader_system_uniform_index(data->s, "model");
        // TODO: Set from configuration.
        data->near_clip = -100.0f;
        data->far_clip = 100.0f;

        // Default
        data->projection_matrix = mat4_orthographic(0.0f, 1280.0f, 720.0f, 0.0f, data->near_clip, data->far_clip);
        data->view_matrix = mat4_identity();

        if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
            KERROR("Unable to listen for refresh required event, creation failed.");
            return false;
        }

        return true;
    }
    KERROR("render_view_ui_on_create - Requires a valid pointer to a view.");
    return false;
}

void render_view_ui_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        // Unregister from the event.
        event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

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
            self->passes[i].render_area.x = 0;
            self->passes[i].render_area.y = 0;
            self->passes[i].render_area.z = width;
            self->passes[i].render_area.w = height;
        }
    }
}

b8 render_view_ui_on_packet_build(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_ui_on_packet_build requires valid pointer to view, packet, and data.");
        return false;
    }

    ui_packet_data* packet_data = (ui_packet_data*)data;
    render_view_ui_internal_data* internal_data = (render_view_ui_internal_data*)self->internal_data;

    out_packet->geometries = darray_create(geometry_render_data);
    out_packet->view = self;

    // Set matrices, etc.
    out_packet->projection_matrix = internal_data->projection_matrix;
    out_packet->view_matrix = internal_data->view_matrix;

    // TODO: temp set extended data to the test text objects for now.
    out_packet->extended_data = linear_allocator_allocate(frame_allocator, sizeof(ui_packet_data));
    kcopy_memory(out_packet->extended_data, packet_data, sizeof(ui_packet_data));

    // Obtain all geometries from the current scene.
    // Iterate all meshes and add them to the packet's geometries collection
    for (u32 i = 0; i < packet_data->mesh_data.mesh_count; ++i) {
        mesh* m = packet_data->mesh_data.meshes[i];
        for (u32 j = 0; j < m->geometry_count; ++j) {
            geometry_render_data render_data;
            render_data.geometry = m->geometries[j];
            render_data.model = transform_world_get(&m->transform);
            darray_push(out_packet->geometries, render_data);
            out_packet->geometry_count++;
        }
    }

    return true;
}

void render_view_ui_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet) {
    darray_destroy(packet->geometries);
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_ui_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index, const struct frame_data* p_frame_data) {
    render_view_ui_internal_data* data = self->internal_data;
    u32 shader_id = data->s->id;

    for (u32 p = 0; p < self->renderpass_count; ++p) {
        renderpass* pass = &self->passes[p];
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
            renderer_geometry_draw(&packet->geometries[i]);
        }

        // Draw bitmap text
        ui_packet_data* packet_data = (ui_packet_data*)packet->extended_data;  // array of texts
        for (u32 i = 0; i < packet_data->text_count; ++i) {
            ui_text* text = packet_data->texts[i];
            shader_system_bind_instance(text->instance_id);

            if (!shader_system_uniform_set_by_index(data->diffuse_map_location, &text->data->atlas)) {
                KERROR("Failed to apply bitmap font diffuse map uniform.");
                return false;
            }

            // TODO: font colour.
            static vec4 white_colour = (vec4){1.0f, 1.0f, 1.0f, 1.0f};  // white
            if (!shader_system_uniform_set_by_index(data->diffuse_colour_location, &white_colour)) {
                KERROR("Failed to apply bitmap font diffuse colour uniform.");
                return false;
            }
            b8 needs_update = text->render_frame_number != frame_number;
            shader_system_apply_instance(needs_update);

            // Sync the frame number.
            text->render_frame_number = frame_number;

            // Apply the locals
            mat4 model = transform_world_get(&text->transform);
            if(!shader_system_uniform_set_by_index(data->model_location, &model)) {
                KERROR("Failed to apply model matrix for text");
            }

            ui_text_draw(text);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_ui_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    return true;
}
