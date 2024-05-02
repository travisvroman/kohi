#include "rendergraph.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core/frame_data.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "strings/kstring.h"

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

/** @brief Configuration of a source, typically used for global sources. */
typedef struct rendergraph_source_config {
    const char* name;
    const char* type;
} rendergraph_source_config;

/**
 * The configuration for a rendergraph.
 */
typedef struct rendergraph_config {
    // The name of the graph.
    const char* name;

    /** @brief The number of global sources configured in this graph. */
    u32 global_source_count;
    /** @brief A collection of global sources */
    rendergraph_source_config* global_sources;

    /** @brief The name of the source that outputs the final version of the colourbuffer. */
    const char* global_colourbuffer_sink_source_name;

    /** @brief The number of nodes in this graph. */
    u32 node_count;
    /** @brief A collection of node configs. */
    rendergraph_node_config* node_configs;
} rendergraph_config;

typedef struct rendergraph_system_state {
    // darray
    rendergraph_node_factory* registered_factories;
} rendergraph_system_state;

static b8 rendergraph_config_deserialize(const char* source_string, rendergraph_config* out_config);
static void rendergraph_config_destroy(rendergraph_config* config);

b8 rendergraph_create(const char* config_str, k_handle global_colourbuffer, k_handle global_depthbuffer, rendergraph* out_graph) {
    if (!out_graph) {
        return false;
    }

    if (!config_str || string_length(config_str)) {
        KERROR("rendergraph_create requires a valid configuration string (KSON).");
        return false;
    }

    if (k_handle_is_invalid(global_colourbuffer) || k_handle_is_invalid(global_depthbuffer)) {
        KERROR("rendergraph_create requires valid handles to global colour and depthbuffers.");
        return false;
    }

    // Process config.
    rendergraph_config config = {0};
    if (!rendergraph_config_deserialize(config_str, &config)) {
        KERROR("Failed to deserialize rendergraph config. See logs for details.");
        return false;
    }

    // Setup the global colourbuffer sink.
    out_graph->colourbuffer_global_sink.name = string_duplicate("colourbuffer_sink");
    out_graph->colourbuffer_global_sink.type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    out_graph->colourbuffer_global_sink.bound_source = 0;

    // Take a copy of the name.
    out_graph->name = string_duplicate(config.name);
    out_graph->global_colourbuffer_sink_source_name = string_duplicate(config.global_colourbuffer_sink_source_name);

    // Save this off for later resolution.
    const char* global_colourbuffer_sink_source_name = string_duplicate(config.global_colourbuffer_sink_source_name);

    rendergraph_system_state* state = engine_systems_get()->rendergraph_system;

    // Process nodes.
    out_graph->node_count = config.node_count;
    for (u32 i = 0; i < out_graph->node_count; ++i) {
        rendergraph_node_config* node_config = &config.node_configs[i];
        rendergraph_node* node = &out_graph->nodes[i];

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
                factory->create(node, node_config->config_str);
                built = true;
                break;
            }
        }
        if (!built) {
            KERROR("No registered factory found for rendergraph node type '%s'.", node_config->type);
            return false;
        }
    }

    // There are two automatic global sources created, the colourbuffer and depthbuffer,
    // in addition to any additional configured ones.
    out_graph->global_source_count = 2 + config.global_source_count;
    out_graph->global_sources = kallocate(sizeof(rendergraph_source) * out_graph->global_source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer and depthbuffer first.
    u32 global_source_idx = 0;
    out_graph->global_sources[global_source_idx].name = string_duplicate("colourbuffer");
    out_graph->global_sources[global_source_idx].type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    out_graph->global_sources[global_source_idx].value.framebuffer_handle = global_colourbuffer;
    out_graph->global_sources[global_source_idx].is_bound = false;
    global_source_idx++;

    out_graph->global_sources[global_source_idx].name = string_duplicate("depthbuffer");
    out_graph->global_sources[global_source_idx].type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    out_graph->global_sources[global_source_idx].value.framebuffer_handle = global_depthbuffer;
    out_graph->global_sources[global_source_idx].is_bound = false;
    global_source_idx++;

    // Additional global sources.
    for (u32 i = 0; global_source_idx < out_graph->global_source_count; ++global_source_idx, ++i) {
        rendergraph_source* source = &out_graph->global_sources[global_source_idx];
        rendergraph_source_config* source_config = &config.global_sources[i];

        // TODO: More info might be needed here depending on the source type.
        source->type = string_to_resource_type(source_config->type);
        source->name = string_duplicate(source_config->name);
    }

    return true;
}

void rendergraph_destroy(rendergraph* graph) {
    if (graph) {
        renderer_wait_for_idle()

            if (graph->name) {
            string_free(graph->name);
            graph->name = 0;
        }

        // TODO: destroy all nodes
    }
}

b8 rendergraph_node_source_get(rendergraph* graph, const char* node_source_name, rendergraph_node** out_node, rendergraph_source** out_source) {
    if (!node_source_name || !string_length(node_source_name) || !out_node || !out_source) {
        return false;
    }

    char** node_source_parts = darray_create(char*);
    u32 parts_count = string_split(graph->global_colourbuffer_sink_source_name, '.', &node_source_parts, true, false);
    if (parts_count != 2) {
        KERROR("node source name must contain node name and source name. Format: <node_name>.<source_name>");
        return false;
    }
    // Find the node.
    for (u32 i = 0; i < graph->node_count; ++i) {
        rendergraph_node* node = &graph->nodes[i];
        if (strings_equali(node->name, node_source_parts[0])) {
            // Found the node. Now find the source.
            for (u32 s = 0; s < node->source_count; ++s) {
                rendergraph_source* source = &node->sources[s];
                if (strings_equali(source->name, node_source_parts[1])) {
                    *out_node = node;
                    *out_source = source;
                    return true;
                }
            }
        }
    }
    return false;
}

b8 rendergraph_finalize(rendergraph* graph) {
    if (!graph) {
        return false;
    }

    // LEFTOFF: Resolve all links from the bottom up.
    rendergraph_node* node = 0;
    rendergraph_source* source = 0;
    if (rendergraph_node_source_get(graph, graph->global_colourbuffer_sink_source_name, &node, &source)) {
        // Found the source.
        if (graph->colourbuffer_global_sink.type != source->type) {
            KERROR("Found the source '%s', but there is a source/sink type mismatch.", graph->global_colourbuffer_sink_source_name);
            return false;
        }
        // Bind the source to the sink.
        graph->colourbuffer_global_sink.bound_source = source;
        source->is_bound = true;

        // LEFTOFF: Add to a dependency list, then traverse the rest of the nodes all the way
        // back to the global colourbuffer.
    }
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

    // Passes will be executed in the order they are added.
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        if (!graph->passes[i]->pass_data.do_execute) {
            continue;
        }
        if (!graph->passes[i]->execute(graph->passes[i], p_frame_data)) {
            KERROR("Error executing pass. Check logs for additional details.");
            return false;
        }
    }

    return true;
}

rendergraph_resource_type string_to_resource_type(const char* str) {
    if (!str || !string_length(str)) {
        KERROR("string_to_resource_type requires a valid string. Returning undefined.");
        return RENDERGRAPH_RESOURCE_TYPE_UNDEFINED;
    }

    const char* type_lookup[RENDERGRAPH_RESOURCE_TYPE_MAX] = {
        "undefined",
        "texture",
        "framebuffer",
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

    // global colourbuffer sink source name.
    if (!kson_object_property_value_get_string(&tree.root, "global_colourbuffer_sink_source_name", &out_config->global_colourbuffer_sink_source_name)) {
        KERROR("rendergraph config missing required property global_colourbuffer_sink_source_name.");
        RG_DESERIALIZE_FAIL();
    }

    // global sources.
    kson_array global_sources;
    if (kson_object_property_value_get_object(&tree.root, "global_sources", &global_sources)) {
        // NOTE: This is an optional property, so if it's not here, no biggie.

        if (!kson_array_element_count_get(&global_sources, &out_config->global_source_count)) {
            KERROR("Failed to get count from global_sources array.");
            RG_DESERIALIZE_FAIL();
        }

        if (out_config->global_source_count > 0) {
            out_config->global_sources = kallocate(sizeof(rendergraph_source_config) * out_config->global_source_count, MEMORY_TAG_ARRAY);

            // Each global source config.
            for (u32 i = 0; i < out_config->global_source_count; ++i) {
                rendergraph_source_config* source_config = &out_config->global_sources[i];

                // Top-level source object.
                kson_object source;
                if (!kson_array_element_value_get_object(&global_sources, i, &source)) {
                    KERROR("Failed to parse global source at index %d", i);
                    RG_DESERIALIZE_FAIL();
                }

                // Name
                if (!kson_object_property_value_get_string(&source, "name", &source_config->name)) {
                    KERROR("Missing global source name property. This property is required.");
                    RG_DESERIALIZE_FAIL();
                }

                // type
                if (!kson_object_property_value_get_string(&source, "type", &source_config->type)) {
                    KERROR("Missing global source type property. This property is required.");
                    RG_DESERIALIZE_FAIL();
                }
            }
        }
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

        if (config->global_colourbuffer_sink_source_name) {
            string_free(config->global_colourbuffer_sink_source_name);
            config->global_colourbuffer_sink_source_name = 0;
        }

        // Global sources.
        if (config->global_source_count && config->global_sources) {
            for (u32 i = 0; i < config->global_source_count; ++i) {
                rendergraph_source_config* source = &config->global_sources[i];

                if (source->name) {
                    string_free(source->name);
                    source->name = 0;
                }
                if (source->type) {
                    string_free(source->type);
                    source->type = 0;
                }
            }
            kfree(config->global_sources, sizeof(rendergraph_source_config) * config->global_source_count, MEMORY_TAG_ARRAY);
            config->global_sources = 0;
            config->global_source_count = 0;
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
