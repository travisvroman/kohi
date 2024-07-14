
#ifndef _WATER_PLANE_RENDERGRAPH_NODE_H_
#define _WATER_PLANE_RENDERGRAPH_NODE_H_

#include "defines.h"
#include "renderer/viewport.h"

struct rendergraph;
struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;

struct water_plane;

b8 water_plane_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
b8 water_plane_rendergraph_node_initialize(struct rendergraph_node* self);
b8 water_plane_rendergraph_node_load_resources(struct rendergraph_node* self);
b8 water_plane_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
void water_plane_rendergraph_node_destroy(struct rendergraph_node* self);

KAPI b8 water_plane_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v);
KAPI b8 water_plane_rendergraph_node_view_projection_set(struct rendergraph_node* self, mat4 view_matrix, vec3 view_pos, mat4 projection_matrix);
KAPI b8 water_plane_rendergraph_node_water_planes_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 count, struct water_plane** planes);

b8 water_plane_rendergraph_node_register_factory(void);

#endif
