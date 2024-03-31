
#pragma once

#include "renderer/rendergraph.h"

struct frame_data;
struct scene;
struct viewport;
struct editor_gizmo;

typedef struct editor_rendergraph {
    rendergraph internal_graph;

    struct editor_gizmo* gizmo;

    rendergraph_pass editor_pass;

} editor_rendergraph;

typedef struct editor_rendergraph_config {
    u16 dummy;
} editor_rendergraph_config;

KAPI b8 editor_rendergraph_create(const editor_rendergraph_config* config, editor_rendergraph* out_graph);
KAPI void editor_rendergraph_destroy(editor_rendergraph* graph);

KAPI b8 editor_rendergraph_initialize(editor_rendergraph* graph);
KAPI b8 editor_rendergraph_update(editor_rendergraph* graph, struct frame_data* p_frame_data);
KAPI b8 editor_rendergraph_frame_prepare(editor_rendergraph* graph, struct frame_data* p_frame_data, struct camera* current_camera, struct viewport* current_viewport, struct scene* scene, u32 render_mode);
KAPI b8 editor_rendergraph_execute(editor_rendergraph* graph, struct frame_data* p_frame_data);
KAPI b8 editor_rendergraph_on_resize(editor_rendergraph* graph, u32 width, u32 height);

KAPI void editor_rendergraph_gizmo_set(editor_rendergraph* graph, struct editor_gizmo* gizmo);

KAPI void editor_rendergraph_refresh_pfns(editor_rendergraph* graph);
