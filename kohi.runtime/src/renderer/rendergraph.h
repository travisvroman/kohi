#ifndef _RENDERGRAPH_H_
#define _RENDERGRAPH_H_

#include "defines.h"
#include "identifiers/khandle.h"
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
    RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER,
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
        struct texture* t;

        /** @brief A handle to a framebuffer resource. */
        k_handle framebuffer_handle;

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

/**
 * @brief Represents a single node in a rendergraph. A node is
 * responsible for acquiring and maintaining its required resources,
 * and generally has some sort of input (typically sinks) and some form of
 * output (typically sources) to potentially other nodes.
 */
typedef struct rendergraph_node {
    /** @brief The name of the node. */
    const char* name;

    u32 source_count;
    rendergraph_source* sources;

    u32 sink_count;
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

    // Handle to a global colourbuffer framebuffer.
    k_handle global_colourbuffer;
    // Handle to a global depthbuffer framebuffer.
    k_handle global_depthbuffer;

    u32 global_source_count;
    rendergraph_source* global_sources;

    u32 node_count;
    // Array of nodes in this graph.
    rendergraph_node* nodes;

    // This is what is fed to the presentation engine once the graph is complete.
    rendergraph_sink colourbuffer_global_sink;

    /** @brief The name of the source that outputs the final version of the colourbuffer. */
    const char* global_colourbuffer_sink_source_name;
} rendergraph;

struct rendergraph_system_state;

typedef struct rendergraph_node_factory {
    const char* type;
    b8 (*create)(rendergraph_node* node, const char* config_str);
} rendergraph_node_factory;

KAPI b8 rendergraph_create(const char* config_str, k_handle global_colourbuffer, k_handle global_depthbuffer, rendergraph* out_graph);
KAPI void rendergraph_destroy(rendergraph* graph);

KAPI b8 rendergraph_finalize(rendergraph* graph);

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
