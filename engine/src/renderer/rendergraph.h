#ifndef _RENDERGRAPH_H_
#define _RENDERGRAPH_H_

#include "core/frame_data.h"
#include "defines.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"

#define RG_CHECK(expr)                             \
    if (!expr) {                                   \
        KERROR("Failed to execute: '%s'.", #expr); \
        return false;                              \
    }

struct texture;

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
    char* name;
    rendergraph_source_type type;
    rendergraph_source_origin origin;
    // Array of texture pointers.
    texture** textures;
} rendergraph_source;

typedef struct rendergraph_sink {
    char* name;
    rendergraph_source* bound_source;
} rendergraph_sink;

typedef struct rendergraph_pass_data {
    b8 do_execute;
    struct viewport* vp;
    mat4 view_matrix;
    mat4 projection_matrix;
    vec3 view_position;  // TODO: might not need this?
    void* ext_data;
} rendergraph_pass_data;

typedef struct rendergraph_pass {
    char* name;

    rendergraph_pass_data pass_data;

    // darray
    rendergraph_source* sources;
    // darray
    rendergraph_sink* sinks;

    renderpass pass;
    void* internal_data;

    b8 presents_after;

    b8 (*initialize)(struct rendergraph_pass* self);
    b8 (*load_resources)(struct rendergraph_pass* self);
    b8 (*execute)(struct rendergraph_pass* self, struct frame_data* p_frame_data);
    void (*destroy)(struct rendergraph_pass* self);
    b8 (*attachment_textures_regenerate)(struct rendergraph_pass* self, u16 width, u16 height);
    b8 (*source_populate)(struct rendergraph_pass* self, rendergraph_source* source);
    b8 (*attachment_populate)(struct rendergraph_pass* self, render_target_attachment* attachment);
} rendergraph_pass;

typedef struct rendergraph {
    char* name;

    // darray
    rendergraph_source* global_sources;

    // darray of pointers to passes.
    rendergraph_pass** passes;

    rendergraph_sink backbuffer_global_sink;
} rendergraph;

KAPI b8 rendergraph_create(const char* name, rendergraph* out_graph);
KAPI void rendergraph_destroy(rendergraph* graph);

KAPI b8 rendergraph_global_source_add(rendergraph* graph, const char* name, rendergraph_source_type type, rendergraph_source_origin origin);

// pass functions
KAPI b8 rendergraph_pass_create(rendergraph* graph, const char* name, b8 (*create_pfn)(struct rendergraph_pass* self, void* config), void* config, rendergraph_pass* out_pass);
KAPI b8 rendergraph_pass_source_add(rendergraph* graph, const char* pass_name, const char* source_name, rendergraph_source_type type, rendergraph_source_origin origin);
KAPI b8 rendergraph_pass_sink_add(rendergraph* graph, const char* pass_name, const char* sink_name);

KAPI b8 rendergraph_pass_set_sink_linkage(rendergraph* graph, const char* pass_name, const char* sink_name, const char* source_pass_name, const char* source_name);

KAPI b8 rendergraph_finalize(rendergraph* graph);

KAPI b8 rendergraph_load_resources(rendergraph* graph);

KAPI b8 rendergraph_execute_frame(rendergraph* graph, frame_data* p_frame_data);

KAPI b8 rendergraph_on_resize(rendergraph* graph, u16 width, u16 height);

#endif
