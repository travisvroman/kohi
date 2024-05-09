#ifndef _FRAME_END_RENDERGRAPH_NODE_H_
#define _FRAME_END_RENDERGRAPH_NODE_H_

#include "defines.h"

struct rendergraph;
struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;

b8 frame_end_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
b8 frame_end_rendergraph_node_initialize(struct rendergraph_node* self);
b8 frame_end_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
void frame_end_rendergraph_node_destroy(struct rendergraph_node* self);

b8 frame_end_rendergraph_node_register_factory(void);

#endif
