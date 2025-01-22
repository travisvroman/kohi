#ifndef _SHADOW_RENDERGRAPH_NODE_H_
#define _SHADOW_RENDERGRAPH_NODE_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"

struct rendergraph;
struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;
struct directional_light;

typedef struct shadow_cascade_data {
    mat4 projection;
    mat4 view;
    f32 split_depth;
    i32 cascade_index;
} shadow_cascade_data;

typedef struct shadow_rendergraph_node_config {
    u16 resolution;
} shadow_rendergraph_node_config;

KAPI b8 shadow_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
KAPI b8 shadow_rendergraph_node_initialize(struct rendergraph_node* self);
KAPI b8 shadow_rendergraph_node_load_resources(struct rendergraph_node* self);
KAPI b8 shadow_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
KAPI void shadow_rendergraph_node_destroy(struct rendergraph_node* self);

KAPI b8 shadow_rendergraph_node_directional_light_set(struct rendergraph_node* self, const struct directional_light* light);
KAPI b8 shadow_rendergraph_node_cascade_data_set(struct rendergraph_node* self, shadow_cascade_data data, u8 cascade_index);
KAPI b8 shadow_rendergraph_node_static_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries);
KAPI b8 shadow_rendergraph_node_terrain_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries);

b8 shadow_rendergraph_node_register_factory(void);

#endif
