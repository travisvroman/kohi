#include "renderer_frontend.h"

#include "containers/freelist.h"
#include "core/frame_data.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/kvar.h"
#include "core/logger.h"
#include "core/systems_manager.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "platform/platform.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "systems/camera_system.h"
#include "systems/material_system.h"
#include "systems/render_view_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

typedef struct renderer_system_state {
    renderer_plugin plugin;
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

    viewport* active_viewport;
} renderer_system_state;

b8 renderer_system_initialize(u64* memory_requirement, void* state, void* config) {
    renderer_system_config* typed_config = (renderer_system_config*)config;
    *memory_requirement = sizeof(renderer_system_state);
    if (state == 0) {
        return true;
    }
    renderer_system_state* state_ptr = (renderer_system_state*)state;

    // Default framebuffer size. Overridden when window is created.
    state_ptr->framebuffer_width = 1280;
    state_ptr->framebuffer_height = 720;
    state_ptr->resizing = false;
    state_ptr->frames_since_resize = 0;
    state_ptr->plugin = typed_config->plugin;

    // TODO: make this configurable.
    // renderer_backend_create(typed_config->renderer_type, &state_ptr->plugin);
    state_ptr->plugin.frame_number = 0;

    renderer_backend_config renderer_config = {};
    renderer_config.application_name = typed_config->application_name;
    // TODO: expose this to the application to configure.
    renderer_config.flags = RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT | RENDERER_CONFIG_FLAG_POWER_SAVING_BIT;

    // Create the vsync kvar
    kvar_int_create("vsync", (renderer_config.flags & RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? 1 : 0);

    // Initialize the backend.
    if (!state_ptr->plugin.initialize(&state_ptr->plugin, &renderer_config, &state_ptr->window_render_target_count)) {
        KERROR("Renderer backend failed to initialize. Shutting down.");
        return false;
    }

    return true;
}

void renderer_system_shutdown(void* state) {
    if (state) {
        renderer_system_state* typed_state = (renderer_system_state*)state;
        typed_state->plugin.shutdown(&typed_state->plugin);
    }
}

void renderer_on_resized(u16 width, u16 height) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
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

b8 renderer_frame_prepare(struct frame_data* p_frame_data) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.frame_number++;

    // Make sure the window is not currently being resized by waiting a designated
    // number of frames after the last resize operation before performing the backend updates.
    if (state_ptr->resizing) {
        state_ptr->frames_since_resize++;

        // If the required number of frames have passed since the resize, go ahead and perform the actual updates.
        if (state_ptr->frames_since_resize >= 30) {
            f32 width = state_ptr->framebuffer_width;
            f32 height = state_ptr->framebuffer_height;
            render_view_system_on_window_resize(width, height);
            state_ptr->plugin.resized(&state_ptr->plugin, width, height);

            // Notify views of the resize.
            render_view_system_on_window_resize(width, height);

            state_ptr->frames_since_resize = 0;
            state_ptr->resizing = false;
        } else {
            // Skip rendering the frame and try again next time.
            // NOTE: Simulate a frame being "drawn" at 60 FPS.
            platform_sleep(16);
            return false;
        }
    }

    b8 result = state_ptr->plugin.frame_prepare(&state_ptr->plugin, p_frame_data);

    // Update the frame data with renderer info.
    u8 attachment_index = state_ptr->plugin.window_attachment_index_get(&state_ptr->plugin);
    p_frame_data->renderer_frame_number = state_ptr->plugin.frame_number;
    p_frame_data->render_target_index = attachment_index;

    return result;
}

b8 renderer_begin(struct frame_data *p_frame_data) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.begin(&state_ptr->plugin, p_frame_data);
}

b8 renderer_end(struct frame_data *p_frame_data) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.end(&state_ptr->plugin, p_frame_data);
}

b8 renderer_present(struct frame_data* p_frame_data) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);

    // End the frame. If this fails, it is likely unrecoverable.
    b8 result = state_ptr->plugin.present(&state_ptr->plugin, p_frame_data);

    if (!result) {
        KERROR("renderer_present failed. Application shutting down...");
    }

    return result;
}

void renderer_viewport_set(vec4 rect) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.viewport_set(&state_ptr->plugin, rect);
}

void renderer_viewport_reset(void) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.viewport_reset(&state_ptr->plugin);
}

void renderer_scissor_set(vec4 rect) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.scissor_set(&state_ptr->plugin, rect);
}

void renderer_scissor_reset(void) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.scissor_reset(&state_ptr->plugin);
}

void renderer_winding_set(renderer_winding winding) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.winding_set(&state_ptr->plugin, winding);
}

void renderer_texture_create(const u8* pixels, struct texture* texture) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_create(&state_ptr->plugin, pixels, texture);
}

void renderer_texture_destroy(struct texture* texture) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_destroy(&state_ptr->plugin, texture);
}

void renderer_texture_create_writeable(texture* t) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_create_writeable(&state_ptr->plugin, t);
}

void renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_write_data(&state_ptr->plugin, t, offset, size, pixels);
}

void renderer_texture_read_data(texture* t, u32 offset, u32 size, void** out_memory) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_read_data(&state_ptr->plugin, t, offset, size, out_memory);
}

void renderer_texture_read_pixel(texture* t, u32 x, u32 y, u8** out_rgba) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_read_pixel(&state_ptr->plugin, t, x, y, out_rgba);
}

void renderer_texture_resize(texture* t, u32 new_width, u32 new_height) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_resize(&state_ptr->plugin, t, new_width, new_height);
}

b8 renderer_geometry_create(geometry* g, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices) {
    if (!g) {
        KERROR("renderer_geometry_create requires a valid pointer to geometry.");
        return false;
    }
    if (!vertex_count || !vertices) {
        KERROR("renderer_geometry_create requires vertex data, and none was supplied. vertex_count=%d, vertices=%p", vertex_count, vertices);
        return false;
    }

    g->material = 0;

    // Invalidate IDs. NOTE: Don't invalidate g->id! It should have a valid id at this point,
    // and invalidating it wreaks havoc.
    g->internal_id = INVALID_ID;
    g->generation = INVALID_ID_U16;

    // Take a copy of the vertex data.
    g->vertex_count = vertex_count;
    g->vertex_element_size = vertex_size;
    g->vertices = kallocate(vertex_size * vertex_count, MEMORY_TAG_RENDERER);
    kcopy_memory(g->vertices, vertices, vertex_size * vertex_count);

    g->index_count = index_count;
    g->index_element_size = index_size;
    g->indices = 0;
    // If supplied, take a copy of the index data.
    if (index_size && index_count) {
        g->indices = kallocate(index_size * index_count, MEMORY_TAG_RENDERER);
        kcopy_memory(g->indices, indices, index_size * index_count);
    }

    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.geometry_create(&state_ptr->plugin, g);
}

b8 renderer_geometry_upload(geometry* g) {
    if (!g) {
        KERROR("renderer_geometry_upload requires a valid pointer to geometry.");
        return false;
    }
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.geometry_upload(&state_ptr->plugin, g, 0, g->vertex_element_size * g->vertex_count, 0, g->index_element_size * g->index_count);
}

void renderer_geometry_vertex_update(geometry* g, u32 offset, u32 vertex_count, void* vertices) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.geometry_vertex_update(&state_ptr->plugin, g, offset, vertex_count, vertices);
}

void renderer_geometry_destroy(geometry* geometry) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.geometry_destroy(&state_ptr->plugin, geometry);
}

void renderer_geometry_draw(geometry_render_data* data) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.geometry_draw(&state_ptr->plugin, data);
}

b8 renderer_renderpass_begin(renderpass* pass, render_target* target) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderpass_begin(&state_ptr->plugin, pass, target);
}

b8 renderer_renderpass_end(renderpass* pass) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderpass_end(&state_ptr->plugin, pass);
}

b8 renderer_shader_create(shader* s, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_create(&state_ptr->plugin, s, config, pass, stage_count, stage_filenames, stages);
}

void renderer_shader_destroy(shader* s) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.shader_destroy(&state_ptr->plugin, s);
}

b8 renderer_shader_initialize(shader* s) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_initialize(&state_ptr->plugin, s);
}

b8 renderer_shader_use(shader* s) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_use(&state_ptr->plugin, s);
}

b8 renderer_shader_bind_globals(shader* s) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_bind_globals(&state_ptr->plugin, s);
}

b8 renderer_shader_bind_instance(shader* s, u32 instance_id) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_bind_instance(&state_ptr->plugin, s, instance_id);
}

b8 renderer_shader_apply_globals(shader* s, b8 needs_update) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_apply_globals(&state_ptr->plugin, s, needs_update);
}

b8 renderer_shader_apply_instance(shader* s, b8 needs_update) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_apply_instance(&state_ptr->plugin, s, needs_update);
}

b8 renderer_shader_instance_resources_acquire(shader* s, u32 texture_map_count, texture_map** maps, u32* out_instance_id) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_instance_resources_acquire(&state_ptr->plugin, s, texture_map_count, maps, out_instance_id);
}

b8 renderer_shader_instance_resources_release(shader* s, u32 instance_id) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_instance_resources_release(&state_ptr->plugin, s, instance_id);
}

b8 renderer_shader_uniform_set(shader* s, shader_uniform* uniform, const void* value) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.shader_uniform_set(&state_ptr->plugin, s, uniform, value);
}

b8 renderer_texture_map_resources_acquire(struct texture_map* map) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.texture_map_resources_acquire(&state_ptr->plugin, map);
}

void renderer_texture_map_resources_release(struct texture_map* map) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.texture_map_resources_release(&state_ptr->plugin, map);
}

void renderer_render_target_create(u8 attachment_count, render_target_attachment* attachments, renderpass* pass, u32 width, u32 height, render_target* out_target) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.render_target_create(&state_ptr->plugin, attachment_count, attachments, pass, width, height, out_target);
}

void renderer_render_target_destroy(render_target* target, b8 free_internal_memory) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.render_target_destroy(&state_ptr->plugin, target, free_internal_memory);

    if (free_internal_memory) {
        kzero_memory(target, sizeof(render_target));
    }
}

texture* renderer_window_attachment_get(u8 index) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.window_attachment_get(&state_ptr->plugin, index);
}

texture* renderer_depth_attachment_get(u8 index) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.depth_attachment_get(&state_ptr->plugin, index);
}

u8 renderer_window_attachment_index_get(void) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.window_attachment_index_get(&state_ptr->plugin);
}

u8 renderer_window_attachment_count_get(void) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.window_attachment_count_get(&state_ptr->plugin);
}

b8 renderer_renderpass_create(const renderpass_config* config, renderpass* out_renderpass) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
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
    out_renderpass->name = string_duplicate(config->name);

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

    return state_ptr->plugin.renderpass_create(&state_ptr->plugin, config, out_renderpass);
}

void renderer_renderpass_destroy(renderpass* pass) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    // Destroy its rendertargets.
    for (u32 i = 0; i < pass->render_target_count; ++i) {
        renderer_render_target_destroy(&pass->targets[i], true);
    }

    if (pass->name) {
        kfree(pass->name, string_length(pass->name) + 1, MEMORY_TAG_STRING);
        pass->name = 0;
    }

    state_ptr->plugin.renderpass_destroy(&state_ptr->plugin, pass);
}

b8 renderer_is_multithreaded(void) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.is_multithreaded(&state_ptr->plugin);
}

b8 renderer_flag_enabled_get(renderer_config_flags flag) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.flag_enabled_get(&state_ptr->plugin, flag);
}

void renderer_flag_enabled_set(renderer_config_flags flag, b8 enabled) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.flag_enabled_set(&state_ptr->plugin, flag, enabled);
}

b8 renderer_renderbuffer_create(const char* name, renderbuffer_type type, u64 total_size, b8 use_freelist, renderbuffer* out_buffer) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    if (!out_buffer) {
        KERROR("renderer_renderbuffer_create requires a valid pointer to hold the created buffer.");
        return false;
    }

    kzero_memory(out_buffer, sizeof(renderbuffer));

    out_buffer->type = type;
    out_buffer->total_size = total_size;
    if (name) {
        out_buffer->name = string_duplicate(name);
    } else {
        char temp_name[256] = {0};
        string_format(temp_name, "renderbuffer_%s", "unnamed");
        out_buffer->name = string_duplicate(temp_name);
    }

    // Create the freelist, if needed.
    if (use_freelist) {
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, 0, 0);
        out_buffer->freelist_block = kallocate(out_buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, out_buffer->freelist_block, &out_buffer->buffer_freelist);
    }

    // Create the internal buffer from the backend.
    if (!state_ptr->plugin.renderbuffer_internal_create(&state_ptr->plugin, out_buffer)) {
        KFATAL("Unable to create backing buffer for renderbuffer. Application cannot continue.");
        return false;
    }

    return true;
}

void renderer_renderbuffer_destroy(renderbuffer* buffer) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    if (buffer) {
        if (buffer->freelist_memory_requirement > 0) {
            freelist_destroy(&buffer->buffer_freelist);
            kfree(buffer->freelist_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
            buffer->freelist_memory_requirement = 0;
        }

        if (buffer->name) {
            u32 length = string_length(buffer->name);
            kfree(buffer->name, length + 1, MEMORY_TAG_STRING);
            buffer->name = 0;
        }

        // Free up the backend resources.
        state_ptr->plugin.renderbuffer_internal_destroy(&state_ptr->plugin, buffer);
        buffer->internal_data = 0;
    }
}

b8 renderer_renderbuffer_bind(renderbuffer* buffer, u64 offset) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    if (!buffer) {
        KERROR("renderer_renderbuffer_bind requires a valid pointer to a buffer.");
        return false;
    }

    return state_ptr->plugin.renderbuffer_bind(&state_ptr->plugin, buffer, offset);
}

b8 renderer_renderbuffer_unbind(renderbuffer* buffer) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_unbind(&state_ptr->plugin, buffer);
}

void* renderer_renderbuffer_map_memory(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_map_memory(&state_ptr->plugin, buffer, offset, size);
}

void renderer_renderbuffer_unmap_memory(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->plugin.renderbuffer_unmap_memory(&state_ptr->plugin, buffer, offset, size);
}

b8 renderer_renderbuffer_flush(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_flush(&state_ptr->plugin, buffer, offset, size);
}

b8 renderer_renderbuffer_read(renderbuffer* buffer, u64 offset, u64 size, void** out_memory) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_read(&state_ptr->plugin, buffer, offset, size, out_memory);
}

b8 renderer_renderbuffer_resize(renderbuffer* buffer, u64 new_total_size) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
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

    b8 result = state_ptr->plugin.renderbuffer_resize(&state_ptr->plugin, buffer, new_total_size);
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
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_load_range(&state_ptr->plugin, buffer, offset, size, data);
}

b8 renderer_renderbuffer_copy_range(renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_copy_range(&state_ptr->plugin, source, source_offset, dest, dest_offset, size);
}

b8 renderer_renderbuffer_draw(renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->plugin.renderbuffer_draw(&state_ptr->plugin, buffer, offset, element_count, bind_only);
}

void renderer_active_viewport_set(viewport* v) {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    state_ptr->active_viewport = v;

    // rect_2d viewport_rect = (vec4){v->rect.x, v->rect.height - v->rect.y, v->rect.width, -v->rect.height};
    rect_2d viewport_rect = (vec4){v->rect.x, v->rect.y + v->rect.height, v->rect.width, -v->rect.height};
    state_ptr->plugin.viewport_set(&state_ptr->plugin, viewport_rect);

    rect_2d scissor_rect = (vec4){v->rect.x, v->rect.y, v->rect.width, v->rect.height};
    state_ptr->plugin.scissor_set(&state_ptr->plugin, scissor_rect);
}

viewport* renderer_active_viewport_get() {
    renderer_system_state* state_ptr = (renderer_system_state*)systems_manager_get_state(K_SYSTEM_TYPE_RENDERER);
    return state_ptr->active_viewport;
}
