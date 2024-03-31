#include "forward_rendergraph.h"

#include "containers/darray.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "renderer/camera.h"
#include "renderer/renderer_types.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "resources/scene.h"
#include "resources/skybox.h"
#include "systems/light_system.h"

// Supported passes.
#include "renderer/passes/scene_pass.h"
#include "renderer/passes/shadow_map_pass.h"
#include "renderer/passes/skybox_pass.h"
#include "systems/texture_system.h"

b8 forward_rendergraph_create(const forward_rendergraph_config* config, forward_rendergraph* out_graph) {
    if (!rendergraph_create("forward_rendergraph", &out_graph->internal_graph)) {
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

    // Skybox pass
    RG_CHECK(rendergraph_pass_create(&out_graph->internal_graph, "skybox", skybox_pass_create, 0, &out_graph->skybox_pass));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "skybox", "colourbuffer"));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "skybox", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "skybox", "colourbuffer", 0, "colourbuffer"));

    // Shadowmap pass
    const char* shadowmap_pass_name = "shadowmap_pass";
    shadow_map_pass_config shadow_pass_config = {0};
    shadow_pass_config.resolution = 2048;
    RG_CHECK(rendergraph_pass_create(&out_graph->internal_graph, shadowmap_pass_name, shadow_map_pass_create, &shadow_pass_config, &out_graph->shadowmap_pass));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, shadowmap_pass_name, "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_SELF));

    // Scene pass
    RG_CHECK(rendergraph_pass_create(&out_graph->internal_graph, "scene", scene_pass_create, 0, &out_graph->scene_pass));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "scene", "colourbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "scene", "depthbuffer"));
    RG_CHECK(rendergraph_pass_sink_add(&out_graph->internal_graph, "scene", "shadowmap"));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "scene", "colourbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_COLOUR, RENDERGRAPH_SOURCE_ORIGIN_OTHER));
    RG_CHECK(rendergraph_pass_source_add(&out_graph->internal_graph, "scene", "depthbuffer", RENDERGRAPH_SOURCE_TYPE_RENDER_TARGET_DEPTH_STENCIL, RENDERGRAPH_SOURCE_ORIGIN_GLOBAL));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "scene", "colourbuffer", "skybox", "colourbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "scene", "depthbuffer", 0, "depthbuffer"));
    RG_CHECK(rendergraph_pass_set_sink_linkage(&out_graph->internal_graph, "scene", "shadowmap", "shadowmap_pass", "depthbuffer"));

    // "refresh" pfns
    out_graph->skybox_pass.initialize = skybox_pass_initialize;
    out_graph->skybox_pass.execute = skybox_pass_execute;
    out_graph->skybox_pass.destroy = skybox_pass_destroy;

    out_graph->shadowmap_pass.initialize = shadow_map_pass_initialize;
    out_graph->shadowmap_pass.execute = shadow_map_pass_execute;
    out_graph->shadowmap_pass.destroy = shadow_map_pass_destroy;
    out_graph->shadowmap_pass.load_resources = shadow_map_pass_load_resources;
    /* graph->shadowmap_pass.source_populate = shadow_map_pass_source_populate; */

    out_graph->scene_pass.initialize = scene_pass_initialize;
    out_graph->scene_pass.execute = scene_pass_execute;
    out_graph->scene_pass.destroy = scene_pass_destroy;
    out_graph->scene_pass.load_resources = scene_pass_load_resources;

    if (!rendergraph_finalize(&out_graph->internal_graph)) {
        KERROR("Failed to finalize rendergraph. See log for details.");
        return false;
    }

    return true;
}
void forward_rendergraph_destroy(forward_rendergraph* graph) {
    rendergraph_destroy(&graph->internal_graph);
}

b8 forward_rendergraph_initialize(forward_rendergraph* graph) {
    if (!rendergraph_load_resources(&graph->internal_graph)) {
        KERROR("Failed to load rendergraph resources.");
        return false;
    }

    return true;
}

b8 forward_rendergraph_update(forward_rendergraph* graph, struct frame_data* p_frame_data) {
    return true;
}

b8 forward_rendergraph_frame_prepare(forward_rendergraph* graph, struct frame_data* p_frame_data, struct camera* current_camera, struct viewport* current_viewport, struct scene* scene, u32 render_mode) {
    // Skybox pass. NOTE: This pass must always run, as it is what clears the screen.
    skybox_pass_extended_data* skybox_pass_ext_data = graph->skybox_pass.pass_data.ext_data;
    graph->skybox_pass.pass_data.vp = current_viewport;
    graph->skybox_pass.pass_data.view_matrix = camera_view_get(current_camera);
    graph->skybox_pass.pass_data.view_position = camera_position_get(current_camera);
    graph->skybox_pass.pass_data.projection_matrix = current_viewport->projection;
    graph->skybox_pass.pass_data.do_execute = true;
    skybox_pass_ext_data->sb = 0;

    // Tell our scene to generate relevant packet data. NOTE: Generates skybox and world packets.
    if (scene->state == SCENE_STATE_LOADED) {
        scene_render_frame_prepare(scene, p_frame_data);

        // HACK: Using the first light in the collection for now.
        // TODO: Support for multiple directional lights with priority sorting.
        directional_light* dir_light = scene->dir_lights ? &scene->dir_lights[0] : 0;

        // Global setup
        f32 near = current_viewport->near_clip;
        f32 far = dir_light ? dir_light->data.shadow_distance + dir_light->data.shadow_fade_distance : 0;
        f32 clip_range = far - near;

        f32 min_z = near;
        f32 max_z = near + clip_range;
        f32 range = max_z - min_z;
        f32 ratio = max_z / min_z;

        f32 cascade_split_multiplier = dir_light ? dir_light->data.shadow_split_mult : 0.95f;

        // Calculate splits based on view camera frustum.
        vec4 splits;
        for (u32 c = 0; c < MAX_CASCADE_COUNT; c++) {
            f32 p = (c + 1) / (f32)MAX_CASCADE_COUNT;
            f32 log = min_z * kpow(ratio, p);
            f32 uniform = min_z + range * p;
            f32 d = cascade_split_multiplier * (log - uniform) + uniform;
            splits.elements[c] = (d - near) / clip_range;
        }

        // Default values to use in the event there is no directional light.
        // These are required because the scene pass needs them.
        mat4 shadow_camera_lookats[MAX_CASCADE_COUNT];
        mat4 shadow_camera_projections[MAX_CASCADE_COUNT];
        vec3 shadow_camera_positions[MAX_CASCADE_COUNT];
        for (u32 i = 0; i < MAX_CASCADE_COUNT; ++i) {
            shadow_camera_lookats[i] = mat4_identity();
            shadow_camera_projections[i] = mat4_identity();
            shadow_camera_positions[i] = vec3_zero();
        }

        // Skybox pass
        {
            // HACK: Just use the first one for now.
            // TODO: Support for multiple skyboxes, possibly transition between them.
            if (scene->skyboxes) {
                u32 skybox_count = darray_length(scene->skyboxes);
                skybox_pass_ext_data->sb = skybox_count ? &scene->skyboxes[0] : 0;
            }
        }

        // Shadowmap pass - only runs if there is a directional light.
        if (dir_light) {
            f32 last_split_dist = 0.0f;
            rendergraph_pass* pass = &graph->shadowmap_pass;
            // Mark this pass as executable.
            pass->pass_data.do_execute = true;

            // Obtain the light direction.
            vec3 light_dir = vec3_normalized(vec3_from_vec4(dir_light->data.direction));

            // Setup the extended data for the pass.
            shadow_map_pass_extended_data* ext_data = pass->pass_data.ext_data;
            ext_data->light = dir_light;

            /* frustum culling_frustum; */
            vec3 culling_center;
            f32 culling_radius;

            // Get the view-projection matrix
            mat4 shadow_dist_projection = mat4_perspective(
                current_viewport->fov,
                current_viewport->rect.width / current_viewport->rect.height,
                near,
                far);
            mat4 cam_view_proj = mat4_transposed(mat4_mul(camera_view_get(current_camera), shadow_dist_projection));

            for (u32 c = 0; c < MAX_CASCADE_COUNT; c++) {
                shadow_map_cascade_data* cascade = &ext_data->cascades[c];
                cascade->cascade_index = c;

                // NOTE: Each pass for cascades will need to do the following process.
                // The only real difference will be that the near/far clips will be adjusted for each.

                // Get the world-space corners of the view frustum.
                vec4 corners[8] = {0};
                frustum_corner_points_world_space(cam_view_proj, corners);

                // Adjust the corners by pulling/pushing the near/far according to the current split.
                f32 split_dist = splits.elements[c];
                for (u32 i = 0; i < 4; ++i) {
                    // far - near
                    vec4 dist = vec4_sub(corners[i + 4], corners[i]);
                    corners[i + 4] = vec4_add(corners[i], vec4_mul_scalar(dist, split_dist));
                    corners[i] = vec4_add(corners[i], vec4_mul_scalar(dist, last_split_dist));
                }

                // Calculate the center of the camera's frustum by averaging the points.
                // This is also used as the lookat point for the shadow "camera".
                vec3 center = vec3_zero();
                for (u32 i = 0; i < 8; ++i) {
                    center = vec3_add(center, vec3_from_vec4(corners[i]));
                }
                center = vec3_div_scalar(center, 8.0f);  // size
                if (c == MAX_CASCADE_COUNT - 1) {
                    culling_center = center;
                }

                // Get the furthest-out point from the center and use that as the extents.
                f32 radius = 0.0f;
                for (u32 i = 0; i < 8; ++i) {
                    f32 distance = vec3_distance(vec3_from_vec4(corners[i]), center);
                    radius = KMAX(radius, distance);
                }
                if (c == MAX_CASCADE_COUNT - 1) {
                    culling_radius = radius;
                }

                // Calculate the extents by using the radius from above.
                extents_3d extents;
                extents.max = vec3_create(radius, radius, radius);
                extents.min = vec3_mul_scalar(extents.max, -1.0f);

                // "Pull" the min inward and "push" the max outward on the z axis to make sure
                // shadow casters outside the view are captured as well (think trees above the player).
                // TODO: This should be adjustable/tuned per scene.
                f32 z_multiplier = 10.0f;
                if (extents.min.z < 0) {
                    extents.min.z *= z_multiplier;
                } else {
                    extents.min.z /= z_multiplier;
                }

                if (extents.max.z < 0) {
                    extents.max.z /= z_multiplier;
                } else {
                    extents.max.z *= z_multiplier;
                }

                // Generate lookat by moving along the opposite direction of the directional light by the
                // minimum extents. This is negated because the directional light points "down" and the camera
                // needs to be "up".
                shadow_camera_positions[c] = vec3_sub(center, vec3_mul_scalar(light_dir, -extents.min.z));
                shadow_camera_lookats[c] = mat4_look_at(shadow_camera_positions[c], center, vec3_up());

                // Generate ortho projection based on extents.
                shadow_camera_projections[c] = mat4_orthographic(extents.min.x, extents.max.x, extents.min.y, extents.max.y, extents.min.z, extents.max.z - extents.min.z);

                // Save these off to the pass data.
                cascade->view = shadow_camera_lookats[c];
                cascade->projection = shadow_camera_projections[c];

                // Store the split depth on the pass.
                cascade->split_depth = (near + split_dist * clip_range) * 1.0f;

                last_split_dist = split_dist;
            }

            // Gather the geometries to be rendered.
            // Note that this only needs to happen once, since all geometries visible by the furthest-out cascase
            // must also be drawn on the nearest cascade to ensure objects outside the view cast shadows into the
            // view properly.
            ext_data->geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);
            if (!scene_mesh_render_data_query_from_line(
                    scene,
                    light_dir,
                    culling_center,
                    culling_radius,
                    p_frame_data,
                    &ext_data->geometry_count, &ext_data->geometries)) {
                KERROR("Failed to query shadow map pass meshes.");
            }
            // Track the number of meshes drawn in the shadow pass.
            p_frame_data->drawn_shadow_mesh_count = ext_data->geometry_count;

            // Gather terrain geometries.
            ext_data->terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);
            if (!scene_terrain_render_data_query_from_line(
                    scene,
                    light_dir,
                    culling_center,
                    culling_radius,
                    p_frame_data,
                    &ext_data->terrain_geometry_count, &ext_data->terrain_geometries)) {
                KERROR("Failed to query shadow map pass terrain geometries.");
            }

            // TODO: Counter for terrain geometries.
            p_frame_data->drawn_shadow_mesh_count += ext_data->terrain_geometry_count;
        }

        // Scene pass.
        {
            // Enable this pass for this frame.
            graph->scene_pass.pass_data.do_execute = true;
            graph->scene_pass.pass_data.vp = current_viewport;
            mat4 camera_view = camera_view_get(current_camera);
            mat4 camera_projection = current_viewport->projection;

            graph->scene_pass.pass_data.view_matrix = camera_view;
            graph->scene_pass.pass_data.view_position = camera_position_get(current_camera);
            graph->scene_pass.pass_data.projection_matrix = camera_projection;

            scene_pass_extended_data* ext_data = graph->scene_pass.pass_data.ext_data;
            // Pass over shadow map "camera" view and projection matrices (one per cascade).
            for (u32 c = 0; c < MAX_CASCADE_COUNT; c++) {
                ext_data->directional_light_views[c] = shadow_camera_lookats[c];
                ext_data->directional_light_projections[c] = shadow_camera_projections[c];

                shadow_map_pass_extended_data* sp_ext_data = graph->shadowmap_pass.pass_data.ext_data;
                ext_data->cascade_splits.elements[c] = sp_ext_data->cascades[c].split_depth;
            }
            ext_data->render_mode = render_mode;
            // HACK: use the skybox cubemap as the irradiance texture for now.
            // HACK: #2 Support for multiple skyboxes, but using the first one for now.
            // DOUBLE HACK!!!
            // TODO: Support multiple skyboxes/irradiance maps.
            ext_data->irradiance_cube_texture = scene->skyboxes ? scene->skyboxes[0].cubemap.texture : texture_system_get_default_cube_texture();

            // Camera frustum culling and count
            viewport* v = current_viewport;
            vec3 forward = camera_forward(current_camera);
            vec3 right = camera_right(current_camera);
            vec3 up = camera_up(current_camera);
            frustum camera_frustum = frustum_create(&current_camera->position, &forward, &right,
                                                    &up, v->rect.width / v->rect.height, v->fov, v->near_clip, v->far_clip);

            p_frame_data->drawn_mesh_count = 0;

            ext_data->geometries = darray_reserve_with_allocator(geometry_render_data, 512, &p_frame_data->allocator);

            // Query the scene for static meshes using the camera frustum.
            if (!scene_mesh_render_data_query(
                    scene,
                    &camera_frustum,
                    current_camera->position,
                    p_frame_data,
                    &ext_data->geometry_count, &ext_data->geometries)) {
                KERROR("Failed to query scene pass meshes.");
            }

            // Track the number of meshes drawn in the shadow pass.
            p_frame_data->drawn_mesh_count = ext_data->geometry_count;

            // Add terrain(s)
            ext_data->terrain_geometries = darray_reserve_with_allocator(geometry_render_data, 16, &p_frame_data->allocator);

            // Query the scene for terrain meshes using the camera frustum.
            if (!scene_terrain_render_data_query(
                    scene,
                    &camera_frustum,
                    current_camera->position,
                    p_frame_data,
                    &ext_data->terrain_geometry_count, &ext_data->terrain_geometries)) {
                KERROR("Failed to query scene pass terrain geometries.");
            }

            // TODO: Counter for terrain geometries.
            p_frame_data->drawn_mesh_count += ext_data->terrain_geometry_count;

            // Debug geometry
            if (!scene_debug_render_data_query(scene, &ext_data->debug_geometry_count, 0)) {
                KERROR("Failed to obtain count of debug render objects.");
                return false;
            }
            ext_data->debug_geometries = darray_reserve_with_allocator(geometry_render_data, ext_data->debug_geometry_count, &p_frame_data->allocator);

            if (!scene_debug_render_data_query(scene, &ext_data->debug_geometry_count, &ext_data->debug_geometries)) {
                KERROR("Failed to obtain debug render objects.");
                return false;
            }
            // Make sure the count is correct before pushing.
            darray_length_set(ext_data->debug_geometries, ext_data->debug_geometry_count);

            // TODO: Move this to the scene.
            /* // HACK: Inject raycast debug geometries into scene pass data.
             *
            u32 line_count = darray_length(state->test_lines);
            for (u32 i = 0; i < line_count; ++i) {
                geometry_render_data rd = {0};
                rd.model = transform_world_get(&state->test_lines[i].xform);
                geometry* g = &state->test_lines[i].geo;
                rd.material = g->material;
                rd.vertex_count = g->vertex_count;
                rd.vertex_buffer_offset = g->vertex_buffer_offset;
                rd.index_count = g->index_count;
                rd.index_buffer_offset = g->index_buffer_offset;
                rd.unique_id = INVALID_ID_U16;
                darray_push(ext_data->debug_geometries, rd);
                ext_data->debug_geometry_count++;
            }
            u32 box_count = darray_length(state->test_boxes);
            for (u32 i = 0; i < box_count; ++i) {
                geometry_render_data rd = {0};
                rd.model = transform_world_get(&state->test_boxes[i].xform);
                geometry* g = &state->test_boxes[i].geo;
                rd.material = g->material;
                rd.vertex_count = g->vertex_count;
                rd.vertex_buffer_offset = g->vertex_buffer_offset;
                rd.index_count = g->index_count;
                rd.index_buffer_offset = g->index_buffer_offset;
                rd.unique_id = INVALID_ID_U16;
                darray_push(ext_data->debug_geometries, rd);
                ext_data->debug_geometry_count++;
            } */
        }
    } else {
        // Scene not loaded.

        // Do not run these passes if the scene is not loaded.
        graph->scene_pass.pass_data.do_execute = false;
        graph->shadowmap_pass.pass_data.do_execute = false;
    }

    return true;
}

b8 forward_rendergraph_execute(forward_rendergraph* graph, struct frame_data* p_frame_data) {
    return rendergraph_execute_frame(&graph->internal_graph, p_frame_data);
}

b8 forward_rendergraph_on_resize(forward_rendergraph* graph, u32 width, u32 height) {
    return rendergraph_on_resize(&graph->internal_graph, width, height);
}
