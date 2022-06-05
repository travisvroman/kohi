#include "renderer_frontend.h"

#include "renderer_backend.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "math/kmath.h"

#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "systems/texture_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"

// TODO: temporary
#include "core/kstring.h"
#include "core/event.h"

// TODO: end temporary

typedef struct renderer_system_state {
    renderer_backend backend;
    camera* active_world_camera;
    mat4 projection;
    vec4 ambient_colour;
    mat4 ui_projection;
    mat4 ui_view;
    f32 near_clip;
    f32 far_clip;
    u32 material_shader_id;
    u32 ui_shader_id;
    u32 render_mode;
    // The number of render targets. Typically lines up with the amount of swapchain images.
    u8 window_render_target_count;
    // The current window framebuffer width.
    u32 framebuffer_width;
    // The current window framebuffer height.
    u32 framebuffer_height;

    // A pointer to the world renderpass. TODO: Configurable via views.
    renderpass* world_renderpass;
    // A pointer to the UI renderpass. TODO: Configurable via views.
    renderpass* ui_renderpass;
    // Indicates if the window is currently being resized.
    b8 resizing;
    // The current number of frames since the last resize operation.'
    // Only set if resizing = true. Otherwise 0.
    u8 frames_since_resize;
} renderer_system_state;

static renderer_system_state* state_ptr;

void regenerate_render_targets();

b8 renderer_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    switch (code) {
        case EVENT_CODE_SET_RENDER_MODE: {
            renderer_system_state* state = (renderer_system_state*)listener_inst;
            i32 mode = context.data.i32[0];
            switch (mode) {
                default:
                case RENDERER_VIEW_MODE_DEFAULT:
                    KDEBUG("Renderer mode set to default.");
                    state->render_mode = RENDERER_VIEW_MODE_DEFAULT;
                    break;
                case RENDERER_VIEW_MODE_LIGHTING:
                    KDEBUG("Renderer mode set to lighting.");
                    state->render_mode = RENDERER_VIEW_MODE_LIGHTING;
                    break;
                case RENDERER_VIEW_MODE_NORMALS:
                    KDEBUG("Renderer mode set to normals.");
                    state->render_mode = RENDERER_VIEW_MODE_NORMALS;
                    break;
            }
            return true;
        }
    }

    return false;
}

#define CRITICAL_INIT(op, msg) \
    if (!op) {                 \
        KERROR(msg);           \
        return false;          \
    }

b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name) {
    *memory_requirement = sizeof(renderer_system_state);
    if (state == 0) {
        return true;
    }
    state_ptr = state;

    // Default framebuffer size. Overridden when window is created.
    state_ptr->framebuffer_width = 1280;
    state_ptr->framebuffer_height = 720;
    state_ptr->resizing = false;
    state_ptr->frames_since_resize = 0;

    // TODO: make this configurable.
    renderer_backend_create(RENDERER_BACKEND_TYPE_VULKAN, &state_ptr->backend);
    state_ptr->backend.frame_number = 0;
    state_ptr->render_mode = RENDERER_VIEW_MODE_DEFAULT;

    event_register(EVENT_CODE_SET_RENDER_MODE, state, renderer_on_event);

    renderer_backend_config renderer_config = {};
    renderer_config.application_name = application_name;
    renderer_config.on_rendertarget_refresh_required = regenerate_render_targets;

    // Renderpasses. TODO: read config from file.
    renderer_config.renderpass_count = 2;
    const char* world_renderpass_name = "Renderpass.Builtin.World";
    const char* ui_renderpass_name = "Renderpass.Builtin.UI";
    renderpass_config pass_configs[2];
    pass_configs[0].name = world_renderpass_name;
    pass_configs[0].prev_name = 0;
    pass_configs[0].next_name = ui_renderpass_name;
    pass_configs[0].render_area = (vec4){0, 0, 1280, 720};
    pass_configs[0].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    pass_configs[0].clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG | RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;

    pass_configs[1].name = ui_renderpass_name;
    pass_configs[1].prev_name = world_renderpass_name;
    pass_configs[1].next_name = 0;
    pass_configs[1].render_area = (vec4){0, 0, 1280, 720};
    pass_configs[1].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    pass_configs[1].clear_flags = RENDERPASS_CLEAR_NONE_FLAG;

    renderer_config.pass_configs = pass_configs;

    // Initialize the backend.
    CRITICAL_INIT(state_ptr->backend.initialize(&state_ptr->backend, &renderer_config, &state_ptr->window_render_target_count), "Renderer backend failed to initialize. Shutting down.");

    // TODO: Will know how to get these when we define views.
    state_ptr->world_renderpass = state_ptr->backend.renderpass_get(world_renderpass_name);
    state_ptr->world_renderpass->render_target_count = state_ptr->window_render_target_count;
    state_ptr->world_renderpass->targets = kallocate(sizeof(render_target) * state_ptr->window_render_target_count, MEMORY_TAG_ARRAY);

    state_ptr->ui_renderpass = state_ptr->backend.renderpass_get(ui_renderpass_name);
    state_ptr->ui_renderpass->render_target_count = state_ptr->window_render_target_count;
    state_ptr->ui_renderpass->targets = kallocate(sizeof(render_target) * state_ptr->window_render_target_count, MEMORY_TAG_ARRAY);

    regenerate_render_targets();

    // Update the main/world renderpass dimensions.
    state_ptr->world_renderpass->render_area.x = 0;
    state_ptr->world_renderpass->render_area.y = 0;
    state_ptr->world_renderpass->render_area.z = state_ptr->framebuffer_width;
    state_ptr->world_renderpass->render_area.w = state_ptr->framebuffer_height;

    // Also update the UI renderpass dimensions.
    state_ptr->ui_renderpass->render_area.x = 0;
    state_ptr->ui_renderpass->render_area.y = 0;
    state_ptr->ui_renderpass->render_area.z = state_ptr->framebuffer_width;
    state_ptr->ui_renderpass->render_area.w = state_ptr->framebuffer_height;

    // Shaders
    resource config_resource;
    shader_config* config = 0;

    // Builtin material shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_MATERIAL, RESOURCE_TYPE_SHADER, &config_resource),
        "Failed to load builtin material shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin material shader.");
    resource_system_unload(&config_resource);
    state_ptr->material_shader_id = shader_system_get_id(BUILTIN_SHADER_NAME_MATERIAL);

    // Builtin UI shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_UI, RESOURCE_TYPE_SHADER, &config_resource),
        "Failed to load builtin UI shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin UI shader.");
    resource_system_unload(&config_resource);
    state_ptr->ui_shader_id = shader_system_get_id(BUILTIN_SHADER_NAME_UI);

    // World projection/view
    state_ptr->near_clip = 0.1f;
    state_ptr->far_clip = 1000.0f;
    state_ptr->projection = mat4_perspective(deg_to_rad(45.0f), 1280 / 720.0f, state_ptr->near_clip, state_ptr->far_clip);
    // TODO: Obtain from scene
    state_ptr->ambient_colour = (vec4){0.25f, 0.25f, 0.25f, 1.0f};

    // UI projection/view
    state_ptr->ui_projection = mat4_orthographic(0, 1280.0f, 720.0f, 0, -100.f, 100.0f);  // Intentionally flipped on y axis.
    state_ptr->ui_view = mat4_inverse(mat4_identity());

    return true;
}

void renderer_system_shutdown(void* state) {
    if (state_ptr) {
        // Destroy render targets.
        for (u8 i = 0; i < state_ptr->window_render_target_count; ++i) {
            state_ptr->backend.render_target_destroy(&state_ptr->world_renderpass->targets[i], true);
            state_ptr->backend.render_target_destroy(&state_ptr->ui_renderpass->targets[i], true);
        }

        state_ptr->backend.shutdown(&state_ptr->backend);
    }
    state_ptr = 0;
}

void renderer_on_resized(u16 width, u16 height) {
    if (state_ptr) {
        // Flag as resizing and store the change, but wait to regenerate.
        state_ptr->resizing = true;
        state_ptr->framebuffer_width = width;
        state_ptr->framebuffer_height = height;
        // Also reset the frame count since the last  resize operation.
        state_ptr->frames_since_resize = 0;
    } else {
        KWARN("renderer backend does not exist to accept resize: %i %i", width, height);
    }
}

b8 renderer_draw_frame(render_packet* packet) {
    state_ptr->backend.frame_number++;

    // Make sure the window is not currently being resized by waiting a designated
    // number of frames after the last resize operation before performing the backend updates.
    if (state_ptr->resizing) {
        state_ptr->frames_since_resize++;

        // If the required number of frames have passed since the resize, go ahead and perform the actual updates.
        if (state_ptr->frames_since_resize >= 30) {
            f32 width = state_ptr->framebuffer_width;
            f32 height = state_ptr->framebuffer_height;
            state_ptr->projection = mat4_perspective(deg_to_rad(45.0f), width / (f32)height, state_ptr->near_clip, state_ptr->far_clip);
            state_ptr->ui_projection = mat4_orthographic(0, (f32)width, (f32)height, 0, -100.f, 100.0f);  // Intentionally flipped on y axis.
            state_ptr->backend.resized(&state_ptr->backend, width, height);

            state_ptr->frames_since_resize = 0;
            state_ptr->resizing = false;
        } else {
            // Skip rendering the frame and try again next time.
            return true;
        }
    }

    // TODO: views
    // Update the main/world renderpass dimensions.
    state_ptr->world_renderpass->render_area.z = state_ptr->framebuffer_width;
    state_ptr->world_renderpass->render_area.w = state_ptr->framebuffer_height;

    // Also update the UI renderpass dimensions.
    state_ptr->ui_renderpass->render_area.z = state_ptr->framebuffer_width;
    state_ptr->ui_renderpass->render_area.w = state_ptr->framebuffer_height;

    if (!state_ptr->active_world_camera) {
        // Just grab the default camera.
        state_ptr->active_world_camera = camera_system_get_default();
    }

    mat4 view = camera_view_get(state_ptr->active_world_camera);

    // If the begin frame returned successfully, mid-frame operations may continue.
    if (state_ptr->backend.begin_frame(&state_ptr->backend, packet->delta_time)) {
        u8 attachment_index = state_ptr->backend.window_attachment_index_get();

        // World renderpass
        if (!state_ptr->backend.begin_renderpass(&state_ptr->backend, state_ptr->world_renderpass, &state_ptr->world_renderpass->targets[attachment_index])) {
            KERROR("backend.begin_renderpass -> BUILTIN_RENDERPASS_WORLD failed. Application shutting down...");
            return false;
        }

        if (!shader_system_use_by_id(state_ptr->material_shader_id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!material_system_apply_global(state_ptr->material_shader_id, &state_ptr->projection, &view, &state_ptr->ambient_colour, &state_ptr->active_world_camera->position, state_ptr->render_mode)) {
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

            // Apply the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times.
            b8 needs_update = m->render_frame_number != state_ptr->backend.frame_number;
            if (!material_system_apply_instance(m, needs_update)) {
                KWARN("Failed to apply material '%s'. Skipping draw.", m->name);
                continue;
            } else {
                // Sync the frame number.
                m->render_frame_number = state_ptr->backend.frame_number;
            }

            // Apply the locals
            material_system_apply_local(m, &packet->geometries[i].model);

            // Draw it.
            state_ptr->backend.draw_geometry(packet->geometries[i]);
        }

        if (!state_ptr->backend.end_renderpass(&state_ptr->backend, state_ptr->world_renderpass)) {
            KERROR("backend.end_renderpass -> BUILTIN_RENDERPASS_WORLD failed. Application shutting down...");
            return false;
        }
        // End world renderpass

        // UI renderpass
        if (!state_ptr->backend.begin_renderpass(&state_ptr->backend, state_ptr->ui_renderpass, &state_ptr->ui_renderpass->targets[attachment_index])) {
            KERROR("backend.begin_renderpass -> BUILTIN_RENDERPASS_UI failed. Application shutting down...");
            return false;
        }

        // Update UI global state
        if (!shader_system_use_by_id(state_ptr->ui_shader_id)) {
            KERROR("Failed to use UI shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!material_system_apply_global(state_ptr->ui_shader_id, &state_ptr->ui_projection, &state_ptr->ui_view, 0, 0, 0)) {
            KERROR("Failed to use apply globals for UI shader. Render frame failed.");
            return false;
        }

        // Draw ui geometries.
        count = packet->ui_geometry_count;
        for (u32 i = 0; i < count; ++i) {
            material* m = 0;
            if (packet->ui_geometries[i].geometry->material) {
                m = packet->ui_geometries[i].geometry->material;
            } else {
                m = material_system_get_default();
            }
            // Apply the material
            b8 needs_update = m->render_frame_number != state_ptr->backend.frame_number;
            if (!material_system_apply_instance(m, needs_update)) {
                KWARN("Failed to apply UI material '%s'. Skipping draw.", m->name);
                continue;
            } else {
                // Sync the frame number.
                m->render_frame_number = state_ptr->backend.frame_number;
            }

            // Apply the locals
            material_system_apply_local(m, &packet->ui_geometries[i].model);

            // Draw it.
            state_ptr->backend.draw_geometry(packet->ui_geometries[i]);
        }

        if (!state_ptr->backend.end_renderpass(&state_ptr->backend, state_ptr->ui_renderpass)) {
            KERROR("backend.end_renderpass -> BUILTIN_RENDERPASS_UI failed. Application shutting down...");
            return false;
        }
        // End UI renderpass

        // End the frame. If this fails, it is likely unrecoverable.
        b8 result = state_ptr->backend.end_frame(&state_ptr->backend, packet->delta_time);

        if (!result) {
            KERROR("renderer_end_frame failed. Application shutting down...");
            return false;
        }
    }

    return true;
}

void renderer_texture_create(const u8* pixels, struct texture* texture) {
    state_ptr->backend.texture_create(pixels, texture);
}

void renderer_texture_destroy(struct texture* texture) {
    state_ptr->backend.texture_destroy(texture);
}

void renderer_texture_create_writeable(texture* t) {
    state_ptr->backend.texture_create_writeable(t);
}

void renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels) {
    state_ptr->backend.texture_write_data(t, offset, size, pixels);
}

void renderer_texture_resize(texture* t, u32 new_width, u32 new_height) {
    state_ptr->backend.texture_resize(t, new_width, new_height);
}

b8 renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices) {
    return state_ptr->backend.create_geometry(geometry, vertex_size, vertex_count, vertices, index_size, index_count, indices);
}

void renderer_destroy_geometry(geometry* geometry) {
    state_ptr->backend.destroy_geometry(geometry);
}

renderpass* renderer_renderpass_get(const char* name) {
    return state_ptr->backend.renderpass_get(name);
}

b8 renderer_shader_create(shader* s, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages) {
    return state_ptr->backend.shader_create(s, pass, stage_count, stage_filenames, stages);
}

void renderer_shader_destroy(shader* s) {
    state_ptr->backend.shader_destroy(s);
}

b8 renderer_shader_initialize(shader* s) {
    return state_ptr->backend.shader_initialize(s);
}

b8 renderer_shader_use(shader* s) {
    return state_ptr->backend.shader_use(s);
}

b8 renderer_shader_bind_globals(shader* s) {
    return state_ptr->backend.shader_bind_globals(s);
}

b8 renderer_shader_bind_instance(shader* s, u32 instance_id) {
    return state_ptr->backend.shader_bind_instance(s, instance_id);
}

b8 renderer_shader_apply_globals(shader* s) {
    return state_ptr->backend.shader_apply_globals(s);
}

b8 renderer_shader_apply_instance(shader* s, b8 needs_update) {
    return state_ptr->backend.shader_apply_instance(s, needs_update);
}

b8 renderer_shader_acquire_instance_resources(shader* s, texture_map** maps, u32* out_instance_id) {
    return state_ptr->backend.shader_acquire_instance_resources(s, maps, out_instance_id);
}

b8 renderer_shader_release_instance_resources(shader* s, u32 instance_id) {
    return state_ptr->backend.shader_release_instance_resources(s, instance_id);
}

b8 renderer_set_uniform(shader* s, shader_uniform* uniform, const void* value) {
    return state_ptr->backend.shader_set_uniform(s, uniform, value);
}

b8 renderer_texture_map_acquire_resources(struct texture_map* map) {
    return state_ptr->backend.texture_map_acquire_resources(map);
}

void renderer_texture_map_release_resources(struct texture_map* map) {
    state_ptr->backend.texture_map_release_resources(map);
}

void renderer_render_target_create(u8 attachment_count, texture** attachments, renderpass* pass, u32 width, u32 height, render_target* out_target) {
    state_ptr->backend.render_target_create(attachment_count, attachments, pass, width, height, out_target);
}

void renderer_render_target_destroy(render_target* target, b8 free_internal_memory) {
    state_ptr->backend.render_target_destroy(target, free_internal_memory);
}

void renderer_renderpass_create(renderpass* out_renderpass, f32 depth, u32 stencil, b8 has_prev_pass, b8 has_next_pass) {
    state_ptr->backend.renderpass_create(out_renderpass, depth, stencil, has_prev_pass, has_next_pass);
}

void renderer_renderpass_destroy(renderpass* pass) {
    state_ptr->backend.renderpass_destroy(pass);
}

void regenerate_render_targets() {
    // Create render targets for each. TODO: Should be configurable.
    for (u8 i = 0; i < state_ptr->window_render_target_count; ++i) {
        // Destroy the old first if they exist.
        state_ptr->backend.render_target_destroy(&state_ptr->world_renderpass->targets[i], false);
        state_ptr->backend.render_target_destroy(&state_ptr->ui_renderpass->targets[i], false);

        texture* window_target_texture = state_ptr->backend.window_attachment_get(i);
        texture* depth_target_texture = state_ptr->backend.depth_attachment_get();

        // World render targets.
        texture* attachments[2] = {window_target_texture, depth_target_texture};
        state_ptr->backend.render_target_create(
            2,
            attachments,
            state_ptr->world_renderpass,
            state_ptr->framebuffer_width,
            state_ptr->framebuffer_height,
            &state_ptr->world_renderpass->targets[i]);

        // UI render targets
        texture* ui_attachments[1] = {window_target_texture};
        state_ptr->backend.render_target_create(
            1,
            ui_attachments,
            state_ptr->ui_renderpass,
            state_ptr->framebuffer_width,
            state_ptr->framebuffer_height,
            &state_ptr->ui_renderpass->targets[i]);
    }
}
