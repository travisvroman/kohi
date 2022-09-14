#include "renderer_frontend.h"

#include "renderer_backend.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "containers/freelist.h"
#include "math/kmath.h"
#include "platform/platform.h"

#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "systems/texture_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"
#include "systems/render_view_system.h"

// TODO: temporary
#include "core/kstring.h"
#include "core/event.h"

// TODO: end temporary

typedef struct renderer_system_state {
    renderer_backend backend;
    u32 skybox_shader_id;
    u32 material_shader_id;
    u32 ui_shader_id;
    // The number of render targets. Typically lines up with the amount of swapchain images.
    u8 window_render_target_count;
    // The current window framebuffer width.
    u32 framebuffer_width;
    // The current window framebuffer height.
    u32 framebuffer_height;

    // A pointer to the skybox renderpass. TODO: Configurable via views.
    renderpass* skybox_renderpass;
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

    renderer_backend_config renderer_config = {};
    renderer_config.application_name = application_name;
    renderer_config.on_rendertarget_refresh_required = regenerate_render_targets;

    // Renderpasses. TODO: read config from file.
    renderer_config.renderpass_count = 3;
    const char* skybox_renderpass_name = "Renderpass.Builtin.Skybox";
    const char* world_renderpass_name = "Renderpass.Builtin.World";
    const char* ui_renderpass_name = "Renderpass.Builtin.UI";
    renderpass_config pass_configs[3];
    pass_configs[0].name = skybox_renderpass_name;
    pass_configs[0].prev_name = 0;
    pass_configs[0].next_name = world_renderpass_name;
    pass_configs[0].render_area = (vec4){0, 0, 1280, 720};
    pass_configs[0].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    pass_configs[0].clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;

    pass_configs[1].name = world_renderpass_name;
    pass_configs[1].prev_name = skybox_renderpass_name;
    pass_configs[1].next_name = ui_renderpass_name;
    pass_configs[1].render_area = (vec4){0, 0, 1280, 720};
    pass_configs[1].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    pass_configs[1].clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;

    pass_configs[2].name = ui_renderpass_name;
    pass_configs[2].prev_name = world_renderpass_name;
    pass_configs[2].next_name = 0;
    pass_configs[2].render_area = (vec4){0, 0, 1280, 720};
    pass_configs[2].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    pass_configs[2].clear_flags = RENDERPASS_CLEAR_NONE_FLAG;

    renderer_config.pass_configs = pass_configs;

    // Initialize the backend.
    CRITICAL_INIT(state_ptr->backend.initialize(&state_ptr->backend, &renderer_config, &state_ptr->window_render_target_count), "Renderer backend failed to initialize. Shutting down.");

    // TODO: Will know how to get these when we define views.
    state_ptr->skybox_renderpass = state_ptr->backend.renderpass_get(skybox_renderpass_name);
    state_ptr->skybox_renderpass->render_target_count = state_ptr->window_render_target_count;
    state_ptr->skybox_renderpass->targets = kallocate(sizeof(render_target) * state_ptr->window_render_target_count, MEMORY_TAG_ARRAY);

    state_ptr->world_renderpass = state_ptr->backend.renderpass_get(world_renderpass_name);
    state_ptr->world_renderpass->render_target_count = state_ptr->window_render_target_count;
    state_ptr->world_renderpass->targets = kallocate(sizeof(render_target) * state_ptr->window_render_target_count, MEMORY_TAG_ARRAY);

    state_ptr->ui_renderpass = state_ptr->backend.renderpass_get(ui_renderpass_name);
    state_ptr->ui_renderpass->render_target_count = state_ptr->window_render_target_count;
    state_ptr->ui_renderpass->targets = kallocate(sizeof(render_target) * state_ptr->window_render_target_count, MEMORY_TAG_ARRAY);

    regenerate_render_targets();

    // Update the skybox renderpass dimensions.
    state_ptr->skybox_renderpass->render_area.x = 0;
    state_ptr->skybox_renderpass->render_area.y = 0;
    state_ptr->skybox_renderpass->render_area.z = state_ptr->framebuffer_width;
    state_ptr->skybox_renderpass->render_area.w = state_ptr->framebuffer_height;

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

    // Builtin skybox shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_SKYBOX, RESOURCE_TYPE_SHADER, 0, &config_resource),
        "Failed to load builtin skybox shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin skybox shader.");
    resource_system_unload(&config_resource);
    state_ptr->skybox_shader_id = shader_system_get_id(BUILTIN_SHADER_NAME_SKYBOX);

    // Builtin material shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_MATERIAL, RESOURCE_TYPE_SHADER, 0, &config_resource),
        "Failed to load builtin material shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin material shader.");
    resource_system_unload(&config_resource);
    state_ptr->material_shader_id = shader_system_get_id(BUILTIN_SHADER_NAME_MATERIAL);

    // Builtin UI shader.
    CRITICAL_INIT(
        resource_system_load(BUILTIN_SHADER_NAME_UI, RESOURCE_TYPE_SHADER, 0, &config_resource),
        "Failed to load builtin UI shader.");
    config = (shader_config*)config_resource.data;
    CRITICAL_INIT(shader_system_create(config), "Failed to load builtin UI shader.");
    resource_system_unload(&config_resource);
    state_ptr->ui_shader_id = shader_system_get_id(BUILTIN_SHADER_NAME_UI);

    return true;
}

void renderer_system_shutdown(void* state) {
    if (state_ptr) {
        // Destroy render targets.
        for (u8 i = 0; i < state_ptr->window_render_target_count; ++i) {
            state_ptr->backend.render_target_destroy(&state_ptr->skybox_renderpass->targets[i], true);
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
            render_view_system_on_window_resize(width, height);
            state_ptr->backend.resized(&state_ptr->backend, width, height);

            state_ptr->frames_since_resize = 0;
            state_ptr->resizing = false;
        } else {
            // Skip rendering the frame and try again next time.
            // NOTE: Simulate a frame being "drawn" at 60 FPS.
            platform_sleep(16);
            return true;
        }
    }

    // If the begin frame returned successfully, mid-frame operations may continue.
    if (state_ptr->backend.begin_frame(&state_ptr->backend, packet->delta_time)) {
        u8 attachment_index = state_ptr->backend.window_attachment_index_get();

        // Render each view.
        for (u32 i = 0; i < packet->view_count; ++i) {
            if (!render_view_system_on_render(packet->views[i].view, &packet->views[i], state_ptr->backend.frame_number, attachment_index)) {
                KERROR("Error rendering view index %i.", i);
                return false;
            }
        }

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

void renderer_draw_geometry(geometry_render_data* data) {
    state_ptr->backend.draw_geometry(data);
}

b8 renderer_renderpass_begin(renderpass* pass, render_target* target) {
    return state_ptr->backend.renderpass_begin(pass, target);
}

b8 renderer_renderpass_end(renderpass* pass) {
    return state_ptr->backend.renderpass_end(pass);
}

renderpass* renderer_renderpass_get(const char* name) {
    return state_ptr->backend.renderpass_get(name);
}

b8 renderer_shader_create(shader* s, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages) {
    return state_ptr->backend.shader_create(s, config, pass, stage_count, stage_filenames, stages);
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

b8 renderer_is_multithreaded() {
    return state_ptr->backend.is_multithreaded();
}

void regenerate_render_targets() {
    // Create render targets for each. TODO: Should be configurable.
    for (u8 i = 0; i < state_ptr->window_render_target_count; ++i) {
        // Destroy the old first if they exist.
        state_ptr->backend.render_target_destroy(&state_ptr->skybox_renderpass->targets[i], false);
        state_ptr->backend.render_target_destroy(&state_ptr->world_renderpass->targets[i], false);
        state_ptr->backend.render_target_destroy(&state_ptr->ui_renderpass->targets[i], false);

        texture* window_target_texture = state_ptr->backend.window_attachment_get(i);
        texture* depth_target_texture = state_ptr->backend.depth_attachment_get();

        // Skybox render targets
        texture* skybox_attachments[1] = {window_target_texture};
        state_ptr->backend.render_target_create(
            1,
            skybox_attachments,
            state_ptr->skybox_renderpass,
            state_ptr->framebuffer_width,
            state_ptr->framebuffer_height,
            &state_ptr->skybox_renderpass->targets[i]);

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

b8 renderer_renderbuffer_create(renderbuffer_type type, u64 total_size, b8 use_freelist, renderbuffer* out_buffer) {
    if (!out_buffer) {
        KERROR("renderer_renderbuffer_create requires a valid pointer to hold the created buffer.");
        return false;
    }

    kzero_memory(out_buffer, sizeof(renderbuffer));

    out_buffer->type = type;
    out_buffer->total_size = total_size;

    // Create the freelist, if needed.
    if (use_freelist) {
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, 0, 0);
        out_buffer->freelist_block = kallocate(out_buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, out_buffer->freelist_block, &out_buffer->buffer_freelist);
    }

    // Create the internal buffer from the backend.
    if (!state_ptr->backend.renderbuffer_create_internal(out_buffer)) {
        KFATAL("Unable to create backing buffer for renderbuffer. Application cannot continue.");
        return false;
    }

    return true;
}

void renderer_renderbuffer_destroy(renderbuffer* buffer) {
    if (buffer) {
        if (buffer->freelist_memory_requirement > 0) {
            freelist_destroy(&buffer->buffer_freelist);
            kfree(buffer->freelist_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
            buffer->freelist_memory_requirement = 0;
        }

        // Free up the backend resources.
        state_ptr->backend.renderbuffer_destroy_internal(buffer);
        buffer->internal_data = 0;
    }
}

b8 renderer_renderbuffer_bind(renderbuffer* buffer, u64 offset) {
    if (!buffer) {
        KERROR("renderer_renderbuffer_bind requires a valid pointer to a buffer.");
        return false;
    }

    return state_ptr->backend.renderbuffer_bind(buffer, offset);
}

b8 renderer_renderbuffer_unbind(renderbuffer* buffer) {
    return state_ptr->backend.renderbuffer_unbind(buffer);
}

void* renderer_renderbuffer_map_memory(renderbuffer* buffer, u64 offset, u64 size) {
    return state_ptr->backend.renderbuffer_map_memory(buffer, offset, size);
}

void renderer_renderbuffer_unmap_memory(renderbuffer* buffer, u64 offset, u64 size) {
    state_ptr->backend.renderbuffer_unmap_memory(buffer, offset, size);
}

b8 renderer_renderbuffer_flush(renderbuffer* buffer, u64 offset, u64 size) {
    return state_ptr->backend.renderbuffer_flush(buffer, offset, size);
}

b8 renderer_renderbuffer_read(renderbuffer* buffer, u64 offset, u64 size, void** out_memory) {
    return state_ptr->backend.renderbuffer_read(buffer, offset, size, out_memory);
}

b8 renderer_renderbuffer_resize(renderbuffer* buffer, u64 new_total_size) {
    // Sanity check.
    if (new_total_size <= buffer->total_size) {
        KERROR("renderer_renderbuffer_resize requires that new size be larger than the old. Not doing this could lead to data loss.");
        return false;
    }

    if (buffer->freelist_memory_requirement > 0) {
        // Resize the freelist first, if used.
        u64 new_memory_requirement = 0;
        freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, 0, 0, 0);
        void* new_block = kallocate(new_memory_requirement, MEMORY_TAG_RENDERER);
        void* old_block = 0;
        if (!freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, new_block, new_total_size, &old_block)) {
            KERROR("renderer_renderbuffer_resize failed to resize internal free list.");
            kfree(new_block, new_memory_requirement, MEMORY_TAG_RENDERER);
            return false;
        }

        // Clean up the old memory, then assign the new properties over.
        kfree(old_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        buffer->freelist_memory_requirement = new_memory_requirement;
        buffer->freelist_block = new_block;
    }

    b8 result = state_ptr->backend.renderbuffer_resize(buffer, new_total_size);
    if (result) {
        buffer->total_size = new_total_size;
    } else {
        KERROR("Failed to resize internal renderbuffer resources.");
    }
    return result;
}

b8 renderer_renderbuffer_allocate(renderbuffer* buffer, u64 size, u64* out_offset) {
    if (!buffer || !size || !out_offset) {
        KERROR("vulkan_buffer_allocate requires valid buffer, a nonzero size and valid pointer to hold offset.");
        return false;
    }

    if (buffer->freelist_memory_requirement == 0) {
        KWARN("vulkan_buffer_allocate called on a buffer not using freelists. Offset will not be valid. Call renderer_renderbuffer_load_range instead.");
        *out_offset = 0;
        return true;
    }
    return freelist_allocate_block(&buffer->buffer_freelist, size, out_offset);
}

b8 renderer_renderbuffer_free(renderbuffer* buffer, u64 size, u64 offset) {
    if (!buffer || !size) {
        KERROR("vulkan_buffer_free requires valid buffer and a nonzero size.");
        return false;
    }

    if (buffer->freelist_memory_requirement == 0) {
        KWARN("vulkan_buffer_allocate called on a buffer not using freelists. Nothing was done.");
        return true;
    }
    return freelist_free_block(&buffer->buffer_freelist, size, offset);
}

b8 renderer_renderbuffer_load_range(renderbuffer* buffer, u64 offset, u64 size, const void* data) {
    return state_ptr->backend.renderbuffer_load_range(buffer, offset, size, data);
}

b8 renderer_renderbuffer_copy_range(renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size) {
    return state_ptr->backend.renderbuffer_copy_range(source, source_offset, dest, dest_offset, size);
}

b8 renderer_renderbuffer_draw(renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only) {
    return state_ptr->backend.renderbuffer_draw(buffer, offset, element_count, bind_only);
}
