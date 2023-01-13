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
    // The number of render targets. Typically lines up with the amount of swapchain images.
    u8 window_render_target_count;
    // The current window framebuffer width.
    u32 framebuffer_width;
    // The current window framebuffer height.
    u32 framebuffer_height;

    // Indicates if the window is currently being resized.
    b8 resizing;
    // The current number of frames since the last resize operation.'
    // Only set if resizing = true. Otherwise 0.
    u8 frames_since_resize;
} renderer_system_state;

static renderer_system_state* state_ptr;

b8 renderer_system_initialize(u64* memory_requirement, void* state, void* config) {
    renderer_system_config* typed_config = (renderer_system_config*)config;
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
    renderer_config.application_name = typed_config->application_name;
    // TODO: expose this to the application to configure.
    renderer_config.flags = RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT | RENDERER_CONFIG_FLAG_POWER_SAVING_BIT;

    // Initialize the backend.
    if (!state_ptr->backend.initialize(&state_ptr->backend, &renderer_config, &state_ptr->window_render_target_count)) {
        KERROR("Renderer backend failed to initialize. Shutting down.");
        return false;
    }

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

            // Notify views of the resize.
            render_view_system_on_window_resize(width, height);

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

void renderer_viewport_set(vec4 rect) {
    state_ptr->backend.viewport_set(rect);
}

void renderer_viewport_reset() {
    state_ptr->backend.viewport_reset();
}

void renderer_scissor_set(vec4 rect) {
    state_ptr->backend.scissor_set(rect);
}

void renderer_scissor_reset() {
    state_ptr->backend.scissor_reset();
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

void renderer_texture_read_data(texture* t, u32 offset, u32 size, void** out_memory) {
    state_ptr->backend.texture_read_data(t, offset, size, out_memory);
}

void renderer_texture_read_pixel(texture* t, u32 x, u32 y, u8** out_rgba) {
    state_ptr->backend.texture_read_pixel(t, x, y, out_rgba);
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

void renderer_render_target_create(u8 attachment_count, render_target_attachment* attachments, renderpass* pass, u32 width, u32 height, render_target* out_target) {
    state_ptr->backend.render_target_create(attachment_count, attachments, pass, width, height, out_target);
}

void renderer_render_target_destroy(render_target* target, b8 free_internal_memory) {
    state_ptr->backend.render_target_destroy(target, free_internal_memory);

    if (free_internal_memory) {
        kzero_memory(target, sizeof(render_target));
    }
}

texture* renderer_window_attachment_get(u8 index) {
    return state_ptr->backend.window_attachment_get(index);
}

texture* renderer_depth_attachment_get(u8 index) {
    return state_ptr->backend.depth_attachment_get(index);
}

u8 renderer_window_attachment_index_get() {
    return state_ptr->backend.window_attachment_index_get();
}

u8 renderer_window_attachment_count_get() {
    return state_ptr->backend.window_attachment_count_get();
}

b8 renderer_renderpass_create(const renderpass_config* config, renderpass* out_renderpass) {
    if (!config) {
        KERROR("Renderpass config is required.");
        return false;
    }

    if (config->render_target_count == 0) {
        KERROR("Cannot have a renderpass target count of 0, ya dingus.");
        return false;
    }

    out_renderpass->render_target_count = config->render_target_count;
    out_renderpass->targets = kallocate(sizeof(render_target) * out_renderpass->render_target_count, MEMORY_TAG_ARRAY);
    out_renderpass->clear_flags = config->clear_flags;
    out_renderpass->clear_colour = config->clear_colour;
    out_renderpass->render_area = config->render_area;

    // Copy over config for each target.
    for (u32 t = 0; t < out_renderpass->render_target_count; ++t) {
        render_target* target = &out_renderpass->targets[t];
        target->attachment_count = config->target.attachment_count;
        target->attachments = kallocate(sizeof(render_target_attachment) * target->attachment_count, MEMORY_TAG_ARRAY);

        // Each attachment for the target.
        for (u32 a = 0; a < target->attachment_count; ++a) {
            render_target_attachment* attachment = &target->attachments[a];
            render_target_attachment_config* attachment_config = &config->target.attachments[a];

            attachment->source = attachment_config->source;
            attachment->type = attachment_config->type;
            attachment->load_operation = attachment_config->load_operation;
            attachment->store_operation = attachment_config->store_operation;
            attachment->texture = 0;
        }
    }

    return state_ptr->backend.renderpass_create(config, out_renderpass);
}

void renderer_renderpass_destroy(renderpass* pass) {
    // Destroy its rendertargets.
    for (u32 i = 0; i < pass->render_target_count; ++i) {
        renderer_render_target_destroy(&pass->targets[i], true);
    }
    
    state_ptr->backend.renderpass_destroy(pass);
}

b8 renderer_is_multithreaded() {
    return state_ptr->backend.is_multithreaded();
}

b8 renderer_flag_enabled(renderer_config_flags flag) {
    return state_ptr->backend.flag_enabled(flag);
}

void renderer_flag_set_enabled(renderer_config_flags flag, b8 enabled) {
    state_ptr->backend.flag_set_enabled(flag, enabled);
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
