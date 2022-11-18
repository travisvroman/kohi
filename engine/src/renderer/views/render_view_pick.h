#pragma once

#include "defines.h"
#include "renderer/renderer_types.inl"

struct linear_allocator;

b8 render_view_pick_on_create(struct render_view* self);
void render_view_pick_on_destroy(struct render_view* self);
void render_view_pick_on_resize(struct render_view* self, u32 width, u32 height);
b8 render_view_pick_on_build_packet(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet);
void render_view_pick_on_destroy_packet(const struct render_view* self, struct render_view_packet* packet);
b8 render_view_pick_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index);

void render_view_pick_get_matrices(const struct render_view* self, mat4* out_view, mat4* out_projection);
b8 render_view_pick_regenerate_attachment_target(struct render_view* self, u32 pass_index, struct render_target_attachment* attachment);
