#ifndef _SKYBOX_RENDERGRAPH_NODE_H_
#define _SKYBOX_RENDERGRAPH_NODE_H_

#include "defines.h"

struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;

struct skybox;

KAPI b8 skybox_rendergraph_node_create(struct rendergraph_node* self, const struct rendergraph_node_config* config);
KAPI b8 skybox_rendergraph_node_initialize(struct rendergraph_node* self);
KAPI b8 skybox_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
KAPI void skybox_rendergraph_node_destroy(struct rendergraph_node* self);

KAPI void skybox_rendergraph_node_set_skybox(struct rendergraph_node* self, struct skybox* sb);

#endif
