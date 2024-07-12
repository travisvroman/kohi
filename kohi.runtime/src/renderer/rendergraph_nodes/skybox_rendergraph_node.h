#ifndef _SKYBOX_RENDERGRAPH_NODE_H_
#define _SKYBOX_RENDERGRAPH_NODE_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/viewport.h"

struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;
struct rendergraph;

struct skybox;

KAPI b8 skybox_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
KAPI b8 skybox_rendergraph_node_initialize(struct rendergraph_node* self);
KAPI b8 skybox_rendergraph_node_load_resources(struct rendergraph_node* self);
KAPI b8 skybox_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
KAPI void skybox_rendergraph_node_destroy(struct rendergraph_node* self);

KAPI void skybox_rendergraph_node_set_skybox(struct rendergraph_node* self, struct skybox* sb);
KAPI void skybox_rendergraph_node_set_viewport_and_matrices(struct rendergraph_node* self, viewport vp, mat4 view, mat4 projection);

b8 skybox_rendergraph_node_register_factory(void);

#endif
