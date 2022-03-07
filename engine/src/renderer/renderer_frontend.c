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
    mat4 ui_projection;
    mat4 ui_view;
    f32 near_clip;
    f32 far_clip;
} renderer_system_state;

static renderer_system_state* state_ptr;

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

    // Builtin UI shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_UI, RESOURCE_TYPE_SHADER, &config_resource),
        "Failed to load builtin UI shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin UI shader.");
    resource_system_unload(&config_resource);

    // World projection/view
    state_ptr->near_clip = 0.1f;
    state_ptr->far_clip = 1000.0f;
    state_ptr->projection = mat4_perspective(deg_to_rad(45.0f), 1280 / 720.0f, state_ptr->near_clip, state_ptr->far_clip);
    // TODO: configurable camera starting position.
    state_ptr->view = mat4_translation((vec3){0, 0, -30.0f});
    state_ptr->view = mat4_inverse(state_ptr->view);

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
    // If the begin frame returned successfully, mid-frame operations may continue.
    if (state_ptr->backend.begin_frame(&state_ptr->backend, packet->delta_time)) {
        // World renderpass
        if (!state_ptr->backend.begin_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_WORLD)) {
            KERROR("backend.begin_renderpass -> BUILTIN_RENDERPASS_WORLD failed. Application shutting down...");
            return false;
        }

        shader_system_use(BUILTIN_SHADER_NAME_MATERIAL);

        // Apply globals
        // TODO: Shader system bind/set uniforms
        // state_ptr->backend.shader_bind_globals(state_ptr->material_shader_id);
        shader_system_uniform_set("projection", &state_ptr->projection);
        shader_system_uniform_set("view", &state_ptr->view);
        shader_system_apply_global();
        // state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_projection_location, state_ptr->projection);
        // state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_view_location, state_ptr->view);
        // state_ptr->backend.shader_apply_globals(state_ptr->material_shader_id);

        // Draw geometries.
        u32 count = packet->geometry_count;
        for (u32 i = 0; i < count; ++i) {
            material* m = 0;
            if (packet->geometries[i].geometry->material) {
                m = packet->geometries[i].geometry->material;
            } else {
                m = material_system_get_default();
            }

            // Apply the material
            // state_ptr->backend.shader_bind_instance(state_ptr->material_shader_id, m->internal_id);
            shader_system_bind_instance(m->internal_id);
            shader_system_uniform_set("diffuse_colour", &m->diffuse_colour);
            shader_system_uniform_set("diffuse_texture", m->diffuse_map.texture);
            shader_system_apply_instance();
            // state_ptr->backend.shader_set_uniform_vec4(state_ptr->material_shader_id, state_ptr->material_shader_diffuse_colour_location, m->diffuse_colour);
            // state_ptr->backend.shader_set_sampler(state_ptr->material_shader_id, state_ptr->material_shader_diffuse_texture_location, m->diffuse_map.texture);
            // state_ptr->backend.shader_apply_instance(state_ptr->material_shader_id);

            // Apply the locals
            shader_system_uniform_set("model", &packet->geometries[i].model);
            // state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_model_location, packet->geometries[i].model);

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
        shader_system_use(BUILTIN_SHADER_NAME_UI);

        // Apply globals
        // state_ptr->backend.shader_bind_globals(state_ptr->ui_shader_id);
        // state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_projection_location, state_ptr->ui_projection);
        // state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_view_location, state_ptr->ui_view);
        // state_ptr->backend.shader_apply_globals(state_ptr->ui_shader_id);
        shader_system_uniform_set("projection", &state_ptr->projection);
        shader_system_uniform_set("view", &state_ptr->view);
        shader_system_apply_global();

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
            shader_system_bind_instance(m->internal_id);
            shader_system_uniform_set("diffuse_colour", &m->diffuse_colour);
            shader_system_uniform_set("diffuse_texture", &m->diffuse_map.texture);
            shader_system_apply_instance();
            // state_ptr->backend.shader_bind_instance(state_ptr->ui_shader_id, m->internal_id);
            // state_ptr->backend.shader_set_uniform_vec4(state_ptr->ui_shader_id, state_ptr->ui_shader_diffuse_colour_location, m->diffuse_colour);
            // state_ptr->backend.shader_set_sampler(state_ptr->ui_shader_id, state_ptr->ui_shader_diffuse_texture_location, m->diffuse_map.texture);
            // state_ptr->backend.shader_apply_instance(state_ptr->ui_shader_id);

            // Apply the locals
            shader_system_uniform_set("model", &packet->geometries[i].model);
            // state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_model_location, packet->ui_geometries[i].model);

            state_ptr->backend.draw_geometry(packet->ui_geometries[i]);
        }

        if (!state_ptr->backend.end_renderpass(&state_ptr->backend, BUILTIN_RENDERPASS_UI)) {
            KERROR("backend.end_renderpass -> BUILTIN_RENDERPASS_UI failed. Application shutting down...");
            return false;
        }
        // End UI renderpass

        // End the frame. If this fails, it is likely unrecoverable.
        b8 result = state_ptr->backend.end_frame(&state_ptr->backend, packet->delta_time);
        state_ptr->backend.frame_number++;

        if (!result) {
            KERROR("renderer_end_frame failed. Application shutting down...");
            return false;
        }
    }

    return true;
}

void renderer_set_view(mat4 view) {
    state_ptr->view = view;
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

b8 renderer_shader_apply_instance(shader* s) {
    return state_ptr->backend.shader_apply_instance(s);
}

b8 renderer_shader_acquire_instance_resources(shader* s, u32* out_instance_id) {
    return state_ptr->backend.shader_acquire_instance_resources(s, out_instance_id);
}

b8 renderer_shader_release_instance_resources(shader* s, u32 instance_id) {
    return state_ptr->backend.shader_release_instance_resources(s, instance_id);
}

b8 renderer_set_uniform(shader* frontend_shader, shader_uniform* uniform, const void* value) {
    return state_ptr->backend.shader_set_uniform(frontend_shader, uniform, value);
}
