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

// TODO: temporary
#include "core/kstring.h"
#include "core/event.h"

// TODO: end temporary

typedef struct renderer_system_state {
    renderer_backend backend;
    mat4 projection;
    mat4 view;
    vec4 ambient_colour;
    vec3 view_position;
    mat4 ui_projection;
    mat4 ui_view;
    f32 near_clip;
    f32 far_clip;
    u32 material_shader_id;
    u32 ui_shader_id;
    u32 render_mode;
} renderer_system_state;

static renderer_system_state* state_ptr;

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

    // TODO: make this configurable.
    renderer_backend_create(RENDERER_BACKEND_TYPE_VULKAN, &state_ptr->backend);
    state_ptr->backend.frame_number = 0;
    state_ptr->render_mode = RENDERER_VIEW_MODE_DEFAULT;

    event_register(EVENT_CODE_SET_RENDER_MODE, state, renderer_on_event);

    // Initialize the backend.
    CRITICAL_INIT(state_ptr->backend.initialize(&state_ptr->backend, application_name), "Renderer backend failed to initialize. Shutting down.");

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
    // TODO: configurable camera starting position.
    state_ptr->view = mat4_translation((vec3){0, 0, -30.0f});
    state_ptr->view = mat4_inverse(state_ptr->view);
    // TODO: Obtain from scene
    state_ptr->ambient_colour = (vec4){0.25f, 0.25f, 0.25f, 1.0f};

    // UI projection/view
    state_ptr->ui_projection = mat4_orthographic(0, 1280.0f, 720.0f, 0, -100.f, 100.0f);  // Intentionally flipped on y axis.
    state_ptr->ui_view = mat4_inverse(mat4_identity());

    return true;
}

void renderer_system_shutdown(void* state) {
    if (state_ptr) {
        state_ptr->backend.shutdown(&state_ptr->backend);
    }
    state_ptr = 0;
}

void renderer_on_resized(u16 width, u16 height) {
    if (state_ptr) {
        state_ptr->projection = mat4_perspective(deg_to_rad(45.0f), width / (f32)height, state_ptr->near_clip, state_ptr->far_clip);
        state_ptr->ui_projection = mat4_orthographic(0, (f32)width, (f32)height, 0, -100.f, 100.0f);  // Intentionally flipped on y axis.
        state_ptr->backend.resized(&state_ptr->backend, width, height);
    } else {
        KWARN("renderer backend does not exist to accept resize: %i %i", width, height);
    }
}

b8 renderer_draw_frame(render_packet* packet) {
    state_ptr->backend.frame_number++;

    // If the begin frame returned successfully, mid-frame operations may continue.
    if (state_ptr->backend.begin_frame(&state_ptr->backend, packet->delta_time)) {
        // World renderpass
        if (!state_ptr->backend.begin_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_WORLD)) {
            KERROR("backend.begin_renderpass -> BUILTIN_RENDERPASS_WORLD failed. Application shutting down...");
            return false;
        }

        if (!shader_system_use_by_id(state_ptr->material_shader_id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!material_system_apply_global(state_ptr->material_shader_id, &state_ptr->projection, &state_ptr->view, &state_ptr->ambient_colour, &state_ptr->view_position, state_ptr->render_mode)) {
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

        if (!state_ptr->backend.end_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_WORLD)) {
            KERROR("backend.end_renderpass -> BUILTIN_RENDERPASS_WORLD failed. Application shutting down...");
            return false;
        }
        // End world renderpass

        // UI renderpass
        if (!state_ptr->backend.begin_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_UI)) {
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

            // Update the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times. It still needs to be bound
            // either way, so this check result gets passed to the backend which either
            // updates the internal shader bindings and binds them, or only binds them.
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

        if (!state_ptr->backend.end_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_UI)) {
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

void renderer_set_view(mat4 view, vec3 view_position) {
    state_ptr->view = view;
    state_ptr->view_position = view_position;
}

void renderer_create_texture(const u8* pixels, struct texture* texture) {
    state_ptr->backend.create_texture(pixels, texture);
}

void renderer_destroy_texture(struct texture* texture) {
    state_ptr->backend.destroy_texture(texture);
}

b8 renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices) {
    return state_ptr->backend.create_geometry(geometry, vertex_size, vertex_count, vertices, index_size, index_count, indices);
}

void renderer_destroy_geometry(geometry* geometry) {
    state_ptr->backend.destroy_geometry(geometry);
}

b8 renderer_renderpass_id(const char* name, u8* out_renderpass_id) {
    // TODO: HACK: Need dynamic renderpasses instead of hardcoding them.
    if (strings_equali("Renderpass.Builtin.World", name)) {
        *out_renderpass_id = BUILTIN_RENDERPASS_WORLD;
        return true;
    } else if (strings_equali("Renderpass.Builtin.UI", name)) {
        *out_renderpass_id = BUILTIN_RENDERPASS_UI;
        return true;
    }

    KERROR("renderer_renderpass_id: No renderpass named '%s'.", name);
    *out_renderpass_id = INVALID_ID_U8;
    return false;
}

b8 renderer_shader_create(shader* s, u8 renderpass_id, u8 stage_count, const char** stage_filenames, shader_stage* stages) {
    return state_ptr->backend.shader_create(s, renderpass_id, stage_count, stage_filenames, stages);
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