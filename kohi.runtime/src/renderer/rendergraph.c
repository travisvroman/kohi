#include "rendergraph.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core/frame_data.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "strings/kstring.h"

// Known node types
#include "renderer/rendergraph_nodes/clear_colour_rendergraph_node.h"
#include "renderer/rendergraph_nodes/clear_depth_rendergraph_node.h"
#include "renderer/rendergraph_nodes/debug_rendergraph_node.h"
#include "renderer/rendergraph_nodes/forward_rendergraph_node.h"
#include "renderer/rendergraph_nodes/shadow_rendergraph_node.h"
#include "renderer/rendergraph_nodes/skybox_rendergraph_node.h"
#include "rendergraph_nodes/frame_begin_rendergraph_node.h"
#include "rendergraph_nodes/frame_end_rendergraph_node.h"

/**
 * The configuration for a rendergraph.
 */
typedef struct rendergraph_config {
    // The name of the graph.
    const char* name;

    /** @brief The number of nodes in this graph. */
    u32 node_count;
    /** @brief A collection of node configs. */
    rendergraph_node_config* node_configs;
} rendergraph_config;

typedef struct rg_dep_node {
    u32 index;

    u32 visited; // 0 = not visited, 1 = visited, 2 = fully processed

    // Linked list of internal node connections within the graph.
    struct rg_node_connection* outputs;
} rg_dep_node;

// Represents the internal connection between two rendergraph nodes.
// This is just referencing this at a global level, not at the source/sink level.
typedef struct rg_node_connection {
    struct rg_dep_node* dest;
    struct rg_node_connection* next;
} rg_node_connection;

typedef struct rg_dep_graph {
    u32 node_count;
    rg_dep_node** nodes;
} rg_dep_graph;

typedef struct rendergraph_system_state {
    // darray
    rendergraph_node_factory* registered_factories;
} rendergraph_system_state;

static rendergraph_node* rendergraph_node_get(rendergraph* graph, const char* node_name);

static b8 rendergraph_config_deserialize(const char* source_string, rendergraph_config* out_config);
static void rendergraph_config_destroy(rendergraph_config* config);

static rg_dep_graph* dep_graph_create(rendergraph* rg);
static void dep_graph_destroy(rg_dep_graph* graph);
static rg_dep_node* dep_node_create(u32 index);
static void dep_node_connection_add(rg_dep_graph* dgraph, u32 from_index, u32 to_index);

static b8 rg_dep_graph_topological_sort(rendergraph* graph);

b8 rendergraph_create(const char* config_str, struct texture* global_colourbuffer, struct texture* global_depthbuffer, rendergraph* out_graph) {
    if (!out_graph) {
        return false;
    }

    if (!config_str || !string_length(config_str)) {
        KERROR("rendergraph_create requires a valid configuration string (KSON).");
        return false;
    }

    if (!global_colourbuffer || !global_depthbuffer) {
        KERROR("rendergraph_create requires valid pointers to global colour and depthbuffers.");
        return false;
    }
    out_graph->global_colourbuffer = global_colourbuffer;
    out_graph->global_depthbuffer = global_depthbuffer;

    // Process config.
    rendergraph_config config = {0};
    if (!rendergraph_config_deserialize(config_str, &config)) {
        KERROR("Failed to deserialize rendergraph config. See logs for details.");
        return false;
    }

    // Take a copy of the name.
    out_graph->name = string_duplicate(config.name);

    rendergraph_system_state* state = engine_systems_get()->rendergraph_system;

    // Process nodes.
    out_graph->node_count = config.node_count;
    out_graph->nodes = kallocate(sizeof(rendergraph_node) * out_graph->node_count, MEMORY_TAG_ARRAY);
    out_graph->execution_list = kallocate(sizeof(u32) * out_graph->node_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < out_graph->node_count; ++i) {
        rendergraph_node_config* node_config = &config.node_configs[i];
        rendergraph_node* node = &out_graph->nodes[i];
        node->index = i;

        // Take a copy of the name.
        node->name = string_duplicate(node_config->name);

        // Search through factories for the given type. The factory is responsible for
        // setting up internal state, function pointers, sinks/sources, etc. for the node.
        b8 built = false;
        u32 factory_count = darray_length(state->registered_factories);
        for (u32 f = 0; f < factory_count; ++f) {
            rendergraph_node_factory* factory = &state->registered_factories[f];
            if (strings_equali(factory->type, node_config->type)) {
                // Handle node-specific creation.
                factory->create(out_graph, node, node_config);
                built = true;
                break;
            }
        }
        if (!built) {
            KERROR("No registered factory found for rendergraph node type '%s'.", node_config->type);
            return false;
        }

        // Check for special node types.
        if (strings_equali(node_config->type, "frame_begin")) {
            // Special "begin frame" node found. Keep track of it.
            if (out_graph->begin_node) {
                KERROR("A node of type 'frame_begin' has already been assigned to this graph. Only one may exist.");
                return false;
            }
            out_graph->begin_node = node;
        } else if (strings_equali(node_config->type, "frame_end")) {
            // Special "end frame" node found. Keep track of it.
            if (out_graph->end_node) {
                KERROR("A node of type 'frame_end' has already been assigned to this graph. Only one may exist.");
                return false;
            }
            out_graph->end_node = node;
        }
    }

    // Ensure that at least the two required nodes (frame_begin and frame_end) exist.
    if (!out_graph->begin_node) {
        KERROR("Rendergraph is missing required node of type 'frame_begin'.");
        return false;
    }
    if (!out_graph->end_node) {
        KERROR("Rendergraph is missing required node of type 'frame_end'.");
        return false;
    }

    return true;
}

void rendergraph_destroy(rendergraph* graph) {
    if (graph) {
        renderer_wait_for_idle();

        if (graph->name) {
            string_free(graph->name);
            graph->name = 0;
        }

        // Destroy all nodes
        for (u32 i = 0; i < graph->node_count; ++i) {
            rendergraph_node* node = &graph->nodes[i];
            if (node->destroy) {
                node->destroy(node);
            }
        }

        // Destroy the dependency graph.
        dep_graph_destroy(graph->dep_graph);

        kfree(graph->nodes, sizeof(graph->nodes[0]) * graph->node_count, MEMORY_TAG_ARRAY);
        kfree(graph->execution_list, sizeof(graph->execution_list[0]) * graph->node_count, MEMORY_TAG_ARRAY);

        kzero_memory(graph, sizeof(rendergraph));
    }
}

// Resolves sink/source linkage for the given node.
b8 rendergraph_node_resolve(rendergraph* graph, rendergraph_node* node) {
    if (!graph || !node) {
        return false;
    }

    // Resolve all linkages for sinks of this node.
    for (u32 i = 0; i < node->sink_count; ++i) {
        rendergraph_sink* sink = &node->sinks[i];

        char** source_name_parts = darray_create(char*);
        u32 parts_count = string_split(sink->configured_source_name, '.', &source_name_parts, true, false);
        if (parts_count != 2) {
            KERROR("node source name must contain node name and source name. Format: <node_name>.<source_name>.");
            string_cleanup_split_array(source_name_parts);
            return false;
        }

        // Find the source node.
        rendergraph_node* source_node = rendergraph_node_get(graph, source_name_parts[0]);
        if (!source_node) {
            KERROR("Unable to find source node called '%s' for sink '%s->%s'", source_name_parts[0], node->name, sink->name);
            string_cleanup_split_array(source_name_parts);
            return false;
        }

        const char* source_node_source_name = source_name_parts[1];

        // Search its sources for the source we are looking for.
        b8 found = false;
        for (u32 j = 0; j < source_node->source_count; ++j) {
            rendergraph_source* source = &source_node->sources[j];

            if (strings_equali(source->name, source_node_source_name)) {
                // found it - verify source/sink types match.
                if (sink->type != source->type) {
                    KERROR("Sink/source type mismatch. Sink: '%s.%s', source: '%s'", node->name, sink->name, sink->configured_source_name);
                    string_cleanup_split_array(source_name_parts);
                    return false;
                }

                // Bind the source to the sink.
                sink->bound_source = source;
                source->is_bound = true;

                // Notify the dependency graph of the connection.
                dep_node_connection_add(graph->dep_graph, node->index, source_node->index);
                string_cleanup_split_array(source_name_parts);
                found = true;
                break;
            }
        }
        if (!found) {
            KERROR("Failed to find source sink '%s.%s'. Expected source: '%s'", node->name, sink->name, sink->configured_source_name);
            string_cleanup_split_array(source_name_parts);
            return false;
        }
    }

    return true;
}

b8 rendergraph_finalize(rendergraph* graph) {
    if (!graph) {
        return false;
    }

    // Setup a dependency graph.
    graph->dep_graph = dep_graph_create(graph);

    // Ensure all nodes are resolved.
    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];
        if (!rendergraph_node_resolve(graph, node)) {
            KERROR("Unable to resolve references for node '%s'. See logs for details.", node->name);
            return false;
        }
    }

    // Now that all configured connections have been resolved, trace back through the nodes from
    // the last to the first of the frame, making sure the colourbuffer makes it through the entire graph.
    if (!graph->end_node) {
        KERROR("End node is missing. Cannot continue.");
        return false;
    }
    if (!graph->begin_node) {
        KERROR("Begin node is missing. Cannot continue.");
        return false;
    }

    // Perform topological sorting based on dependencies.
    if (!rg_dep_graph_topological_sort(graph)) {
        KERROR("Failed to sort rendergraph dependencies.");
    }

    return true;
}

b8 rendergraph_initialize(rendergraph* graph) {
    if (!graph) {
        return false;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];

        if (node->initialize) {
            if (!node->initialize(node)) {
                KERROR("Failed to initialize node '%s'.", node->name);
                return false;
            }
        }
    }

    return true;
}

b8 rendergraph_load_resources(rendergraph* graph) {
    if (!graph) {
        return false;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];

        if (node->load_resources) {
            if (!node->load_resources(node)) {
                KERROR("Failed to load resources for node '%s'.", node->name);
                return false;
            }
        }
    }

    return true;
}

b8 rendergraph_execute_frame(rendergraph* graph, frame_data* p_frame_data) {
    if (!graph) {
        return false;
    }

    // Execute nodes according to execution list.
    for (u32 i = 0; i < graph->node_count; ++i) {
        u32 current_index = graph->execution_list[i];
        if (!graph->nodes[current_index].execute(&graph->nodes[current_index], p_frame_data)) {
            KERROR("Error executing rendergraph node. Check logs for additional details.");
            return false;
        }
    }

    return true;
}

static rendergraph_node* rendergraph_node_get(rendergraph* graph, const char* node_name) {
    if (!graph || !node_name || !string_length(node_name)) {
        return 0;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];
        if (strings_equali(node->name, node_name)) {
            return node;
        }
    }

    return 0;
}

rendergraph_resource_type string_to_resource_type(const char* str) {
    if (!str || !string_length(str)) {
        KERROR("string_to_resource_type requires a valid string. Returning undefined.");
        return RENDERGRAPH_RESOURCE_TYPE_UNDEFINED;
    }

    const char* type_lookup[RENDERGRAPH_RESOURCE_TYPE_MAX] = {
        "undefined",
        "texture",
        "number"};

    for (u32 i = 0; i < RENDERGRAPH_RESOURCE_TYPE_MAX; ++i) {
        if (strings_equali(str, type_lookup[i])) {
            return (rendergraph_resource_type)i;
        }
    }

    KERROR("string_to_resource_type - unrecognized type '%s', returning undefined.", str);
    return RENDERGRAPH_RESOURCE_TYPE_UNDEFINED;
}

b8 rendergraph_system_initialize(u64* memory_requirement, struct rendergraph_system_state* state) {
    if (!memory_requirement) {
        KERROR("rendergraph_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }

    *memory_requirement = sizeof(rendergraph_system_state);

    if (!state) {
        return true;
    }

    state->registered_factories = darray_create(rendergraph_node_factory);
    // Register known types.
    if (!frame_begin_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'frame_begin'.");
        return false;
    }
    if (!frame_end_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'frame_end'.");
        return false;
    }

    if (!clear_colour_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'clear_colour'.");
        return false;
    }

    if (!clear_depth_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'clear_depth'.");
        return false;
    }

    if (!skybox_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'skybox'.");
        return false;
    }

    if (!forward_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'forward'.");
        return false;
    }

    if (!shadow_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'shadow'.");
        return false;
    }

    if (!debug_rendergraph_node_register_factory()) {
        KERROR("Failed to register known rendergraph factory type 'debug'.");
        return false;
    }

    return true;
}

void rendergraph_system_shutdown(struct rendergraph_system_state* state) {
    if (state) {
        if (state->registered_factories) {
            darray_destroy(state->registered_factories);
            state->registered_factories = 0;
        }
    }
}

KAPI b8 rendergraph_system_node_factory_register(struct rendergraph_system_state* state, const rendergraph_node_factory* new_factory) {
    if (!state || !new_factory) {
        KERROR("rendergraph_system_node_factory_register requires valid pointers to state and a new_factory.");
        return false;
    }

    // Make sure one with this name doesn't already exist.
    u32 factory_count = darray_length(state->registered_factories);
    for (u32 i = 0; i < factory_count; ++i) {
        rendergraph_node_factory* factory = &state->registered_factories[i];
        // NOTE: Yes, a hashtable would likely be faster, but since this only happens on startup,
        // it isn't really that big of a deal here.
        if (strings_equali(factory->type, new_factory->type)) {
            KDEBUG("A factory named '%s' already exists, so the one with this name will be overwritten. Note that names are case-insensitive.", new_factory->type);
            state->registered_factories[i] = *new_factory;
            return true;
        }
    }

    // No matches found, safe to register it.
    darray_push(state->registered_factories, *new_factory);
    return true;
}

#define RG_DESERIALIZE_FAIL() \
    result = false;           \
    goto rendergraph_deserialize_cleanup;

static b8 rendergraph_config_deserialize(const char* source_string, rendergraph_config* out_config) {
    if (!source_string || !out_config) {
        KERROR("rendergraph_config_deserialize requires valid source_string and valid pointer to out_config");
        return false;
    }

    kson_tree tree;
    b8 result = kson_tree_from_string(source_string, &tree);
    if (!result) {
        KERROR("Failed to parse kson config for rendergraph. See logs for details.");
        return false;
    }

    // graph name
    if (!kson_object_property_value_get_string(&tree.root, "name", &out_config->name)) {
        KWARN("Missing name property. Assigning defualtname.");
        out_config->name = string_duplicate("rendergraph_default");
    }

    // nodes
    kson_array nodes;
    if (kson_object_property_value_get_object(&tree.root, "nodes", &nodes)) {

        if (!kson_array_element_count_get(&nodes, &out_config->node_count)) {
            KERROR("Failed to get node count from nodes array.");
            RG_DESERIALIZE_FAIL();
        }

        if (out_config->node_count > 0) {
            out_config->node_configs = kallocate(sizeof(rendergraph_node_config) * out_config->node_count, MEMORY_TAG_ARRAY);

            // Each node config.
            for (u32 i = 0; i < out_config->node_count; ++i) {
                rendergraph_node_config* node_config = &out_config->node_configs[i];

                // Top-level node object.
                kson_object node;
                if (!kson_array_element_value_get_object(&nodes, i, &node)) {
                    KERROR("Failed to parse node at index %d", i);
                    RG_DESERIALIZE_FAIL();
                }

                // Name
                if (!kson_object_property_value_get_string(&node, "name", &node_config->name)) {
                    KERROR("Missing node name property. This property is required.");
                    RG_DESERIALIZE_FAIL();
                }

                // type
                if (!kson_object_property_value_get_string(&node, "type", &node_config->type)) {
                    KERROR("Missing node type property. This property is required.");
                    RG_DESERIALIZE_FAIL();
                }

                // config string
                kson_object config_block;
                if (kson_object_property_value_get_object(&node, "config", &config_block)) {
                    // Property is optional, so squish into a string if found for later processing.
                    kson_tree config_tree = {0};
                    config_tree.root = config_block;
                    node_config->config_str = kson_tree_to_string(&config_tree);
                    // NOTE: Don't cleanup this tree, as it's a pointer to the actual tree.
                }

                // Sink configs.
                kson_array sinks_array;
                if (kson_object_property_value_get_object(&node, "sinks", &sinks_array)) {
                    // Property is optional, so process it if found.

                    if (!kson_array_element_count_get(&sinks_array, &node_config->sink_count)) {
                        KERROR("Failed to get node count from nodes array.");
                        RG_DESERIALIZE_FAIL();
                    }

                    if (node_config->sink_count > 0) {
                        node_config->sinks = kallocate(sizeof(rendergraph_node_sink_config) * node_config->sink_count, MEMORY_TAG_ARRAY);

                        // Each sink.
                        for (u32 s = 0; s < node_config->sink_count; ++s) {
                            rendergraph_node_sink_config* sink_config = &node_config->sinks[s];

                            // Top-level sink object.
                            kson_object sink;
                            if (!kson_array_element_value_get_object(&sinks_array, s, &sink)) {
                                KERROR("Failed to parse sink at index %d for node '%s'.", s, node_config->name);
                                RG_DESERIALIZE_FAIL();
                            }

                            // name
                            if (!kson_object_property_value_get_string(&sink, "name", &sink_config->name)) {
                                KERROR("Missing required name field for sink at index %d for node '%s'.", s, node_config->name);
                                RG_DESERIALIZE_FAIL();
                            }

                            // type
                            if (kson_object_property_value_get_string(&sink, "type", &sink_config->type)) {
                                // NOTE: this property is optional, as default sinks for nodes already have type defined.
                            }

                            // source name
                            if (!kson_object_property_value_get_string(&sink, "source_name", &sink_config->source_name)) {
                                KERROR("Missing required source_name field for sink at index %d for node '%s'.", s, node_config->name);
                                RG_DESERIALIZE_FAIL();
                            }
                        }
                    }
                } // sinks
            }
        } // nodes
    } else {
        KERROR("Failed to parse nodes from kson rendergraph config.");
        RG_DESERIALIZE_FAIL();
    }

    result = true;

    // Cleanup from config
rendergraph_deserialize_cleanup:
    // Always cleanup the tree.
    kson_tree_cleanup(&tree);
    if (!result) {
        // Cleanup config if failed.
        rendergraph_config_destroy(out_config);
    }
    return result;
}

static void rendergraph_config_destroy(rendergraph_config* config) {
    if (config) {
        if (config->name) {
            string_free(config->name);
            config->name = 0;
        }

        // Nodes
        if (config->node_count && config->node_configs) {
            for (u32 i = 0; i < config->node_count; ++i) {
                rendergraph_node_config* node = &config->node_configs[i];

                if (node->name) {
                    string_free(node->name);
                    node->name = 0;
                }
                if (node->type) {
                    string_free(node->type);
                    node->type = 0;
                }
                if (node->config_str) {
                    string_free(node->config_str);
                    node->config_str = 0;
                }

                if (node->sink_count && node->sinks) {
                    for (u32 s = 0; s < node->sink_count; ++s) {
                        rendergraph_node_sink_config* sink = &node->sinks[s];

                        if (sink->name) {
                            string_free(sink->name);
                            sink->name = 0;
                        }
                        if (sink->type) {
                            string_free(sink->type);
                            sink->type = 0;
                        }
                        if (sink->source_name) {
                            string_free(sink->source_name);
                            sink->source_name = 0;
                        }
                    }
                    kfree(node->sinks, sizeof(node->sinks[0]), MEMORY_TAG_ARRAY);
                    node->sinks = 0;
                    node->sink_count = 0;
                }
            }
            kfree(config->node_configs, sizeof(config->node_configs[0]), MEMORY_TAG_ARRAY);
            config->node_configs = 0;
            config->node_count = 0;
        }
    }
}

static rg_dep_graph* dep_graph_create(rendergraph* rg) {
    rg_dep_graph* graph = kallocate(sizeof(rg_dep_graph), MEMORY_TAG_RENDERER);
    graph->node_count = rg->node_count;
    graph->nodes = kallocate(sizeof(rg_dep_node*) * rg->node_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < graph->node_count; ++i) {
        graph->nodes[i] = dep_node_create(i);
    }

    return graph;
}

static void dep_graph_destroy(rg_dep_graph* graph) {
    for (u32 i = 0; i < graph->node_count; ++i) {
        rg_node_connection* conn = graph->nodes[i]->outputs;
        while (conn) {
            rg_node_connection* next = conn->next;
            kfree(conn, sizeof(rg_node_connection), MEMORY_TAG_RENDERER);
            conn = next;
        }
        kfree(graph->nodes[i], sizeof(rg_dep_node), MEMORY_TAG_RENDERER);
    }
    kfree(graph->nodes, sizeof(rg_dep_node*) * graph->node_count, MEMORY_TAG_ARRAY);
    kfree(graph, sizeof(rg_dep_graph), MEMORY_TAG_RENDERER);
}

static rg_dep_node* dep_node_create(u32 index) {
    rg_dep_node* node = kallocate(sizeof(rg_dep_node), MEMORY_TAG_RENDERER);
    node->index = index;
    node->outputs = 0;
    node->visited = 0;
    return node;
}

static void dep_node_connection_add(rg_dep_graph* dgraph, u32 from_index, u32 to_index) {
    rg_node_connection* conn = kallocate(sizeof(rg_node_connection), MEMORY_TAG_RENDERER);
    conn->dest = dgraph->nodes[to_index];
    rg_dep_node* source = dgraph->nodes[from_index];
    conn->next = source->outputs;
    source->outputs = conn;
}

static b8 rg_dep_graph_topological_sort_recurse(rendergraph* graph, rg_dep_node* node, rg_dep_node** stack, i32* stack_index) {
    if (node->visited == 1) {
        // If the node is already on the stack, then a circular dependency must exist.
        KERROR("Circular dependency detected.");
        return false;
    }
    if (node->visited == 0) {
        node->visited = 1; // Now it is visited.
        // Recurse nodes adjacent to this one.
        rg_node_connection* conn = node->outputs;
        while (conn != 0) {
            if (!rg_dep_graph_topological_sort_recurse(graph, conn->dest, stack, stack_index)) {
                KERROR("Connection causes circular dependency: '%s->%s'", graph->nodes[node->index].name, graph->nodes[conn->dest->index].name);
                return false;
            }
            conn = conn->next;
        }

        // Mark the node as fully processed.
        node->visited = 2;
        // Push node to stack.
        stack[(*stack_index)++] = node;
    }

    return true;
}

static b8 rg_dep_graph_topological_sort(rendergraph* graph) {
    rg_dep_node** stack = (rg_dep_node**)kallocate(sizeof(rg_dep_node*) * graph->node_count, MEMORY_TAG_ARRAY);
    i32 stack_index = 0;

    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->dep_graph->nodes[i]->visited == 0) {
            if (!rg_dep_graph_topological_sort_recurse(graph, graph->dep_graph->nodes[i], stack, &stack_index)) {
                kfree(stack, sizeof(rg_dep_node*) * graph->node_count, MEMORY_TAG_ARRAY);
                // Circular dependency detected, fail.
                return false;
            }
        }
    }

    // TODO: check for orphaned nodes.

    // Save this list off - it's the execution order of the graph.
    // Always force the begin node to be first and the end node to be last.
    graph->execution_list[0] = graph->begin_node->index;
    graph->execution_list[graph->node_count - 1] = graph->end_node->index;
    u32 current_index = graph->node_count - 2; // Work backwards 1;
    while (stack_index) {
        rg_dep_node* node = stack[--stack_index];
        if (node->index == graph->begin_node->index || node->index == graph->end_node->index) {
            // Don't add begin/end nodes more than once.
            continue;
        }
        graph->execution_list[current_index] = node->index;
        current_index--;
    }
    // Get rid of the stack.
    kfree(stack, sizeof(rg_dep_node*) * graph->node_count, MEMORY_TAG_ARRAY);

    // Print out the final order of nodes to be executed.
    KDEBUG("Rengergraph will be executed in the following order:");
    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];
        KINFO("[%d]: %s", node->index, graph->nodes[node->index].name);
    }
    return true;
}
