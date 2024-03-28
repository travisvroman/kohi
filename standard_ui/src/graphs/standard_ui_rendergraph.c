#include "standard_ui_rendergraph.h"

#include "containers/darray.h"
#include "core/logger.h"
#include "core/systems_manager.h"
#include "math/kmath.h"
#include "passes/ui_pass.h"
#include "renderer/camera.h"
#include "renderer/viewport.h"
#include "resources/scene.h"

b8 standard_ui_rendergraph_create(const standard_ui_rendergraph_config* config, standard_ui_rendergraph* out_graph) {
    if (!rendergraph_create("standard_ui_rendergraph", &out_graph->internal_graph)) {
        KERROR("Failed to create rendergraph.");
        return false;
    }

    // Add global sources.
    if (!rendergraph_global_source_add(&out_graph->internal_graph, "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL)) {
        KERROR("Failed to add global colourbuffer source.");
        return false;
    }
    if (!rendergraph_global_source_add(&out_graph->internal_graph, "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL)) {
        KERROR("Failed to add global depthbuffer source.");
        return false;
    }

    // UI pass
    RG_CHECK(rendergraph_pass_create(&out_graph->internal_graph, "ui", ui_pass_create, 0, &out_graph->ui_pass));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "ui", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "ui", "depthbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "ui", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "ui", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "ui", "colourbuffer", 0, "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "ui", "depthbuffer", 0, "depthbuffer"));

    out_graph->ui_pass.initialize = ui_pass_initialize;
    out_graph->ui_pass.execute = ui_pass_execute;
    out_graph->ui_pass.destroy = ui_pass_destroy;

    if (!rendergraph_finalize(&out_graph->internal_graph)) {
        KERROR("Failed to finalize rendergraph. See log for details.");
        return false;
    }

    return true;
}
void standard_ui_rendergraph_destroy(standard_ui_rendergraph* graph) {
    rendergraph_destroy(&graph->internal_graph);
}

b8 standard_ui_rendergraph_initialize(standard_ui_rendergraph* graph) {
    if (!rendergraph_load_resources(&graph->internal_graph)) {
        KERROR("Failed to load rendergraph resources.");
        return false;
    }

    return true;
}
b8 standard_ui_rendergraph_update(standard_ui_rendergraph* graph, struct frame_data* p_frame_data) {
    return true;
}
b8 standard_ui_rendergraph_frame_prepare(standard_ui_rendergraph* graph, struct frame_data* p_frame_data, struct camera* current_camera, struct viewport* current_viewport, struct scene* scene, u32 render_mode) {
    // UI
    {
        ui_pass_extended_data* ext_data = graph->ui_pass.pass_data.ext_data;
        graph->ui_pass.pass_data.vp = current_viewport;
        graph->ui_pass.pass_data.view_matrix = mat4_identity();
        graph->ui_pass.pass_data.projection_matrix = current_viewport->projection;
        graph->ui_pass.pass_data.do_execute = true;

        // Renderables.
        ext_data->sui_render_data.renderables = darray_create_with_allocator(standard_ui_renderable, &p_frame_data->allocator);
        void* sui_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
        if (!standard_ui_system_render(sui_state, 0, p_frame_data, &ext_data->sui_render_data)) {
            KERROR("The standard ui system failed to render.");
        }
    }
    return true;
}

b8 standard_ui_rendergraph_execute(standard_ui_rendergraph* graph, struct frame_data* p_frame_data) {
    return rendergraph_execute_frame(&graph->internal_graph, p_frame_data);
}

b8 standard_ui_rendergraph_on_resize(standard_ui_rendergraph* graph, u32 width, u32 height) {
    return rendergraph_on_resize(&graph->internal_graph, width, height);
}
