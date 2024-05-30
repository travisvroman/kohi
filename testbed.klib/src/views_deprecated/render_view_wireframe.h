/*
#ifndef _TESTBED_RENDER_VIEW_WIREFRAME_H_
#define _TESTBED_RENDER_VIEW_WIREFRAME_H_

#include "defines.h"
#include "renderer/renderer_types.h"

struct frame_data;
struct viewport;
struct camera;

typedef struct render_view_wireframe_data {
    // darray
    geometry_render_data* world_geometries;
    // darray
    geometry_render_data* terrain_geometries;
    // darray
    geometry_render_data* debug_geometries;

    u32 selected_id;
} render_view_wireframe_data;

b8 render_view_wireframe_on_registered(struct render_view* self);
void render_view_wireframe_on_destroy(struct render_view* self);
void render_view_wireframe_on_resize(struct render_view* self, u32 width, u32 height);
b8 render_view_wireframe_on_packet_build(const struct render_view* self, struct frame_data* p_frame_data, struct viewport* v, struct camera* c, void* data, struct render_view_packet* out_packet);
void render_view_wireframe_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet);
b8 render_view_wireframe_on_render(const struct render_view* self, const struct render_view_packet* packet, struct frame_data* p_frame_data);

#endif
*/
