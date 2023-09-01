#include "rendergraph.h"

#include "application_types.h"
#include "containers/darray.h"
#include "core/frame_data.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"

static b8 regenerate_render_targets(rendergraph* graph, rendergraph_pass* pass);

b8 rendergraph_create(const char* name, struct application* app, rendergraph* out_graph) {
    if (!out_graph) {
        return false;
    }

    out_graph->name = string_duplicate(name);
    out_graph->app = app;
    out_graph->passes = darray_create(rendergraph_pass*);
    out_graph->global_sources = darray_create(rendergraph_source);

    return true;
}

void rendergraph_destroy(rendergraph* graph) {
    if (graph) {
        graph->app = 0;

        if (graph->name) {
            string_free(graph->name);
            graph->name = 0;
        }

        if (graph->passes) {
            darray_destroy(graph->passes);
            graph->passes = 0;
        }

        if (graph->global_sources) {
            darray_destroy(graph->global_sources);
            graph->global_sources = 0;
        }
    }
}

b8 rendergraph_global_source_add(rendergraph* graph, const char* name, rendergraph_source_type type, rendergraph_source_origin origin) {
    if (!graph) {
        return false;
    }

    rendergraph_source source = {0};
    source.name = string_duplicate(name);
    source.type = type;
    source.origin = origin;
    darray_push(graph->global_sources, source);

    return true;
}

// pass functions
b8 rendergraph_pass_create(rendergraph* graph, const char* name, b8 (*create_pfn)(struct rendergraph_pass* self), rendergraph_pass* out_pass) {
    if (!graph || !out_pass) {
        return false;
    }

    // Make sure that there isn't already another pass with this name.
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        if (strings_equal(graph->passes[i]->name, name)) {
            KERROR("Unable to add pass because a pass named '%s' already exists.", name);
            return false;
        }
    }

    out_pass->name = string_duplicate(name);
    out_pass->sources = darray_create(rendergraph_source);
    out_pass->sinks = darray_create(rendergraph_sink);

    if (!create_pfn(out_pass)) {
        KERROR("Error creating rendergraph pass, See logs for details.");
        return false;
    }

    darray_push(graph->passes, out_pass);

    return true;
}

b8 rendergraph_pass_source_add(rendergraph* graph, const char* pass_name, const char* source_name, rendergraph_source_type type, rendergraph_source_origin origin) {
    if (!graph) {
        return false;
    }

    // Find the pass.
    rendergraph_pass* pass = 0;
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        if (strings_equal(graph->passes[i]->name, pass_name)) {
            pass = graph->passes[i];
            break;
        }
    }

    if (!pass) {
        KERROR("Unable to find a rendergraph pass named '%s'.", pass_name);
        return false;
    }

    // Verify that the pass doesn't already have a source of the same name.
    u32 source_count = darray_length(pass->sources);
    for (u32 i = 0; i < source_count; ++i) {
        if (strings_equal(pass->sources[i].name, source_name)) {
            KERROR("The pass '%s' already has a source named '%s'. Source not added.", pass_name, source_name);
            return false;
        }
    }

    rendergraph_source source = {0};
    source.name = string_duplicate(source_name);
    source.type = type;
    source.origin = origin;
    darray_push(pass->sources, source);

    return true;
}

b8 rendergraph_pass_sink_add(rendergraph* graph, const char* pass_name, const char* sink_name) {
    if (!graph) {
        return false;
    }

    // Find the pass.
    rendergraph_pass* pass = 0;
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        if (strings_equal(graph->passes[i]->name, pass_name)) {
            pass = graph->passes[i];
            break;
        }
    }

    if (!pass) {
        KERROR("Unable to find a rendergraph pass named '%s'.", pass_name);
        return false;
    }

    // Verify that the pass doesn't already have a sink of the same name.
    u32 sink_count = darray_length(pass->sinks);
    for (u32 i = 0; i < sink_count; ++i) {
        if (strings_equal(pass->sinks[i].name, sink_name)) {
            KERROR("The pass '%s' already has a sink named '%s'. Sink not added.", pass_name, sink_name);
            return false;
        }
    }

    rendergraph_sink sink = {0};
    sink.name = string_duplicate(sink_name);
    darray_push(pass->sinks, sink);

    return true;
}

b8 rendergraph_pass_set_sink_linkage(rendergraph* graph, const char* pass_name, const char* sink_name, const char* source_pass_name, const char* source_name) {
    if (!graph) {
        return false;
    }

    // Find the target pass.
    rendergraph_pass* pass = 0;
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        if (strings_equal(graph->passes[i]->name, pass_name)) {
            pass = graph->passes[i];
            break;
        }
    }

    if (!pass) {
        KERROR("Unable to find a rendergraph target pass named '%s'.", pass_name);
        return false;
    }

    // Find the target sink.
    rendergraph_sink* sink = 0;
    u32 sink_count = darray_length(pass->sinks);
    for (u32 i = 0; i < sink_count; ++i) {
        if (strings_equal(pass->sinks[i].name, sink_name)) {
            sink = &pass->sinks[i];
            break;
        }
    }

    if (!sink) {
        KERROR("Unable to find sink named '%s' on rendergraph target pass named '%s'.", sink_name, pass_name);
        return false;
    }

    rendergraph_source* source = 0;
    if (!source_pass_name) {
        // Global source.
        u32 source_count = darray_length(graph->global_sources);
        for (u32 i = 0; i < source_count; ++i) {
            if (strings_equal(graph->global_sources[i].name, source_name)) {
                source = &graph->global_sources[i];
                break;
            }
        }
    } else {
        // Pass source.
        rendergraph_pass* source_pass = 0;
        u32 pass_count = darray_length(graph->passes);
        for (u32 i = 0; i < pass_count; ++i) {
            if (strings_equal(graph->passes[i]->name, source_pass_name)) {
                source_pass = graph->passes[i];
                break;
            }
        }
        if (!source_pass) {
            KERROR("Unable to find source pass named '%s'.", source_pass_name);
            return false;
        }

        u32 source_count = darray_length(source_pass->sources);
        for (u32 i = 0; i < source_count; ++i) {
            if (strings_equal(source_pass->sources[i].name, source_name)) {
                source = &source_pass->sources[i];
                break;
            }
        }
    }

    if (!source) {
        KERROR("Unable to find source named '%s'.", source_name);
        return false;
    }

    // Everything needed to perform the link is present, so do the thing.
    sink->bound_source = source;

    return true;
}

b8 rendergraph_finalize(rendergraph* graph) {
    if (!graph) {
        return false;
    }

    // Get global texture references for global sources.
    u32 global_source_count = darray_length(graph->global_sources);
    for (u32 i = 0; i < global_source_count; ++i) {
        rendergraph_source* source = &graph->global_sources[i];
        if (source->origin == RENDERGRAPH_SOURCE_ORIGIN_GLOBAL) {
            u32 attachment_count = renderer_window_attachment_count_get();
            source->textures = kallocate(sizeof(texture*) * attachment_count, MEMORY_TAG_ARRAY);
            for (u32 j = 0; j < attachment_count; ++j) {
                if (source->type == RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR) {
                    source->textures[i] = renderer_window_attachment_get(i);
                } else if (source->type == RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL) {
                    source->textures[i] = renderer_depth_attachment_get(i);
                } else {
                    KERROR("Unsupported source type: 0x%x", source->type);
                    return false;
                }
            }
        }
    }

    // Verify that something is linked up to the global colour source.
    rendergraph_pass* backbuffer_first_user = 0;
    u32 pass_count = darray_length(graph->passes);
    for (u32 i = 0; i < pass_count; ++i) {
        u32 sink_count = darray_length(graph->passes[i]->sinks);
        for (u32 j = 0; j < sink_count; ++j) {
            rendergraph_source* source = graph->passes[i]->sinks[j].bound_source;
            if (source) {
                if (source->origin == RENDERGRAPH_SOURCE_ORIGIN_GLOBAL && source->type == RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR) {
                    // found it
                    backbuffer_first_user = graph->passes[i];
                    break;
                }
            }
        }
    }
    if (!backbuffer_first_user) {
        KERROR("Rendergraph configuration error: No reference to global backbuffer source exists.");
        return false;
    }

    // Traverse the entire list of all passes and verify that all sinks
    // have sources bound.
    // Then figure out what is the last render target source output and
    // link that to the global sink backbuffer_global_sink.
    for (u32 i = 0; i < pass_count; ++i) {
        rendergraph_pass* pass = &graph->passes[i];

        // Look for a "backbuffer" source for this pass, if there is one.
        u32 source_count = darray_length(pass->sources);
        for (u32 j = 0; j < source_count; ++j) {
            rendergraph_source* source = &pass->sources[j];
            if (source->type == RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR) {
                if (source->origin == RENDERGRAPH_SOURCE_ORIGIN_OTHER) {
                    // Other is a reference to the source output of another pass.
                    // Search all other pass' sinks to see if any have this source
                    // as a bound source.
                    for (u32 k = 0; k < pass_count; ++k) {
                        rendergraph_pass* ref_check_pass = &graph->passes[k];
                        u32 sink_count = darray_length(ref_check_pass->sinks);
                        b8 found = false;
                        for (u32 s = 0; s < sink_count; ++s) {
                            if (ref_check_pass->sinks[s].bound_source == source) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // End of the line. This source should be hooked into the backbuffer_global_sink.
                            graph->backbuffer_global_sink.bound_source = source;
                            pass->presents_after = true;
                            break;
                        }
                    }
                }
            }
            if (graph->backbuffer_global_sink.bound_source) {
                break;
            }
        }
        if (graph->backbuffer_global_sink.bound_source) {
            break;
        }
    }

    if (!graph->backbuffer_global_sink.bound_source) {
        KERROR("Unable to link backbuffer_global_sink to a source because no source was found.");
        return false;
    }

    // Once all linking is complete, initialize each pass.
    for (u32 i = 0; i < pass_count; ++i) {
        if (!graph->passes[i]->initialize(graph->passes[i])) {
            KERROR("Error intializing pass. Check logs for more info.");
            return false;
        }

        // Also generate render targets.
        if (!regenerate_render_targets(graph, graph->passes[i])) {
            KERROR("Failed to rengenerate render targets");
            return false;
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
        if (!graph->passes[i]->execute(graph->passes[i], p_frame_data)) {
            KERROR("Error executing pass. Check logs for additional details.");
            return false;
        }
    }

    return true;
}

static b8 regenerate_render_targets(rendergraph* graph, rendergraph_pass* pass) {
    if (!graph || !pass) {
        return false;
    }

    for (u32 i = 0; i < pass->pass.render_target_count; ++i) {
        render_target* target = &pass->pass.targets[i];

        // Destroy the old target if it exists.
        renderer_render_target_destroy(target, false);

        for (u32 a = 0; a < target->attachment_count; ++a) {
            render_target_attachment* attachment = &target->attachments[a];
            if (attachment->source == RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT) {
                if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_COLOUR) {
                    attachment->texture = renderer_window_attachment_get(i);
                } else if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_DEPTH) {
                    attachment->texture = renderer_depth_attachment_get(i);
                } else {
                    KERROR("Unsupported attachment type: 0x%x", attachment->type);
                    return false;
                }
            } else if (attachment->source == RENDER_TARGET_ATTACHMENT_SOURCE_VIEW) {
                // TODO: Self-owned attachments for rendergraph passes, e.g. call a pfn to (re)generate attachements.
                KFATAL("Self-owned attachements not yet supported.");
                return false;
            }
        }

        // Create the underlying render target.
        renderer_render_target_create(
            target->attachment_count,
            target->attachments,
            &pass->pass,
            target->attachments[0].texture->width,
            target->attachments[0].texture->height,
            &pass->pass.targets[i]);
    }

    return true;
}
