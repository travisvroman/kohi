#include "frame_begin_rendergraph_node.h"

#include "../rendergraph.h"
#include "core/engine.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "renderer/renderer_frontend.h"

b8 frame_begin_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!graph || !self) {
        return false;
    }

    self->name = string_duplicate(config->name);

    // No sinks
    self->sink_count = 0;
    self->sinks = 0;

    // Two sources, colourbuffer and depthbuffer.
    self->source_count = 2;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* colourbuffer_source = &self->sources[0];
    colourbuffer_source->name = string_duplicate("colourbuffer");
    colourbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    colourbuffer_source->value.t = graph->global_colourbuffer;
    colourbuffer_source->is_bound = false;

    // Setup the depthbuffer source.
    rendergraph_source* depthbuffer_source = &self->sources[1];
    depthbuffer_source->name = string_duplicate("depthbuffer");
    depthbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    depthbuffer_source->value.t = graph->global_depthbuffer;
    depthbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = frame_begin_rendergraph_node_initialize;
    self->destroy = frame_begin_rendergraph_node_destroy;
    self->load_resources = 0; // no resources to load.
    self->execute = frame_begin_rendergraph_node_execute;

    return true;
}

b8 frame_begin_rendergraph_node_initialize(struct rendergraph_node* self) {
    // Nothing to initialize here, this is a no-op.
    return true;
}

b8 frame_begin_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    // TODO: This is probably where an image layout transformation should occur,
    // instead of doing it at the renderpass level and having that worry about it.
    renderer_begin_debug_label(self->name, (vec3){0.75f, 0.75f, 0.75f});

    renderer_end_debug_label();

    return true;
}

void frame_begin_rendergraph_node_destroy(struct rendergraph_node* self) {
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

b8 frame_begin_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "frame_begin";
    factory.create = frame_begin_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
