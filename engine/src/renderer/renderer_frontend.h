#pragma once

#include "renderer_types.inl"

b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name);
void renderer_system_shutdown(void* state);

void renderer_on_resized(u16 width, u16 height);

b8 renderer_draw_frame(render_packet* packet);

// HACK: this should not be exposed outside the engine.
KAPI void renderer_set_view(mat4 view);

void renderer_create_texture(
    const char* name,
    b8 auto_release,
    i32 width,
    i32 height,
    i32 channel_count,
    const u8* pixels,
    b8 has_transparency,
    struct texture* out_texture);

void renderer_destroy_texture(struct texture* texture);