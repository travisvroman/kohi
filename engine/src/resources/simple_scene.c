#include "simple_scene.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/frame_data.h"
#include "containers/darray.h"
#include "math/transform.h"
#include "resources/resource_types.h"
#include "resources/skybox.h"
#include "resources/mesh.h"
#include "renderer/renderer_types.inl"
#include "systems/render_view_system.h"
#include "renderer/camera.h"
#include "math/kmath.h"
#include "systems/light_system.h"

static void simple_scene_actual_unload(simple_scene* scene);

static u32 global_scene_id = 0;

b8 simple_scene_create(void* config, simple_scene* out_scene) {
    if (!out_scene) {
        KERROR(("simple_scene_create(): A valid pointer to out_scene is required."));
        return false;
    }

    kzero_memory(out_scene, sizeof(simple_scene));

    out_scene->enabled = false;
    out_scene->state = SIMPLE_SCENE_STATE_UNINITIALIZED;
    out_scene->scene_transform = transform_create();
    global_scene_id++;
    out_scene->id = global_scene_id;

    // Internal "lists" of renderable objects.
    out_scene->dir_light = 0;
    out_scene->point_lights = darray_create(point_light*);
    out_scene->meshes = darray_create(mesh*);
    out_scene->sb = 0;

    // TODO: process scene config.

    return true;
}

b8 simple_scene_initialize(simple_scene* scene) {
    if (!scene) {
        KERROR("simple_scene_initialize requires a valid pointer to a scene.");
        return false;
    }

    // TODO: Process configuration and setup hierarchy.

    if (scene->sb) {
        if (!skybox_initialize(scene->sb)) {
            KERROR("Skybox failed to initialize.");
            scene->sb = 0;
            return false;
        }
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (!mesh_initialize(scene->meshes[i])) {
            KERROR("Mesh failed to initialize.");
            return false;
        }
    }

    // Update the state to show the scene is initialized.
    scene->state = SIMPLE_SCENE_STATE_INITIALIZED;

    return true;
}

b8 simple_scene_load(simple_scene* scene) {
    if (!scene) {
        return false;
    }

    // Update the state to show the scene is currently loading.
    scene->state = SIMPLE_SCENE_STATE_LOADING;

    if (scene->sb) {
        if (scene->sb->instance_id == INVALID_ID) {
            if (!skybox_load(scene->sb)) {
                KERROR("Skybox failed to load.");
                scene->sb = 0;
                return false;
            }
        }
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (!mesh_load(scene->meshes[i])) {
            KERROR("Mesh failed to load.");
            return false;
        }
    }

    // Update the state to show the scene is fully loaded.
    scene->state = SIMPLE_SCENE_STATE_LOADED;

    return true;
}

b8 simple_scene_unload(simple_scene* scene) {
    if (!scene) {
        return false;
    }

    // Update the state to show the scene is currently unloading.
    scene->state = SIMPLE_SCENE_STATE_UNLOADING;
    return true;
}

b8 simple_scene_update(simple_scene* scene, const struct frame_data* p_frame_data) {
    if (!scene) {
        return false;
    }

    if (scene->state == SIMPLE_SCENE_STATE_UNLOADING) {
        simple_scene_actual_unload(scene);
    }

    return true;
}

b8 simple_scene_populate_render_packet(simple_scene* scene, struct camera* current_camera, f32 aspect, struct frame_data* p_frame_data, struct render_packet* packet) {
    if (!scene || !packet) {
        return false;
    }

    // TODO: cache this somewhere so that we don't have to check this every time.
    for (u32 i = 0; i < packet->view_count; ++i) {
        render_view_packet* view_packet = &packet->views[i];
        const render_view* view = view_packet->view;
        if (view->type == RENDERER_VIEW_KNOWN_TYPE_SKYBOX) {
            if (scene->sb) {
                // Skybox
                skybox_packet_data skybox_data = {};
                skybox_data.sb = scene->sb;
                if (!render_view_system_build_packet(view, p_frame_data->frame_allocator, &skybox_data, view_packet)) {
                    KERROR("Failed to build packet for view 'skybox'.");
                    return false;
                }
            }
            break;
        }
    }

    for (u32 i = 0; i < packet->view_count; ++i) {
        render_view_packet* view_packet = &packet->views[i];
        const render_view* view = view_packet->view;
        if (view->type == RENDERER_VIEW_KNOWN_TYPE_WORLD) {
            // Update the frustum
            vec3 forward = camera_forward(current_camera);
            vec3 right = camera_right(current_camera);
            vec3 up = camera_up(current_camera);
            // TODO: get camera fov, aspect, etc.
            frustum f = frustom_create(&current_camera->position, &forward, &right, &up, aspect, deg_to_rad(45.0f), 0.1f, 1000.0f);

            // NOTE: starting at a reasonable default to avoid too many reallocs.
            // TODO: Use frame allocator.
            geometry_render_data* world_geometries = darray_reserve(geometry_render_data, 512);
            p_frame_data->drawn_mesh_count = 0;

            u32 mesh_count = darray_length(scene->meshes);
            for (u32 i = 0; i < mesh_count; ++i) {
                mesh* m = scene->meshes[i];
                if (m->generation != INVALID_ID_U8) {
                    mat4 model = transform_get_world(&m->transform);

                    for (u32 j = 0; j < m->geometry_count; ++j) {
                        geometry* g = m->geometries[j];

                        // // Bounding sphere calculation.
                        // {
                        //     // Translate/scale the extents.
                        //     vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                        //     vec3 extents_max = vec3_mul_mat4(g->extents.max, model);

                        //     f32 min = KMIN(KMIN(extents_min.x, extents_min.y), extents_min.z);
                        //     f32 max = KMAX(KMAX(extents_max.x, extents_max.y), extents_max.z);
                        //     f32 diff = kabs(max - min);
                        //     f32 radius = diff * 0.5f;

                        //     // Translate/scale the center.
                        //     vec3 center = vec3_mul_mat4(g->center, model);

                        //     if (frustum_intersects_sphere(&state->camera_frustum, &center, radius)) {
                        //         // Add it to the list to be rendered.
                        //         geometry_render_data data = {0};
                        //         data.model = model;
                        //         data.geometry = g;
                        //         data.unique_id = m->unique_id;
                        //         darray_push(game_inst->frame_data.world_geometries, data);

                        //         draw_count++;
                        //     }
                        // }

                        // AABB calculation
                        {
                            // Translate/scale the extents.
                            // vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                            vec3 extents_max = vec3_mul_mat4(g->extents.max, model);

                            // Translate/scale the center.
                            vec3 center = vec3_mul_mat4(g->center, model);
                            vec3 half_extents = {
                                kabs(extents_max.x - center.x),
                                kabs(extents_max.y - center.y),
                                kabs(extents_max.z - center.z),
                            };

                            if (frustum_intersects_aabb(&f, &center, &half_extents)) {
                                // Add it to the list to be rendered.
                                geometry_render_data data = {0};
                                data.model = model;
                                data.geometry = g;
                                data.unique_id = m->unique_id;
                                darray_push(world_geometries, data);

                                p_frame_data->drawn_mesh_count++;
                            }
                        }
                    }
                }
            }

            // World
            if (!render_view_system_build_packet(render_view_system_get("world"), p_frame_data->frame_allocator, world_geometries, &packet->views[1])) {
                KERROR("Failed to build packet for view 'world_opaque'.");
                return false;
            }

            // TODO: bad.....
            darray_destroy(world_geometries);
        }
    }

    return true;
}

b8 simple_scene_add_directional_light(simple_scene* scene, struct directional_light* light) {
    if (!scene) {
        return false;
    }

    if (scene->dir_light) {
        // TODO: Do any resource unloading required.
        light_system_remove_directional(scene->dir_light);
    }

    if (light) {
        if (!light_system_add_directional(light)) {
            KERROR("simple_scene_add_directional_light - failed to add directional light to light system.");
            return false;
        }
    }

    scene->dir_light = light;

    return true;
}

b8 simple_scene_add_point_light(simple_scene* scene, struct point_light* light) {
    if (!scene || !light) {
        return false;
    }

    if (!light_system_add_point(light)) {
        KERROR("Failed to add point light to scene (light system add failure, check logs).");
        return false;
    }

    darray_push(scene->point_lights, light);

    return true;
}

b8 simple_scene_add_mesh(simple_scene* scene, struct mesh* m) {
    if (!scene || !m) {
        return false;
    }

    if (scene->state > SIMPLE_SCENE_STATE_INITIALIZED) {
        if (!mesh_initialize(m)) {
            KERROR("Mesh failed to initialize.");
            return false;
        }
    }

    if (scene->state >= SIMPLE_SCENE_STATE_LOADED) {
        if (!mesh_load(m)) {
            KERROR("Mesh failed to load.");
            return false;
        }
    }

    darray_push(scene->meshes, m);

    return true;
}

b8 simple_scene_add_skybox(simple_scene* scene, struct skybox* sb) {
    if (!scene) {
        return false;
    }

    // TODO: if one already exists, do we do anything with it?
    scene->sb = sb;
    if (scene->state > SIMPLE_SCENE_STATE_INITIALIZED) {
        if (!skybox_initialize(sb)) {
            KERROR("Skybox failed to initialize.");
            scene->sb = 0;
            return false;
        }
    }

    if (scene->state >= SIMPLE_SCENE_STATE_LOADED) {
        if (!skybox_load(sb)) {
            KERROR("Skybox failed to load.");
            scene->sb = 0;
            return false;
        }
    }

    return true;
}

b8 simple_scene_remove_directional_light(simple_scene* scene, struct directional_light* light) {
    if (!scene || !light) {
        return false;
    }

    if (light != scene->dir_light) {
        KWARN("Cannot remove directional light from scene that is not part of the scene.");
        return false;
    }

    if (!light_system_remove_directional(light)) {
        KERROR("Failed to remove directional light from light system.");
        return false;
    }

    scene->dir_light = 0;

    return true;
}

b8 simple_scene_remove_point_light(simple_scene* scene, struct point_light* light) {
    if (!scene || !light) {
        return false;
    }

    u32 light_count = darray_length(scene->point_lights);
    for (u32 i = 0; i < light_count; ++i) {
        if (scene->point_lights[i] == light) {
            if (!light_system_remove_point(light)) {
                KERROR("Failed to remove point light from light system.");
                return false;
            }

            point_light rubbish = {0};
            darray_pop_at(scene->point_lights, i, &rubbish);

            return true;
        }
    }

    KERROR("Cannot remove a point light from a scene of which it is not a part.");
    return false;
}

b8 simple_scene_remove_mesh(simple_scene* scene, struct mesh* m) {
    return true;
}

b8 simple_scene_remove_skybox(simple_scene* scene, struct skybox* sb) {
    return true;
}

static void simple_scene_actual_unload(simple_scene* scene) {
    if (scene->sb) {
        if (!skybox_unload(scene->sb)) {
            KERROR("Failed to unload skybox");
        }
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (scene->meshes[i]->generation != INVALID_ID_U8) {
            if (!mesh_unload(scene->meshes[i])) {
                KERROR("Failed to unload mesh.");
            }
        }
    }

    if (scene->dir_light) {
        // TODO: If there are resource to unload, that should be done before this next line. Ex: box representing pos/colour
        if (!simple_scene_remove_directional_light(scene, scene->dir_light)) {
            KERROR("Failed to unload/remove directional light.");
        }
    }

    u32 p_light_count = darray_length(scene->point_lights);
    for (u32 i = 0; i < p_light_count; ++i) {
        // TODO: If there are resource to unload, that should be done before this next line. Ex: box representing pos/colour
        // Always kill the first one on the list each time since entries are popped from the array.
        if (!simple_scene_remove_point_light(scene, scene->point_lights[0])) {
            KERROR("Failed to unload/remove point light.");
        }
    }

    // Update the state to show the scene is initialized.
    scene->state = SIMPLE_SCENE_STATE_UNLOADED;

    // Also destroy the scene.
    scene->dir_light = 0;
    scene->sb = 0;

    if (scene->point_lights) {
        darray_destroy(scene->point_lights);
    }

    if (scene->meshes) {
        darray_destroy(scene->meshes);
    }

    kzero_memory(scene, sizeof(simple_scene));
}