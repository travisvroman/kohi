#ifndef _RENDERGRAPH_H_
#define _RENDERGRAPH_H_

#include "core/frame_data.h"
#include "defines.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"

struct application;

typedef enum rendergraph_source_type {
    RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR,
    RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL
} rendergraph_source_type;

typedef enum rendergraph_source_origin {
    RENDERGRAPH_SOURCE_ORIGIN_GLOBAL,
    RENDERGRAPH_SOURCE_ORIGIN_OTHER,
    RENDERGRAPH_SOURCE_ORIGIN_SELF
} rendergraph_source_origin;

typedef struct rendergraph_source {
    u32 id;
    char* name;
    rendergraph_source_type type;
    rendergraph_source_origin origin;
    // Array of texture pointers.
    texture** textures;
} rendergraph_source;

typedef struct rendergraph_sink {
    u32 id;
    char* name;
    rendergraph_source* bound_source;
} rendergraph_sink;

typedef struct rendergraph_pass_data {
    struct viewport* vp;
    mat4 view_matrix;
    mat4 projection_matrix;
    vec3 view_position;  // TODO: might not need this?
    void* ext_data;
} rendergraph_pass_data;

typedef struct rendergraph_pass {
    u32 id;
    char* name;

    rendergraph_pass_data pass_data;

    // darray
    rendergraph_source* sources;
    // darray
    rendergraph_sink* sinks;

    renderpass pass;
    void* internal_data;

    b8 (*initialize)(struct rendergraph_pass* self);
    b8 (*execute)(struct rendergraph_pass* self, struct frame_data* p_frame_data);
    void (*destroy)(struct rendergraph_pass* self);
} rendergraph_pass;

typedef struct rendergraph {
    const char* name;
    struct application* app;

    // darray
    rendergraph_source* global_sources;

    // darray of pointers to passes.
    rendergraph_pass** passes;

    rendergraph_sink backbuffer_global_sink;
} rendergraph;

KAPI b8 rendergraph_create(const char* name, struct application* app, rendergraph* out_graph);
KAPI void rendergraph_destroy(rendergraph* graph);

KAPI b8 rendergraph_global_source_add(rendergraph* graph, const char* name, rendergraph_source_type type, rendergraph_source_origin origin);

// pass functions
KAPI b8 rendergraph_pass_create(rendergraph* graph, const char* name, b8(create_pfn)(struct rendergraph_pass* self), rendergraph_pass* out_pass);
KAPI b8 rendergraph_pass_source_add(rendergraph* graph, const char* pass_name, const char* source_name, rendergraph_source_type type, rendergraph_source_origin origin);
KAPI b8 rendergraph_pass_sink_add(rendergraph* graph, const char* pass_name, const char* sink_name);

KAPI b8 rendergraph_pass_set_sink_linkage(rendergraph* graph, const char* pass_name, const char* sink_name, const char* source_pass_name, const char* source_name);

KAPI b8 rendergraph_finalize(rendergraph* graph);

KAPI b8 rendergraph_execute_frame(rendergraph* graph, frame_data* p_frame_data);

#endif
