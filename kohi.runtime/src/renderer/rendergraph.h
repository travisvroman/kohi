#ifndef _RENDERGRAPH_H_
#define _RENDERGRAPH_H_

#include "defines.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"

#define RG_CHECK(expr)                             \
    if (!expr) {                                   \
        KERROR("Failed to execute: '%s'.", #expr); \
        return false;                              \
    }

struct texture;

/**
 * @brief Represents a resource type to be used with rendergraph
 * sources and sinks.
 */
typedef enum rendergraph_resource_type {
    RENDERGRAPH_RESOURCE_TYPE_UNDEFINED,
    RENDERGRAPH_RESOURCE_TYPE_TEXTURE,
    RENDERGRAPH_RESOURCE_TYPE_NUMBER
} rendergraph_resource_type;

/**
 * @brief Represents some source of data/resource, to be plugged
 * into a sink.
 */
typedef struct rendergraph_source {
    char* name;
    /** @brief Indicates if this source has been bound. */
    b8 is_bound;
    /** @brief The type of resource held in this source. */
    rendergraph_resource_type type;
    /** @brief The resource value. */
    union {
        /** @brief A pointer to the underlying texture resource. */
        struct texture* t;
        /** @brief A copy of the underlying unsigned int resource. */
        u64 u64;
    } value;
} rendergraph_source;

/**
 * @brief Represents a sortof "socket" which accepts data of a specfic
 * type (i.e. a texture or number), which is provided by a source.
 */
typedef struct rendergraph_sink {
    char* name;
    /** @brief The type of data expected in this sink. Bound source type must match. */
    rendergraph_resource_type type;
    /** @brief A pointer to the bound source. */
    rendergraph_source* bound_source;
} rendergraph_sink;

typedef struct rendergraph_pass_sink_config {
    const char* name;
    const char* type;
    const char* source_name;
} rendergraph_pass_sink_config;

/**
 * @brief The configuration for a rendergraph pass.
 */
typedef struct rendergraph_node_config {
    /** @brief The name of the node. */
    const char* name;
    /** @brief The type of the node. */
    const char* type;

    /** @brief darray A collection of sink configs. Must be a config for each sink in the node. Names must match. */
    rendergraph_pass_sink_config* sinks;
    /** @brief Additional node-specific config in string format. The node should know how to parse this. Optional. */
    const char* config_str;
} rendergraph_pass_config;

/**
 * @brief Represents a single node in a rendergraph. A node is
 * responsible for acquiring and maintaining its required resources,
 * and generally has some sort of input (typically sinks) and some form of
 * output (typically sources) to potentially other nodes.
 */
typedef struct rendergraph_node {
    /** @brief The name of the node. */
    const char* name;
    /** @brief A copy of the node config. */
    rendergraph_pass_config* config;

    // darray
    rendergraph_source* sources;
    // darray
    rendergraph_sink* sinks;

    void* internal_data;

    /**
     * @brief Indicates if the colour attachment will be used for presentation at the completion of this node's execution.
     * NOTE: This should only ever be set by the owning rendergraph during linked resource resolution time.
     */
    b8 presents_colour;

    b8 (*initialize)(struct rendergraph_node* self);
    b8 (*load_resources)(struct rendergraph_node* self);
    b8 (*execute)(struct rendergraph_node* self, struct frame_data* p_frame_data);
    void (*destroy)(struct rendergraph_node* self);
} rendergraph_node;

typedef struct rendergraph {
    char* name;

    // Pointer to a global colour buffer.
    texture* global_colourbuffer;
    // Pointer to a global depth buffer.
    texture* global_depthbuffer;

    // darray
    rendergraph_source* global_sources;

    // darray of pointers to nodes.
    // FIXME: Rendergraph should probably own the nodes, not the application
    rendergraph_node** nodes;

    // This is what is fed to the presentation engine once the graph is complete.
    rendergraph_sink colourbuffer_global_sink;
} rendergraph;

KAPI b8 rendergraph_create(const char* name, texture* global_colourbuffer, texture* global_depthbuffer, rendergraph* out_graph);
KAPI void rendergraph_destroy(rendergraph* graph);

KAPI b8 rendergraph_finalize(rendergraph* graph);

KAPI b8 rendergraph_load_resources(rendergraph* graph);

KAPI b8 rendergraph_execute_frame(rendergraph* graph, struct frame_data* p_frame_data);

#endif
