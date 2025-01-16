#ifndef _RENDERGRAPH_H_
#define _RENDERGRAPH_H_

#include "defines.h"
#include "renderer/renderer_types.h"

#define RG_CHECK(expr)                             \
    if (!expr) {                                   \
        KERROR("Failed to execute: '%s'.", #expr); \
        return false;                              \
    }

struct kresource_texture;
struct rendergraph_system_state;

/**
 * @brief Represents a resource type to be used with rendergraph
 * sources and sinks.
 */
typedef enum rendergraph_resource_type {
    RENDERGRAPH_RESOURCE_TYPE_UNDEFINED,
    RENDERGRAPH_RESOURCE_TYPE_TEXTURE,
    RENDERGRAPH_RESOURCE_TYPE_NUMBER,
    RENDERGRAPH_RESOURCE_TYPE_MAX
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
        struct kresource_texture* t;

        /** @brief A copy of the underlying unsigned int resource. */
        u64 u64;
    } value;
} rendergraph_source;

/**
 * @brief Represents a sortof "socket" which accepts data of a specfic
 * type (i.e. a texture or number), which is provided by a source.
 */
typedef struct rendergraph_sink {
    const char* name;
    const char* configured_source_name;
    /** @brief The type of data expected in this sink. Bound source type must match. */
    rendergraph_resource_type type;
    /** @brief A pointer to the bound source. */
    rendergraph_source* bound_source;
} rendergraph_sink;

struct rendergraph;

/**
 * @brief Represents a single node in a rendergraph. A node is
 * responsible for acquiring and maintaining its required resources,
 * and generally has some sort of input (typically sinks) and some form of
 * output (typically sources) to potentially other nodes.
 */
typedef struct rendergraph_node {
    u32 index;
    /** @brief The name of the node. */
    const char* name;

    // The graph owning this node.
    struct rendergraph* graph;

    u32 source_count;
    rendergraph_source* sources;

    u32 sink_count;
    rendergraph_sink* sinks;

    void* internal_data;

    b8 (*initialize)(struct rendergraph_node* self);
    b8 (*load_resources)(struct rendergraph_node* self);
    b8 (*execute)(struct rendergraph_node* self, struct frame_data* p_frame_data);
    void (*destroy)(struct rendergraph_node* self);
} rendergraph_node;

// Opaque type representing internal dependency graph.
struct rg_dep_graph;

typedef struct rendergraph {
    char* name;

    // A pointer to the global colourbuffer framebuffer.
    struct kresource_texture* global_colourbuffer;
    // A pointer to the global depthbuffer framebuffer.
    struct kresource_texture* global_depthbuffer;

    u32 node_count;
    // Array of nodes in this graph.
    rendergraph_node* nodes;

    rendergraph_node* begin_node;
    rendergraph_node* end_node;

    u32* execution_list;

    struct rg_dep_graph* dep_graph;
} rendergraph;

/**
 * Configuration structure for a node sink
 */
typedef struct rendergraph_node_sink_config {
    const char* name;
    const char* type;
    const char* source_name;
} rendergraph_node_sink_config;

/**
 * @brief The configuration for a rendergraph node.
 */
typedef struct rendergraph_node_config {
    /** @brief The name of the node. */
    const char* name;
    /** @brief The type of the node. */
    const char* type;

    /** @brief The number of sinks in this node. */
    u32 sink_count;
    /** @brief A collection of sink configs. Must be a config for each sink in the node. Names must match. */
    rendergraph_node_sink_config* sinks;

    /** @brief Additional node-specific config in string format. The node should know how to parse this. Optional. */
    const char* config_str;
} rendergraph_node_config;

typedef struct rendergraph_node_factory {
    const char* type;
    b8 (*create)(rendergraph* graph, rendergraph_node* node, const struct rendergraph_node_config* config);
} rendergraph_node_factory;

KAPI b8 rendergraph_create(const char* config_str, struct kresource_texture* global_colourbuffer, struct kresource_texture* global_depthbuffer, rendergraph* out_graph);
KAPI void rendergraph_destroy(rendergraph* graph);

KAPI b8 rendergraph_finalize(rendergraph* graph);

KAPI b8 rendergraph_initialize(rendergraph* graph);
KAPI b8 rendergraph_load_resources(rendergraph* graph);

KAPI b8 rendergraph_execute_frame(rendergraph* graph, struct frame_data* p_frame_data);

KAPI rendergraph_resource_type string_to_resource_type(const char* str);

/**
 * Initializes the rendergraph system. Should be called twice, once with to obtain the memory
 * requirement (pass 0 to state) and a second time with allocated memory for state.
 *
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A pointer to the system state.
 * @returns True on success; otherwise false.
 */
b8 rendergraph_system_initialize(u64* memory_requirement, struct rendergraph_system_state* state);

/**
 * @brief Shuts this system down.
 *
 * @param state A pointer to the system state.
 */
void rendergraph_system_shutdown(struct rendergraph_system_state* state);

/**
 * @brief Registers the provided factory with the rendergraph system. Note that passing a factory with a
 * duplicate/already existing type will update/overwrite the existing factory of that type.
 *
 * @param state A pointer to the rendergraph system state.
 * @param new_factory A constant pointer to the factory to be registered. Note that a copy of this is taken.
 * @returns True on success; otherwise false.
 */
KAPI b8 rendergraph_system_node_factory_register(struct rendergraph_system_state* state, const rendergraph_node_factory* new_factory);

#endif
