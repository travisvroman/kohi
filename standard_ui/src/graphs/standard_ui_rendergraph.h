
#pragma once

#include "renderer/rendergraph.h"

struct frame_data;
struct scene;
struct viewport;
struct editor_gizmo;

typedef struct standard_ui_rendergraph {
    rendergraph internal_graph;

    rendergraph_pass ui_pass;

} standard_ui_rendergraph;

typedef struct standard_ui_rendergraph_config {
    u16 dummy;
} standard_ui_rendergraph_config;

KAPI b8 standard_ui_rendergraph_create(const standard_ui_rendergraph_config* config, standard_ui_rendergraph* out_graph);
KAPI void standard_ui_rendergraph_destroy(standard_ui_rendergraph* graph);

KAPI b8 standard_ui_rendergraph_initialize(standard_ui_rendergraph* graph);
KAPI b8 standard_ui_rendergraph_update(standard_ui_rendergraph* graph, struct frame_data* p_frame_data);
KAPI b8 standard_ui_rendergraph_frame_prepare(standard_ui_rendergraph* graph, struct frame_data* p_frame_data, struct camera* current_camera, struct viewport* current_viewport, struct scene* scene, u32 render_mode);
KAPI b8 standard_ui_rendergraph_execute(standard_ui_rendergraph* graph, struct frame_data* p_frame_data);
KAPI b8 standard_ui_rendergraph_on_resize(standard_ui_rendergraph* graph, u32 width, u32 height);
