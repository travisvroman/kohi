#pragma once

#include "defines.h"
#include "renderer/renderer_types.inl"

b8 render_view_skybox_on_create(struct render_view* self);
void render_view_skybox_on_destroy(struct render_view* self);
void render_view_skybox_on_resize(struct render_view* self, u32 width, u32 height);
b8 render_view_skybox_on_build_packet(const struct render_view* self, void* data, struct render_view_packet* out_packet);
void render_view_skybox_on_destroy_packet(const struct render_view* self, struct render_view_packet* packet);
b8 render_view_skybox_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index);
