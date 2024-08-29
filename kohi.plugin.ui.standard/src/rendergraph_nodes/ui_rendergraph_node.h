#ifndef _UI_RENDERGRAPH_NODE_H_
#define _UI_RENDERGRAPH_NODE_H_

#include "defines.h"
#include "renderer/viewport.h"
#include "standard_ui_system.h"

struct rendergraph;
struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;

KAPI b8 ui_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
KAPI b8 ui_rendergraph_node_initialize(struct rendergraph_node* self);
KAPI b8 ui_rendergraph_node_load_resources(struct rendergraph_node* self);
KAPI b8 ui_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
KAPI void ui_rendergraph_node_destroy(struct rendergraph_node* self);

KAPI void ui_rendergraph_node_set_atlas(struct rendergraph_node* self, kresource_texture_map* atlas);
KAPI void ui_rendergraph_node_set_render_data(struct rendergraph_node* self, standard_ui_render_data render_data);
KAPI void ui_rendergraph_node_set_viewport_and_matrices(struct rendergraph_node* self, viewport vp, mat4 view, mat4 projection);

b8 ui_rendergraph_node_register_factory(void);

#endif
