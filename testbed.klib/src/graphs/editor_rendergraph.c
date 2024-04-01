#include "editor_rendergraph.h"

#include "containers/darray.h"
#include "core/logger.h"
#include "editor/editor_gizmo.h"
#include "math/kmath.h"
#include "passes/editor_pass.h"
#include "renderer/camera.h"
#include "renderer/viewport.h"
#include "resources/scene.h"
#include "systems/xform_system.h"

b8 editor_rendergraph_create(const editor_rendergraph_config* config, editor_rendergraph* out_graph) {
    if (!rendergraph_create("editor_rendergraph", &out_graph->internal_graph)) {
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

    // Editor pass
    RG_CHECK(rendergraph_pass_create(&out_graph->internal_graph, "editor", editor_pass_create, 0, &out_graph->editor_pass));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "editor", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "editor", "depthbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "editor", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "editor", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "editor", "colourbuffer", 0, "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "editor", "depthbuffer", 0, "depthbuffer"));

    editor_rendergraph_refresh_pfns(out_graph);

    if (!rendergraph_finalize(&out_graph->internal_graph)) {
        KERROR("Failed to finalize rendergraph. See log for details.");
        return false;
    }

    return true;
}
void editor_rendergraph_destroy(editor_rendergraph* graph) {
    rendergraph_destroy(&graph->internal_graph);
}

b8 editor_rendergraph_initialize(editor_rendergraph* graph) {
    if (!rendergraph_load_resources(&graph->internal_graph)) {
        KERROR("Failed to load rendergraph resources.");
        return false;
    }

    return true;
}
b8 editor_rendergraph_update(editor_rendergraph* graph, struct frame_data* p_frame_data) {
    return true;
}
b8 editor_rendergraph_frame_prepare(editor_rendergraph* graph, struct frame_data* p_frame_data, struct camera* current_camera, struct viewport* current_viewport, struct scene* scene, u32 render_mode) {
    if (scene->state == SCENE_STATE_LOADED) {
        if (graph->gizmo) {
            editor_gizmo_render_frame_prepare(graph->gizmo, p_frame_data);
        }

        // Editor pass
        {
            // Enable this pass for this frame.
            graph->editor_pass.pass_data.do_execute = true;
            graph->editor_pass.pass_data.vp = current_viewport;
            graph->editor_pass.pass_data.view_matrix = camera_view_get(current_camera);
            graph->editor_pass.pass_data.view_position = camera_position_get(current_camera);
            graph->editor_pass.pass_data.projection_matrix = current_viewport->projection;

            editor_pass_extended_data* ext_data = graph->editor_pass.pass_data.ext_data;

            geometry* g = &graph->gizmo->mode_data[graph->gizmo->mode].geo;

            // vec3 camera_pos = camera_position_get(c);
            // vec3 gizmo_pos = transform_position_get(&packet_data->gizmo->xform);
            // TODO: Should get this from the camera/viewport.
            // f32 fov = deg_to_rad(45.0f);
            // f32 dist = vec3_distance(camera_pos, gizmo_pos);

            // NOTE: Use the local transform of the gizmo since it won't ever be parented to anything.
            xform_calculate_local(graph->gizmo->xform_handle);
            mat4 model = xform_local_get(graph->gizmo->xform_handle);
            // f32 fixed_size = 0.1f;                            // TODO: Make this a configurable option for gizmo size.
            f32 scale_scalar = 1.0f;                   // ((2.0f * ktan(fov * 0.5f)) * dist) * fixed_size;
            graph->gizmo->scale_scalar = scale_scalar; // Keep a copy of this for hit detection.
            mat4 scale = mat4_scale((vec3){scale_scalar, scale_scalar, scale_scalar});
            model = mat4_mul(model, scale);

            geometry_render_data render_data = {0};
            render_data.model = model;
            render_data.material = g->material;
            render_data.vertex_count = g->vertex_count;
            render_data.vertex_buffer_offset = g->vertex_buffer_offset;
            render_data.index_count = g->index_count;
            render_data.index_buffer_offset = g->index_buffer_offset;
            render_data.unique_id = INVALID_ID;

            ext_data->debug_geometries = darray_create_with_allocator(geometry_render_data, &p_frame_data->allocator);
            darray_push(ext_data->debug_geometries, render_data);

#ifdef _DEBUG
            {
                geometry_render_data plane_normal_render_data = {0};
                plane_normal_render_data.model = xform_world_get(graph->gizmo->plane_normal_line.xform);
                geometry* g = &graph->gizmo->plane_normal_line.geo;
                plane_normal_render_data.material = 0;
                plane_normal_render_data.material = g->material;
                plane_normal_render_data.vertex_count = g->vertex_count;
                plane_normal_render_data.vertex_buffer_offset = g->vertex_buffer_offset;
                plane_normal_render_data.index_count = g->index_count;
                plane_normal_render_data.index_buffer_offset = g->index_buffer_offset;
                plane_normal_render_data.unique_id = INVALID_ID;
                darray_push(ext_data->debug_geometries, plane_normal_render_data);
            }
#endif
            ext_data->debug_geometry_count = darray_length(ext_data->debug_geometries);
        }
    } else {
        // Do not run these passes if the scene is not loaded.
        graph->editor_pass.pass_data.do_execute = false;
    }

    return true;
}

b8 editor_rendergraph_execute(editor_rendergraph* graph, struct frame_data* p_frame_data) {
    return rendergraph_execute_frame(&graph->internal_graph, p_frame_data);
}

b8 editor_rendergraph_on_resize(editor_rendergraph* graph, u32 width, u32 height) {
    return rendergraph_on_resize(&graph->internal_graph, width, height);
}

void editor_rendergraph_gizmo_set(editor_rendergraph* graph, struct editor_gizmo* gizmo) {
    graph->gizmo = gizmo;
}

void editor_rendergraph_refresh_pfns(editor_rendergraph* graph) {
    graph->editor_pass.initialize = editor_pass_initialize;
    graph->editor_pass.execute = editor_pass_execute;
    graph->editor_pass.destroy = editor_pass_destroy;
}
