#pragma once

#include "defines.h"
#include "renderer/renderer_types.h"

struct linear_allocator;
struct frame_data;
struct viewport;
struct camera;

b8 render_view_pick_on_registered(struct render_view* self);
void render_view_pick_on_destroy(struct render_view* self);
void render_view_pick_on_resize(struct render_view* self, u32 width, u32 height);
b8 render_view_pick_on_packet_build(const struct render_view* self, struct frame_data* p_frame_data, struct viewport* v, struct camera* c, void* data, struct render_view_packet* out_packet);
void render_view_pick_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet);
b8 render_view_pick_on_render(const struct render_view* self, const struct render_view_packet* packet, struct frame_data* p_frame_data);

void render_view_pick_get_matrices(const struct render_view* self, mat4* out_view, mat4* out_projection);
b8 render_view_pick_attachment_target_regenerate(struct render_view* self, u32 pass_index, struct render_target_attachment* attachment);
