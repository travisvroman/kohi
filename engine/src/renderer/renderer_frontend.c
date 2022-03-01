#include "renderer_frontend.h"

#include "renderer_backend.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "math/kmath.h"

#include "resources/resource_types.h"
#include "systems/texture_system.h"
#include "systems/material_system.h"

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
    // Material shader
    u32 material_shader_id;
    u32 material_shader_projection_location;
    u32 material_shader_view_location;
    u32 material_shader_diffuse_colour_location;
    u32 material_shader_diffuse_texture_location;
    u32 material_shader_model_location;
    // UI shader
    u32 ui_shader_id;
    u32 ui_shader_projection_location;
    u32 ui_shader_view_location;
    u32 ui_shader_diffuse_colour_location;
    u32 ui_shader_diffuse_texture_location;
    u32 ui_shader_model_location;
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

    CRITICAL_INIT(state_ptr->backend.initialize(&state_ptr->backend, application_name), "Renderer backend failed to initialize. Shutting down.");
    // if (!state_ptr->backend.initialize(&state_ptr->backend, application_name)) {
    //     KFATAL("Renderer backend failed to initialize. Shutting down.");
    //     return false;
    // }

    // Shaders
    // TODO: Move this shader to material system.
    const char* mat_shader_error = "Error creating built-in material shader.";
    CRITICAL_INIT(
        state_ptr->backend.shader_create(
            "Builtin.MaterialShader",
            BUILTIN_RENDERPASS_WORLD,
            SHADER_STAGE_VERTEX | SHADER_STAGE_FRAGMENT,
            true,
            true,
            &state_ptr->material_shader_id),
        mat_shader_error);

    // Attributes: Position, texcoord
    CRITICAL_INIT(state_ptr->backend.shader_add_attribute(state_ptr->material_shader_id, "in_position", SHADER_ATTRIB_TYPE_FLOAT32_3), mat_shader_error);
    CRITICAL_INIT(state_ptr->backend.shader_add_attribute(state_ptr->material_shader_id, "in_texcoord", SHADER_ATTRIB_TYPE_FLOAT32_2), mat_shader_error);

    // Uniforms: Global
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(
            state_ptr->material_shader_id,
            "projection",
            SHADER_SCOPE_GLOBAL,
            &state_ptr->material_shader_projection_location),
        mat_shader_error);
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(state_ptr->material_shader_id, "view", SHADER_SCOPE_GLOBAL, &state_ptr->material_shader_view_location),
        mat_shader_error);

    // Uniforms: Instance
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_vec4(
            state_ptr->material_shader_id,
            "diffuse_colour",
            SHADER_SCOPE_INSTANCE,
            &state_ptr->material_shader_diffuse_colour_location),
        mat_shader_error);
    CRITICAL_INIT(
        state_ptr->backend.shader_add_sampler(
            state_ptr->material_shader_id,
            "diffuse_texture",
            SHADER_SCOPE_INSTANCE,
            &state_ptr->material_shader_diffuse_texture_location),
        mat_shader_error);

    // Uniforms: Local
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(state_ptr->material_shader_id, "model", SHADER_SCOPE_LOCAL, &state_ptr->material_shader_model_location),
        mat_shader_error);

    // Initialize
    CRITICAL_INIT(state_ptr->backend.shader_initialize(state_ptr->material_shader_id), mat_shader_error);

    // UI shader
    const char* ui_shader_error = "Error creating built-in UI shader.";
    // TODO: Move this shader to material system.
    CRITICAL_INIT(
        state_ptr->backend.shader_create(
            "Builtin.UIShader",
            BUILTIN_RENDERPASS_UI,
            SHADER_STAGE_VERTEX | SHADER_STAGE_FRAGMENT,
            true,
            true,
            &state_ptr->ui_shader_id),
        ui_shader_error);
    // Attributes: Position, texcoord
    CRITICAL_INIT(state_ptr->backend.shader_add_attribute(state_ptr->ui_shader_id, "in_position", SHADER_ATTRIB_TYPE_FLOAT32_2), ui_shader_error);
    CRITICAL_INIT(state_ptr->backend.shader_add_attribute(state_ptr->ui_shader_id, "in_texcoord", SHADER_ATTRIB_TYPE_FLOAT32_2), ui_shader_error);

    // Uniforms: Global
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(state_ptr->ui_shader_id, "projection", SHADER_SCOPE_GLOBAL, &state_ptr->ui_shader_projection_location),
        ui_shader_error);
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(state_ptr->ui_shader_id, "view", SHADER_SCOPE_GLOBAL, &state_ptr->ui_shader_view_location),
        ui_shader_error);

    // Uniforms: Instance
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_vec4(state_ptr->ui_shader_id, "diffuse_colour", SHADER_SCOPE_INSTANCE, &state_ptr->ui_shader_diffuse_colour_location),
        ui_shader_error);
    CRITICAL_INIT(
        state_ptr->backend.shader_add_sampler(state_ptr->ui_shader_id, "diffuse_texture", SHADER_SCOPE_INSTANCE, &state_ptr->ui_shader_diffuse_texture_location),
        ui_shader_error);

    // Uniforms: Local
    CRITICAL_INIT(
        state_ptr->backend.shader_add_uniform_mat4(state_ptr->ui_shader_id, "model", SHADER_SCOPE_LOCAL, &state_ptr->ui_shader_model_location),
        ui_shader_error);

    // Initialize
    CRITICAL_INIT(state_ptr->backend.shader_initialize(state_ptr->ui_shader_id), ui_shader_error);

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
        state_ptr->backend.shader_destroy(state_ptr->material_shader_id);
        state_ptr->material_shader_id = INVALID_ID;

        state_ptr->backend.shader_destroy(state_ptr->ui_shader_id);
        state_ptr->ui_shader_id = INVALID_ID;

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

        state_ptr->backend.shader_use(state_ptr->material_shader_id);

        // Apply globals
        state_ptr->backend.shader_bind_globals(state_ptr->material_shader_id);
        state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_projection_location, state_ptr->projection);
        state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_view_location, state_ptr->view);
        state_ptr->backend.shader_apply_globals(state_ptr->material_shader_id);

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
            state_ptr->backend.shader_bind_instance(state_ptr->material_shader_id, m->internal_id);
            state_ptr->backend.shader_set_uniform_vec4(state_ptr->material_shader_id, state_ptr->material_shader_diffuse_colour_location, m->diffuse_colour);
            state_ptr->backend.shader_set_sampler(state_ptr->material_shader_id, state_ptr->material_shader_diffuse_texture_location, m->diffuse_map.texture);
            state_ptr->backend.shader_apply_instance(state_ptr->material_shader_id);

            // Apply the locals
            state_ptr->backend.shader_set_uniform_mat4(state_ptr->material_shader_id, state_ptr->material_shader_model_location, packet->geometries[i].model);

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
        // state_ptr->backend.update_global_ui_state(state_ptr->ui_projection, state_ptr->ui_view, 0);
        state_ptr->backend.shader_use(state_ptr->ui_shader_id);

        // Apply globals
        state_ptr->backend.shader_bind_globals(state_ptr->ui_shader_id);
        state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_projection_location, state_ptr->ui_projection);
        state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_view_location, state_ptr->ui_view);
        state_ptr->backend.shader_apply_globals(state_ptr->ui_shader_id);

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
            state_ptr->backend.shader_bind_instance(state_ptr->ui_shader_id, m->internal_id);
            state_ptr->backend.shader_set_uniform_vec4(state_ptr->ui_shader_id, state_ptr->ui_shader_diffuse_colour_location, m->diffuse_colour);
            state_ptr->backend.shader_set_sampler(state_ptr->ui_shader_id, state_ptr->ui_shader_diffuse_texture_location, m->diffuse_map.texture);
            state_ptr->backend.shader_apply_instance(state_ptr->ui_shader_id);

            // Apply the locals
            state_ptr->backend.shader_set_uniform_mat4(state_ptr->ui_shader_id, state_ptr->ui_shader_model_location, packet->ui_geometries[i].model);

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

b8 renderer_create_material(struct material* material) {
    // TODO: get shader by name
    u32 shader_id = material->type == MATERIAL_TYPE_UI ? state_ptr->ui_shader_id : state_ptr->material_shader_id;
    return state_ptr->backend.shader_acquire_instance_resources(shader_id, &material->internal_id);
}

void renderer_destroy_material(struct material* material) {
    // TODO: get shader by name
    u32 shader_id = material->type == MATERIAL_TYPE_UI ? state_ptr->ui_shader_id : state_ptr->material_shader_id;
    state_ptr->backend.shader_release_instance_resources(shader_id, material->internal_id);
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
        *out_renderpass_id = 1;
        return true;
    } else if (strings_equali("Renderpass.Builtin.UI", name)) {
        *out_renderpass_id = 2;
        return true;
    }

    KERROR("renderer_renderpass_id: No renderpass named '%s'.", name);
    *out_renderpass_id = INVALID_ID_U8;
    return false;
}

b8 renderer_shader_create(const char* name, u8 renderpass_id, u32 stages, b8 use_instances, b8 use_local, u32* out_shader_id) {
    return state_ptr->backend.shader_create(name, renderpass_id, stages, use_instances, use_local, out_shader_id);
}

void renderer_shader_destroy(u32 shader_id) {
    state_ptr->backend.shader_destroy(shader_id);
}

b8 renderer_shader_add_attribute(u32 shader_id, const char* name, shader_attribute_type type) {
    return state_ptr->backend.shader_add_attribute(shader_id, name, type);
}

b8 renderer_shader_add_sampler(u32 shader_id, const char* sampler_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_sampler(shader_id, sampler_name, scope, out_location);
}

b8 renderer_shader_add_uniform_i8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_i8(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_i16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_i16(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_i32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_i32(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_u8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_u8(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_u16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_u16(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_u32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_u32(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_f32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_f32(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_vec2(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_vec2(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_vec3(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_vec3(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_vec4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_vec4(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_mat4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_mat4(shader_id, uniform_name, scope, out_location);
}

b8 renderer_shader_add_uniform_custom(u32 shader_id, const char* uniform_name, u32 size, shader_scope scope, u32* out_location) {
    return state_ptr->backend.shader_add_uniform_custom(shader_id, uniform_name, size, scope, out_location);
}

b8 renderer_shader_add_uniform(u32 shader_id, const char* uniform_name, shader_uniform_type type, shader_scope scope, u32* out_location) {
    if (type == SHADER_UNIFORM_TYPE_CUSTOM) {
        KERROR("renderer_shader_add_uniform does not accept uniform type SHADER_UNIFORM_TYPE_CUSTOM. Call renderer_shader_add_uniform_custom for this type instead.");
        return false;
    }

    renderer_backend* b = &state_ptr->backend;
    switch (type) {
        // Float types
        case SHADER_UNIFORM_TYPE_FLOAT32:
            return b->shader_add_uniform_f32(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_FLOAT32_2:
            return b->shader_add_uniform_vec2(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_FLOAT32_3:
            return b->shader_add_uniform_vec3(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_FLOAT32_4:
            return b->shader_add_uniform_vec4(shader_id, uniform_name, scope, out_location);

        // Unsigned int types
        case SHADER_UNIFORM_TYPE_UINT8:
            return b->shader_add_uniform_u8(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_UINT16:
            return b->shader_add_uniform_u16(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_UINT32:
            return b->shader_add_uniform_u32(shader_id, uniform_name, scope, out_location);

            // Signed int types
        case SHADER_UNIFORM_TYPE_INT8:
            return b->shader_add_uniform_i8(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_INT16:
            return b->shader_add_uniform_i16(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_INT32:
            return b->shader_add_uniform_i32(shader_id, uniform_name, scope, out_location);
        case SHADER_UNIFORM_TYPE_MATRIX_4:
            return b->shader_add_uniform_mat4(shader_id, uniform_name, scope, out_location);

        default:
            KERROR("renderer_shader_add_uniform Unknown shader type: %d", type);
            return false;
    }
}

b8 renderer_shader_initialize(u32 shader_id) {
    return state_ptr->backend.shader_initialize(shader_id);
}

b8 renderer_shader_use(u32 shader_id) {
    return state_ptr->backend.shader_use(shader_id);
}

b8 renderer_shader_bind_globals(u32 shader_id) {
    return state_ptr->backend.shader_bind_globals(shader_id);
}

b8 renderer_shader_bind_instance(u32 shader_id, u32 instance_id) {
    return state_ptr->backend.shader_bind_instance(shader_id, instance_id);
}

b8 renderer_shader_apply_globals(u32 shader_id) {
    return state_ptr->backend.shader_apply_globals(shader_id);
}

b8 renderer_shader_apply_instance(u32 shader_id) {
    return state_ptr->backend.shader_apply_instance(shader_id);
}

b8 renderer_shader_acquire_instance_resources(u32 shader_id, u32* out_instance_id) {
    return state_ptr->backend.shader_acquire_instance_resources(shader_id, out_instance_id);
}

b8 renderer_shader_release_instance_resources(u32 shader_id, u32 instance_id) {
    return state_ptr->backend.shader_release_instance_resources(shader_id, instance_id);
}

u32 renderer_shader_uniform_location(u32 shader_id, const char* uniform_name) {
    return state_ptr->backend.shader_uniform_location(shader_id, uniform_name);
}

b8 renderer_shader_set_sampler(u32 shader_id, u32 location, texture* t) {
    return state_ptr->backend.shader_set_sampler(shader_id, location, t);
}

b8 renderer_shader_set_uniform_i8(u32 shader_id, u32 location, i8 value) {
    return state_ptr->backend.shader_set_uniform_i8(shader_id, location, value);
}

b8 renderer_shader_set_uniform_i16(u32 shader_id, u32 location, i16 value) {
    return state_ptr->backend.shader_set_uniform_i16(shader_id, location, value);
}

b8 renderer_shader_set_uniform_i32(u32 shader_id, u32 location, i32 value) {
    return state_ptr->backend.shader_set_uniform_i32(shader_id, location, value);
}

b8 renderer_shader_set_uniform_u8(u32 shader_id, u32 location, u8 value) {
    return state_ptr->backend.shader_set_uniform_u8(shader_id, location, value);
}

b8 renderer_shader_set_uniform_u16(u32 shader_id, u32 location, u16 value) {
    return state_ptr->backend.shader_set_uniform_u16(shader_id, location, value);
}

b8 renderer_shader_set_uniform_u32(u32 shader_id, u32 location, u32 value) {
    return state_ptr->backend.shader_set_uniform_u32(shader_id, location, value);
}

b8 renderer_shader_set_uniform_f32(u32 shader_id, u32 location, f32 value) {
    return state_ptr->backend.shader_set_uniform_f32(shader_id, location, value);
}

b8 renderer_shader_set_uniform_vec2(u32 shader_id, u32 location, vec2 value) {
    return state_ptr->backend.shader_set_uniform_vec2(shader_id, location, value);
}

b8 renderer_shader_set_uniform_vec2f(u32 shader_id, u32 location, f32 value_0, f32 value_1) {
    return state_ptr->backend.shader_set_uniform_vec2f(shader_id, location, value_0, value_1);
}

b8 renderer_shader_set_uniform_vec3(u32 shader_id, u32 location, vec3 value) {
    return state_ptr->backend.shader_set_uniform_vec3(shader_id, location, value);
}

b8 renderer_shader_set_uniform_vec3f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2) {
    return state_ptr->backend.shader_set_uniform_vec3f(shader_id, location, value_0, value_1, value_2);
}

b8 renderer_shader_set_uniform_vec4(u32 shader_id, u32 location, vec4 value) {
    return state_ptr->backend.shader_set_uniform_vec4(shader_id, location, value);
}

b8 renderer_shader_set_uniform_vec4f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3) {
    return state_ptr->backend.shader_set_uniform_vec4f(shader_id, location, value_0, value_1, value_2, value_3);
}

b8 renderer_shader_set_uniform_mat4(u32 shader_id, u32 location, mat4 value) {
    return state_ptr->backend.shader_set_uniform_mat4(shader_id, location, value);
}

b8 renderer_shader_set_uniform_custom(u32 shader_id, u32 location, void* value) {
    return state_ptr->backend.shader_set_uniform_custom(shader_id, location, value);
}
