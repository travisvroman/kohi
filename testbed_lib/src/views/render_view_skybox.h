#pragma once

#include "defines.h"
#include "renderer/renderer_types.h"

struct linear_allocator;
struct frame_data;

b8 render_view_skybox_on_registered(struct render_view* self);
void render_view_skybox_on_destroy(struct render_view* self);
void render_view_skybox_on_resize(struct render_view* self, u32 width, u32 height);
b8 render_view_skybox_on_packet_build(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet);
void render_view_skybox_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet);
b8 render_view_skybox_on_render(const struct render_view* self, const struct render_view_packet* packet, const struct frame_data* p_frame_data);
