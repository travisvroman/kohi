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
} renderer_system_state;

static renderer_system_state* state_ptr;

b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name) {
    *memory_requirement = sizeof(renderer_system_state);
    if (state == 0) {
        return true;
    }
    state_ptr = state;

    // TODO: make this configurable.
    renderer_backend_create(RENDERER_BACKEND_TYPE_VULKAN, &state_ptr->backend);
    state_ptr->backend.frame_number = 0;

    if (!state_ptr->backend.initialize(&state_ptr->backend, application_name)) {
        KFATAL("Renderer backend failed to initialize. Shutting down.");
        return false;
    }

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
        state_ptr->ui_projection = mat4_orthographic(0, (f32)width, (f32)height, 0, -100.f, 100.0f); // Intentionally flipped on y axis.
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

        state_ptr->backend.update_global_world_state(state_ptr->projection, state_ptr->view, vec3_zero(), vec4_one(), 0);

        // Draw geometries.
        u32 count = packet->geometry_count;
        for (u32 i = 0; i < count; ++i) {
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
        state_ptr->backend.update_global_ui_state(state_ptr->ui_projection, state_ptr->ui_view, 0);

        // Draw ui geometries.
        count = packet->ui_geometry_count;
        for (u32 i = 0; i < count; ++i) {
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
    return state_ptr->backend.create_material(material);
}

void renderer_destroy_material(struct material* material) {
    state_ptr->backend.destroy_material(material);
}

b8 renderer_create_geometry(geometry* geometry, u32 vertex_count, const vertex_3d* vertices, u32 index_count, const u32* indices) {
    return state_ptr->backend.create_geometry(geometry, vertex_count, vertices, index_count, indices);
}

void renderer_destroy_geometry(geometry* geometry) {
    state_ptr->backend.destroy_geometry(geometry);
}