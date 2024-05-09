
#include "clear_depth_rendergraph_node.h"
#include "core/engine.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "strings/kstring.h"

typedef struct clear_depth_rendergraph_node_config {
    const char* source_name;
    f32 depth_clear_value;
    u32 stencil_clear_value;
} clear_depth_rendergraph_node_config;

typedef struct clear_depth_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    k_handle buffer_handle;
    f32 depth_clear_value;
    u32 stencil_clear_value;
} clear_depth_rendergraph_node_internal_data;

static b8 deserialize_config(const char* source_str, clear_depth_rendergraph_node_config* out_config);
static void destroy_config(clear_depth_rendergraph_node_config* config);

b8 clear_depth_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!graph || !self) {
        return false;
    }

    // This node does require the config string.
    clear_depth_rendergraph_node_config typed_config = {0};
    if (!deserialize_config(config->config_str, &typed_config)) {
        KERROR("Failed to deserialize configuration for clear_depth_rendergraph_node. Node creation failed.");
        return false;
    }

    self->internal_data = kallocate(sizeof(clear_depth_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    clear_depth_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->depth_clear_value = typed_config.depth_clear_value;
    internal_data->stencil_clear_value = typed_config.stencil_clear_value;

    self->name = string_duplicate(config->name);

    // Has one sink, for the depthbuffer.
    self->sink_count = 1;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    // Setup the depthbuffer sink.
    rendergraph_sink* depthbuffer_sink = &self->sinks[0];
    depthbuffer_sink->name = string_duplicate("depthbuffer");
    depthbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    depthbuffer_sink->bound_source = 0;
    // Save off the configured source name for later lookup and binding.
    depthbuffer_sink->configured_source_name = string_duplicate(typed_config.source_name);

    // Has one source, for the depthbuffer.
    self->source_count = 1;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the depthbuffer source.
    rendergraph_source* depthbuffer_source = &self->sources[0];
    depthbuffer_source->name = string_duplicate("depthbuffer");
    depthbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_FRAMEBUFFER;
    depthbuffer_source->value.framebuffer_handle = graph->global_depthbuffer;
    depthbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = clear_depth_rendergraph_node_initialize;
    self->destroy = clear_depth_rendergraph_node_destroy;
    self->load_resources = clear_depth_rendergraph_node_load_resources;
    self->execute = clear_depth_rendergraph_node_execute;

    return true;
}

b8 clear_depth_rendergraph_node_initialize(struct rendergraph_node* self) {
    // Nothing to initialize here, this is a no-op.
    return true;
}

b8 clear_depth_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    clear_depth_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->buffer_handle = self->sinks[0].bound_source->value.framebuffer_handle;

    return true;
}

b8 clear_depth_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    clear_depth_rendergraph_node_internal_data* internal_data = self->internal_data;

    renderer_clear_depth_set(internal_data->renderer, internal_data->depth_clear_value);
    renderer_clear_stencil_set(internal_data->renderer, internal_data->stencil_clear_value);

    return renderer_clear_depth_stencil(internal_data->renderer, internal_data->buffer_handle);
}

void clear_depth_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self) {
        if (self->name) {
            string_free(self->name);
            self->name = 0;
        }

        if (self->source_count && self->sources) {
            kfree(self->sources, sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);
            self->sources = 0;
            self->source_count = 0;
        }

        if (self->sink_count && self->sinks) {
            kfree(self->sinks, sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);
            self->sinks = 0;
            self->sink_count = 0;
        }
    }
}

b8 clear_depth_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "clear_depth";
    factory.create = clear_depth_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}

static b8 deserialize_config(const char* source_str, clear_depth_rendergraph_node_config* out_config) {
    if (!source_str || !string_length(source_str) || !out_config) {
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(source_str, &tree)) {
        KERROR("Failed to parse config for clear_depth_rendergraph_node.");
        return false;
    }

    b8 result = kson_object_property_value_get_string(&tree.root, "source_name", &out_config->source_name);
    if (!result) {
        KERROR("Failed to read required config property 'source_name' from config. Deserialization failed.");
    }

    result = kson_object_property_value_get_float(&tree.root, "depth_clear_value", &out_config->depth_clear_value);
    if (!result) {
        out_config->depth_clear_value = 1.0f;
    }

    i64 stencil_clear_value;
    result = kson_object_property_value_get_int(&tree.root, "depth_clear_value", &stencil_clear_value);
    out_config->stencil_clear_value = result ? stencil_clear_value : 0;

    kson_tree_cleanup(&tree);

    return result;
}

static void destroy_config(clear_depth_rendergraph_node_config* config) {
    if (config) {
        if (config->source_name) {
            string_free(config->source_name);
            config->source_name = 0;
        }
    }
}
