#include "scene.h"

#include "containers/darray.h"
#include "core/console.h"
#include "core/frame_data.h"
#include "core/identifier.h"
#include "core/khandle.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "graphs/hierarchy_graph.h"
#include "math/geometry_3d.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "math/transform.h"
#include "renderer/camera.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "resources/debug/debug_box3d.h"
#include "resources/debug/debug_line3d.h"
#include "resources/mesh.h"
#include "resources/resource_types.h"
#include "resources/skybox.h"
#include "resources/terrain.h"
#include "systems/light_system.h"
#include "systems/resource_system.h"
#include "systems/xform_system.h"
#include "utils/ksort.h"

static void scene_actual_unload(scene *scene);
static scene_attachment *scene_attachment_acquire(scene *s);
static void scene_attachment_release(scene *s, scene_attachment *attachment);

static u32 global_scene_id = 0;

typedef struct scene_debug_data {
    debug_box3d box;
    debug_line3d line;
} scene_debug_data;

/** @brief A private structure used to sort geometry by distance from the camera. */
typedef struct geometry_distance {
    /** @brief The geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

static i32 geometry_render_data_compare(void *a, void *b) {
    geometry_render_data *a_typed = a;
    geometry_render_data *b_typed = b;
    if (!a_typed->material || !b_typed->material) {
        return 0;  // Don't sort invalid entries.
    }
    return a_typed->material->id - b_typed->material->id;
}

static i32 geometry_distance_compare(void *a, void *b) {
    geometry_distance *a_typed = a;
    geometry_distance *b_typed = b;
    if (a_typed->distance > b_typed->distance) {
        return 1;
    } else if (a_typed->distance < b_typed->distance) {
        return -1;
    }
    return 0;
}

b8 scene_create(void *config, scene *out_scene) {
    if (!out_scene) {
        KERROR("scene_create(): A valid pointer to out_scene is required.");
        return false;
    }

    kzero_memory(out_scene, sizeof(scene));

    out_scene->enabled = false;
    out_scene->state = SCENE_STATE_UNINITIALIZED;
    global_scene_id++;
    out_scene->id = global_scene_id;

    // Internal "lists" of renderable objects.
    out_scene->dir_lights = darray_create(directional_light);
    out_scene->point_lights = darray_create(point_light);
    out_scene->meshes = darray_create(mesh);
    out_scene->terrains = darray_create(terrain);
    out_scene->skyboxes = darray_create(skybox);

    // Internal lists of attachments.
    out_scene->mesh_attachments = darray_create(scene_attachment);
    out_scene->terrain_attachments = darray_create(scene_attachment);

    if (!hierarchy_graph_create(&out_scene->hierarchy)) {
        KERROR("Failed to create hierarchy graph");
        return false;
    }

    if (config) {
        out_scene->config = kallocate(sizeof(scene_config), MEMORY_TAG_SCENE);
        kcopy_memory(out_scene->config, config, sizeof(scene_config));
    }

    debug_grid_config grid_config = {0};
    grid_config.orientation = DEBUG_GRID_ORIENTATION_XZ;
    grid_config.tile_count_dim_0 = 100;
    grid_config.tile_count_dim_1 = 100;
    grid_config.tile_scale = 1.0f;
    grid_config.name = "debug_grid";
    grid_config.use_third_axis = true;

    if (!debug_grid_create(&grid_config, &out_scene->grid)) {
        return false;
    }

    return true;
}

void scene_node_initialize(scene *s, k_handle parent_handle, scene_node_config *node_config) {
    if (node_config) {
        if (node_config->name) {
            char *name = string_duplicate(node_config->name);
        }

        // Obtain the xform if one is configured.
        k_handle xform_handle;
        if (node_config->xform) {
            xform_handle = xform_from_position_rotation_scale(node_config->xform->position, node_config->xform->rotation, node_config->xform->scale);
        } else {
            xform_handle = k_handle_invalid();
        }

        // Add a node in the heirarchy.
        k_handle node_handle = hierarchy_graph_child_add_with_xform(&s->hierarchy, parent_handle, xform_handle);

        // Process attachment configs.
        if (node_config->attachments) {
            u32 attachment_count = darray_length(node_config->attachments);
            for (u32 i = 0; i < attachment_count; ++i) {
                void *attachment = &node_config->attachments[i];
                scene_node_attachment_type attachment_type = *((scene_node_attachment_type *)attachment);
                switch (attachment_type) {
                    default:
                    case SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN:
                        KERROR("An unknown attachment type was found in config. This attachment will be ignored.");
                        continue;
                    case SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
                        scene_node_attachment_static_mesh *typed_attachment = attachment;

                        if (typed_attachment->resource_name) {
                            KWARN("Invalid mesh config, resource_name is required.");
                            return;
                        }

                        // Create mesh config, then create the mesh.
                        mesh_config new_mesh_config = {0};
                        new_mesh_config.resource_name = string_duplicate(typed_attachment->resource_name);
                        mesh new_mesh = {0};
                        if (!mesh_create(new_mesh_config, &new_mesh)) {
                            KERROR("Failed to create new mesh in scene.");
                            kfree(new_mesh_config.resource_name, string_length(new_mesh_config.resource_name), MEMORY_TAG_STRING);
                            return;
                        }

                        // Destroy the config.
                        kfree(new_mesh_config.resource_name, string_length(new_mesh_config.resource_name), MEMORY_TAG_STRING);

                        if (!mesh_initialize(&new_mesh)) {
                            KERROR("Failed to initialize static mesh.");
                            return;
                        } else {
                            // Find a free static mesh slot and take it, or push a new one.
                            u32 index = INVALID_ID;
                            u32 count = darray_length(s->meshes);
                            for (u32 i = 0; i < count; ++i) {
                                if (s->meshes[i].state == MESH_STATE_UNDEFINED) {
                                    // Found a slot, use it.
                                    index = i;
                                    s->meshes[i] = new_mesh;
                                    break;
                                }
                            }
                            if (index == INVALID_ID) {
                                darray_push(s->meshes, new_mesh);
                                index = count;
                            }

                            // Acquire a scene node attachment and set its resource handle.
                            scene_attachment *mesh_attachment = scene_attachment_acquire(s);
                            if (!mesh_attachment) {
                                KERROR("Failed to acquire scene attachment.");
                                return;
                            }
                            mesh_attachment->resource_handle = k_handle_create(index);
                            mesh_attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
                            mesh_attachment->hierarchy_node_handle = node_handle;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_TERRAIN: {
                        scene_node_attachment_terrain *typed_attachment = attachment;

                        if (typed_attachment->resource_name) {
                            KWARN("Invalid terrain config, resource_name is required.");
                            return;
                        }

                        terrain_config new_terrain_config = {0};
                        new_terrain_config.resource_name = typed_attachment->resource_name;
                        terrain new_terrain = {0};
                        if (!terrain_create(&new_terrain_config, &new_terrain)) {
                            KWARN("Failed to load terrain.");
                            return;
                        }

                        // Destroy the config.
                        kfree(new_terrain_config.resource_name, string_length(new_terrain_config.resource_name), MEMORY_TAG_STRING);

                        if (!terrain_initialize(&new_terrain)) {
                            KERROR("Failed to initialize terrain.");
                            return;
                        } else {
                            // Find a free static terrain slot and take it, or push a new one.
                            u32 index = INVALID_ID;
                            u32 count = darray_length(s->terrains);
                            for (u32 i = 0; i < count; ++i) {
                                if (s->terrains[i].state == TERRAIN_STATE_UNDEFINED) {
                                    // Found a slot, use it.
                                    index = i;
                                    s->terrains[i] = new_terrain;
                                    break;
                                }
                            }
                            if (index == INVALID_ID) {
                                darray_push(s->terrains, new_terrain);
                                index = count;
                            }

                            // Acquire a scene node attachment and set its resource handle.
                            scene_attachment *mesh_attachment = scene_attachment_acquire(s);
                            if (!mesh_attachment) {
                                KERROR("Failed to acquire scene attachment.");
                                return;
                            }
                            mesh_attachment->resource_handle = k_handle_create(index);
                            mesh_attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_TERRAIN;
                            mesh_attachment->hierarchy_node_handle = node_handle;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
                        scene_node_attachment_skybox *typed_attachment = attachment;

                        // Create a skybox config and use it to create the skybox.
                        skybox_config sb_config = {0};
                        sb_config.cubemap_name = string_duplicate(typed_attachment->cubemap_name);
                        skybox sb;
                        if (!skybox_create(sb_config, &sb)) {
                            KWARN("Failed to create skybox.");
                        }

                        // Destroy the skybox config.
                        string_free((char *)sb_config.cubemap_name);
                        sb_config.cubemap_name = 0;

                        // Initialize the skybox.
                        if (!skybox_initialize(&sb)) {
                            KERROR("Failed to initialize skybox. See logs for details.");
                        } else {
                            // Find a free skybox slot and take it, or push a new one.
                            u32 index = INVALID_ID;
                            u32 skybox_count = darray_length(s->skyboxes);
                            for (u32 i = 0; i < skybox_count; ++i) {
                                if (s->skyboxes[i].state == SKYBOX_STATE_UNDEFINED) {
                                    // Found a slot, use it.
                                    index = i;
                                    s->skyboxes[i] = sb;
                                    break;
                                }
                            }
                            if (index == INVALID_ID) {
                                darray_push(s->skyboxes, sb);
                                index = skybox_count;
                            }

                            // Acquire a scene node attachment and set its resource handle.
                            scene_attachment *sb_attachment = scene_attachment_acquire(s);
                            if (!sb_attachment) {
                                KERROR("Failed to acquire scene attachment.");
                                return;
                            }
                            sb_attachment->resource_handle = k_handle_create(index);
                            sb_attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_SKYBOX;
                            sb_attachment->hierarchy_node_handle = node_handle;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
                        scene_node_attachment_directional_light *typed_attachment = attachment;

                        directional_light new_dir_light = {0};
                        // TODO: name?
                        /* new_dir_light.name = string_duplicate(typed_attachment.name); */
                        new_dir_light.data.colour = typed_attachment->colour;
                        new_dir_light.data.direction = typed_attachment->direction;
                        new_dir_light.data.shadow_distance = typed_attachment->shadow_distance;
                        new_dir_light.data.shadow_fade_distance = typed_attachment->shadow_fade_distance;
                        new_dir_light.data.shadow_split_mult = typed_attachment->shadow_split_mult;
                        new_dir_light.generation = 0;

                        // Add debug data and initialize it.
                        new_dir_light.debug_data = kallocate(sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                        scene_debug_data *debug = new_dir_light.debug_data;

                        // Generate the line points based on the light direction.
                        // The first point will always be at the scene's origin.
                        vec3 point_0 = vec3_zero();
                        vec3 point_1 = vec3_mul_scalar(vec3_normalized(vec3_from_vec4(scene->dir_light->data.direction)), -1.0f);

                        if (!debug_line3d_create(point_0, point_1, 0, &debug->line)) {
                            KERROR("Failed to create debug line for directional light.");
                        }
                        if (!debug_line3d_initialize(&debug->line)) {
                            KERROR("Failed to create debug line for directional light.");
                        } else {
                            // Find a free skybox slot and take it, or push a new one.
                            u32 index = INVALID_ID;
                            u32 directional_light_count = darray_length(s->dir_lights);
                            for (u32 i = 0; i < directional_light_count; ++i) {
                                if (s->dir_lights[i].generation == INVALID_ID) {
                                    // Found a slot, use it.
                                    index = i;
                                    s->dir_lights[i] = new_dir_light;
                                    break;
                                }
                            }
                            if (index == INVALID_ID) {
                                darray_push(s->dir_lights, new_dir_light);
                                index = directional_light_count;
                            }

                            // Acquire a scene node attachment and set its resource handle.
                            scene_attachment *sb_attachment = scene_attachment_acquire(s);
                            if (!sb_attachment) {
                                KERROR("Failed to acquire scene attachment.");
                                return;
                            }
                            sb_attachment->resource_handle = k_handle_create(index);
                            sb_attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT;
                            sb_attachment->hierarchy_node_handle = node_handle;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
                        scene_node_attachment_point_light *typed_attachment = attachment;

                        point_light new_light = {0};
                        // TODO: name?
                        /* new_light.name = string_duplicate(typed_attachment->name); */
                        new_light.data.colour = typed_attachment->colour;
                        new_light.data.constant_f = typed_attachment->constant_f;
                        new_light.data.linear = typed_attachment->linear;
                        new_light.data.position = typed_attachment->position;
                        new_light.data.quadratic = typed_attachment->quadratic;

                        // Add debug data and initialize it.
                        new_light.debug_data = kallocate(sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                        scene_debug_data *debug = new_light.debug_data;

                        if (!debug_box3d_create((vec3){0.2f, 0.2f, 0.2f}, 0, &debug->box)) {
                            KERROR("Failed to create debug box for directional light.");
                        } else {
                            transform_position_set(&debug->box.xform, vec3_from_vec4(new_light.data.position));
                        }
                        if (!debug_box3d_initialize(&debug->box)) {
                            KERROR("Failed to create debug box for point light.");
                        } else {
                            // Find a free skybox slot and take it, or push a new one.
                            u32 index = INVALID_ID;
                            u32 point_light_count = darray_length(s->point_lights);
                            for (u32 i = 0; i < point_light_count; ++i) {
                                if (s->point_lights[i].generation == INVALID_ID) {
                                    // Found a slot, use it.
                                    index = i;
                                    s->point_lights[i] = new_light;
                                    break;
                                }
                            }
                            if (index == INVALID_ID) {
                                darray_push(s->point_lights, new_light);
                                index = point_light_count;
                            }

                            // Acquire a scene node attachment and set its resource handle.
                            scene_attachment *sb_attachment = scene_attachment_acquire(s);
                            if (!sb_attachment) {
                                KERROR("Failed to acquire scene attachment.");
                                return;
                            }
                            sb_attachment->resource_handle = k_handle_create(index);
                            sb_attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT;
                            sb_attachment->hierarchy_node_handle = node_handle;
                        }
                    } break;
                }
            }
        }

        // Process children.
        if (node_config->children) {
            u32 child_count = darray_length(node_config->children);
            for (u32 i = 0; i < child_count; ++i) {
                scene_node_initialize(s, node_handle, &node_config->children[i]);
            }
        }
    }
}

b8 scene_initialize(scene *scene) {
    if (!scene) {
        KERROR("scene_initialize requires a valid pointer to a scene.");
        return false;
    }

    // Process configuration and setup hierarchy.
    if (scene->config) {
        scene_config *config = scene->config;
        if (scene->config->name) {
            scene->name = string_duplicate(scene->config->name);
        }
        if (scene->config->description) {
            scene->description = string_duplicate(scene->config->description);
        }

        // Process root nodes.
        if (config->nodes) {
            u32 node_count = darray_length(config->nodes);
            // An invalid handle means there is no parent, which is true for root nodes.
            k_handle invalid_handle = k_handle_invalid();
            for (u32 i = 0; i < node_count; ++i) {
                scene_node_initialize(scene, invalid_handle, &config->nodes[i]);
            }
        }

        // TODO: Convert grid to use the new node/attachment configs/logic
        if (!debug_grid_initialize(&scene->grid)) {
            return false;
        }
    }

    // Update the state to show the scene is initialized.
    scene->state = SCENE_STATE_INITIALIZED;

    return true;
}

b8 scene_load(scene *scene) {
    if (!scene) {
        return false;
    }

    // Update the state to show the scene is currently loading.
    scene->state = SCENE_STATE_LOADING;

    // Register with the console.
    console_object_register("scene", scene, CONSOLE_OBJECT_TYPE_STRUCT);
    console_object_add_property("scene", "id", &scene->id, CONSOLE_OBJECT_TYPE_UINT32);

    // Load skyboxes
    if (scene->skyboxes) {
        u32 skybox_count = darray_length(scene->skyboxes);
        for (u32 i = 0; i < skybox_count; ++i) {
            if (!skybox_load(&scene->skyboxes[i])) {
                KERROR("Failed to load skybox. See logs for details.");
            }
        }
    }

    // Load static meshes
    if (scene->meshes) {
        u32 mesh_count = darray_length(scene->meshes);
        for (u32 i = 0; i < mesh_count; ++i) {
            if (!mesh_load(&scene->meshes[i])) {
                KERROR("Mesh failed to load.");
            }
        }
    }

    // Load terrains
    if (scene->terrains) {
        u32 terrain_count = darray_length(scene->terrains);
        for (u32 i = 0; i < terrain_count; ++i) {
            if (!terrain_load(&scene->terrains[i])) {
                KERROR("Terrain failed to load.");
            }
        }
    }

    // Debug grid.
    if (!debug_grid_load(&scene->grid)) {
        return false;
    }

    if (scene->dir_lights) {
        u32 directional_light_count = darray_length(scene->dir_lights);
        for (u32 i = 0; i < directional_light_count; ++i) {
            if (!light_system_directional_add(&scene->dir_lights[i])) {
                KWARN("Failed to add directional light to lighting system.");
            } else {
                if (scene->dir_lights[i].debug_data) {
                    scene_debug_data *debug = scene->dir_lights[i].debug_data;
                    if (!debug_line3d_load(&debug->line)) {
                        KERROR("debug line failed to load.");
                        kfree(scene->dir_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                        scene->dir_lights[i].debug_data = 0;
                    }
                }
            }
        }
    }

    if (scene->point_lights) {
        u32 point_light_count = darray_length(scene->point_lights);
        for (u32 i = 0; i < point_light_count; ++i) {
            if (!light_system_point_add(&scene->point_lights[i])) {
                KWARN("Failed to add point light to lighting system.");
            } else {
                // Load debug data if it was setup.
                scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;
                if (!debug_box3d_load(&debug->box)) {
                    KERROR("debug box failed to load.");
                    kfree(scene->point_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    scene->point_lights[i].debug_data = 0;
                }
            }
        }
    }

    // Update the state to show the scene is fully loaded.
    scene->state = SCENE_STATE_LOADED;

    return true;
}

b8 scene_unload(scene *scene, b8 immediate) {
    if (!scene) {
        return false;
    }

    if (immediate) {
        scene->state = SCENE_STATE_UNLOADING;
        scene_actual_unload(scene);
        return true;
    }

    // Update the state to show the scene is currently unloading.
    scene->state = SCENE_STATE_UNLOADING;
    return true;
}

b8 scene_update(scene *scene, const struct frame_data *p_frame_data) {
    if (!scene) {
        return false;
    }

    if (scene->state == SCENE_STATE_UNLOADING) {
        scene_actual_unload(scene);
        return true;
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        if (scene->dir_lights) {
            u32 directional_light_count = darray_length(scene->dir_lights);
            for (u32 i = 0; i < directional_light_count; ++i) {
                // TODO: Only update directional light if changed.
                if (scene->dir_lights[i].generation != INVALID_ID && scene->dir_lights[i].debug_data) {
                    scene_debug_data *debug = scene->dir_lights[i].debug_data;
                    if (debug->line.geo.generation != INVALID_ID_U16) {
                        // Update colour. NOTE: doing this every frame might be expensive if we have to reload the geometry all the time.
                        // TODO: Perhaps there is another way to accomplish this, like a shader that uses a uniform for colour?
                        debug_line3d_colour_set(&debug->line, scene->dir_lights[i].data.colour);
                    }
                }
            }
        }

        // Update point light debug boxes.
        if (scene->point_lights) {
            u32 point_light_count = darray_length(scene->point_lights);
            for (u32 i = 0; i < point_light_count; ++i) {
                if (scene->point_lights[i].debug_data) {
                    // TODO: Only update point light if changed.
                    scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;
                    if (debug->box.geo.generation != INVALID_ID_U16) {
                        // Update transform.
                        transform_position_set(&debug->box.xform, vec3_from_vec4(scene->point_lights[i].data.position));

                        // Update colour. NOTE: doing this every frame might be expensive if we have to reload the geometry all the time.
                        // TODO: Perhaps there is another way to accomplish this, like a shader that uses a uniform for colour?
                        debug_box3d_colour_set(&debug->box, scene->point_lights[i].data.colour);
                    }
                }
            }
        }

        // Check meshes to see if they have debug data. If not, add it here and init/load it.
        // Doing this here because mesh loading is multi-threaded, and may not yet be available
        // even though the object is present in the scene.
        u32 mesh_count = darray_length(scene->meshes);
        for (u32 i = 0; i < mesh_count; ++i) {
            mesh *m = &scene->meshes[i];
            if (m->generation == INVALID_ID_U8) {
                continue;
            }
            if (!m->debug_data) {
                m->debug_data = kallocate(sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene_debug_data *debug = m->debug_data;

                if (!debug_box3d_create((vec3){0.2f, 0.2f, 0.2f}, 0, &debug->box)) {
                    KERROR("Failed to create debug box for mesh '%s'.", m->name);
                } else {
                    // TODO: Need to update debug box/lines to use xforms instead of transforms.
                    // This is broken until that is fixed.
                    /* transform_parent_set(&debug->box.xform, &m->transform); */

                    if (!debug_box3d_initialize(&debug->box)) {
                        KERROR("debug box failed to initialize.");
                        kfree(m->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                        m->debug_data = 0;
                        continue;
                    }

                    if (!debug_box3d_load(&debug->box)) {
                        KERROR("debug box failed to load.");
                        kfree(m->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                        m->debug_data = 0;
                    }

                    // Update the extents.
                    debug_box3d_colour_set(&debug->box, (vec4){0.0f, 1.0f, 0.0f, 1.0f});
                    debug_box3d_extents_set(&debug->box, m->extents);
                }
            }
        }
    }

    return true;
}

void scene_render_frame_prepare(scene *scene, const struct frame_data *p_frame_data) {
    if (!scene) {
        return;
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        if (scene->dir_lights) {
            u32 directional_light_count = darray_length(scene->dir_lights);
            for (u32 i = 0; i < directional_light_count; ++i) {
                if (scene->dir_lights[i].generation != INVALID_ID && scene->dir_lights[i].debug_data) {
                    scene_debug_data *debug = scene->dir_lights[i].debug_data;
                    debug_line3d_render_frame_prepare(&debug->line, p_frame_data);
                }
            }
        }

        // Update point light debug boxes.
        if (scene->point_lights) {
            u32 point_light_count = darray_length(scene->point_lights);
            for (u32 i = 0; i < point_light_count; ++i) {
                if (scene->point_lights[i].debug_data) {
                    scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;
                    debug_box3d_render_frame_prepare(&debug->box, p_frame_data);
                }
            }
        }

        // Check meshes to see if they have debug data.
        if (scene->meshes) {
            u32 mesh_count = darray_length(scene->meshes);
            for (u32 i = 0; i < mesh_count; ++i) {
                mesh *m = &scene->meshes[i];
                if (m->generation == INVALID_ID_U8) {
                    continue;
                }
                if (m->debug_data) {
                    scene_debug_data *debug = m->debug_data;
                    debug_box3d_render_frame_prepare(&debug->box, p_frame_data);
                }
            }
        }
    }
}

void scene_update_lod_from_view_position(scene *scene, const frame_data *p_frame_data, vec3 view_position, f32 near_clip, f32 far_clip) {
    if (!scene) {
        return;
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        // Update terrain chunk LODs
        u32 terrain_count = darray_length(scene->terrains);
        for (u32 i = 0; i < terrain_count; ++i) {
            terrain *t = &scene->terrains[i];

            // Perform a lookup into the attachments array to get the hierarchy node.
            // TODO: simplify the lookup process.
            scene_attachment *attachment = &scene->terrain_attachments[scene->terrain_attachment_indices[i]];
            k_handle xform_handle = scene->hierarchy.xform_handles[attachment->hierarchy_node_handle.handle_index];
            mat4 model = xform_world_get(xform_handle);

            // Calculate LOD splits based on clip range.
            f32 range = far_clip - near_clip;

            // The first split distance is always 0.
            f32 *splits = p_frame_data->allocator.allocate(sizeof(f32) * (t->lod_count + 1));
            splits[0] = 0.0f;
            for (u32 l = 0; l < t->lod_count; ++l) {
                f32 pct = (l + 1) / (f32)t->lod_count;
                // Just do linear splits for now.
                splits[l + 1] = (near_clip + range) * pct;
            }

            // Calculate chunk LODs based on distance from camera position.
            for (u32 c = 0; c < t->chunk_count; c++) {
                terrain_chunk *chunk = &t->chunks[c];

                // Translate/scale the center.
                vec3 g_center = vec3_mul_mat4(chunk->center, model);

                // Check the distance of the chunk.
                f32 dist_to_chunk = vec3_distance(view_position, g_center);
                u8 lod = INVALID_ID_U8;
                for (u8 l = 0; l < t->lod_count; ++l) {
                    // If between this and the next split, this is the LOD to use.
                    if (dist_to_chunk >= splits[l] && dist_to_chunk <= splits[l + 1]) {
                        lod = l;
                        break;
                    }
                }
                // Cover the case of chunks outside the view frustum.
                if (lod == INVALID_ID_U8) {
                    lod = t->lod_count - 1;
                }

                chunk->current_lod = lod;
            }
        }
    }
}

b8 scene_raycast(scene *scene, const struct ray *r, struct raycast_result *out_result) {
    if (!scene || !r || !out_result || scene->state < SCENE_STATE_LOADED) {
        return false;
    }

    // Only create if needed.
    out_result->hits = 0;

    // Iterate meshes in the scene.
    // TODO: This needs to be optimized. We need some sort of spatial partitioning to speed this up.
    // Otherwise a scene with thousands of objects will be super slow!
    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        mesh *m = &scene->meshes[i];
        // Perform a lookup into the attachments array to get the hierarchy node.
        // TODO: simplify the lookup process.
        scene_attachment *attachment = &scene->mesh_attachments[scene->mesh_attachment_indices[i]];
        k_handle xform_handle = scene->hierarchy.xform_handles[attachment->hierarchy_node_handle.handle_index];
        mat4 model = xform_world_get(xform_handle);
        f32 dist;
        if (raycast_oriented_extents(m->extents, model, r, &dist)) {
            // Hit
            if (!out_result->hits) {
                out_result->hits = darray_create(raycast_hit);
            }

            raycast_hit hit = {0};
            hit.distance = dist;
            hit.type = RAYCAST_HIT_TYPE_OBB;
            hit.position = vec3_add(r->origin, vec3_mul_scalar(r->direction, hit.distance));
            hit.unique_id = m->id.uniqueid;

            darray_push(out_result->hits, hit);
        }
    }

    // Sort the results based on distance.
    if (out_result->hits) {
        b8 swapped;
        u32 length = darray_length(out_result->hits);
        for (u32 i = 0; i < length - 1; ++i) {
            swapped = false;
            for (u32 j = 0; j < length - 1; ++j) {
                if (out_result->hits[j].distance > out_result->hits[j + 1].distance) {
                    KSWAP(raycast_hit, out_result->hits[j], out_result->hits[j + 1]);
                    swapped = true;
                }
            }

            // If no 2 elements were swapped, then sort is complete.
            if (!swapped) {
                break;
            }
        }
    }
    return out_result->hits != 0;
}

b8 scene_directional_light_add(scene *scene, const char *name,
                               struct directional_light *light) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene) {
        return false;
    }

    // TODO: Refactor for multiple lights.
    if (scene->dir_light) {
        light_system_directional_remove(scene->dir_light);
        if (scene->dir_light->debug_data) {
            scene_debug_data *debug = scene->dir_light->debug_data;

            debug_line3d_unload(&debug->line);
            debug_line3d_destroy(&debug->line);

            // NOTE: not freeing here unless there is a light since it will be used again below.
            if (!light) {
                kfree(scene->dir_light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene->dir_light->debug_data = 0;
            }
        }
    }

    scene->dir_light = light;

    if (scene->dir_light) {
        if (!light_system_directional_add(light)) {
            KERROR("scene_add_directional_light - failed to add directional light to light system.");
            return false;
        }

        // Add lines indicating light direction.
        scene_debug_data *debug = scene->dir_light->debug_data;

        // Generate the line points based on the light direction.
        // The first point will always be at the scene's origin.
        vec3 point_0 = vec3_zero();
        vec3 point_1 = vec3_mul_scalar(vec3_normalized(vec3_from_vec4(scene->dir_light->data.direction)), -1.0f);

        if (!debug_line3d_create(point_0, point_1, 0, &debug->line)) {
            KERROR("Failed to create debug line for directional light.");
        } else {
            if (scene->state > SCENE_STATE_INITIALIZED) {
                if (!debug_line3d_initialize(&debug->line)) {
                    KERROR("debug line failed to initialize.");
                    kfree(light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    light->debug_data = 0;
                    return false;
                }
            }

            if (scene->state >= SCENE_STATE_LOADED) {
                if (!debug_line3d_load(&debug->line)) {
                    KERROR("debug line failed to load.");
                    kfree(light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    light->debug_data = 0;
                }
            }
        }
    }

    return true;
}

b8 scene_point_light_add(scene *scene, const char *name,
                         struct point_light *light) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene || !light) {
        return false;
    }

    if (!light_system_point_add(light)) {
        KERROR("Failed to add point light to scene (light system add failure, check logs).");
        return false;
    }

    light->debug_data = kallocate(sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
    scene_debug_data *debug = light->debug_data;

    if (!debug_box3d_create((vec3){0.2f, 0.2f, 0.2f}, 0, &debug->box)) {
        KERROR("Failed to create debug box for directional light.");
    } else {
        transform_position_set(&debug->box.xform, vec3_from_vec4(light->data.position));

        if (scene->state > SCENE_STATE_INITIALIZED) {
            if (!debug_box3d_initialize(&debug->box)) {
                KERROR("debug box failed to initialize.");
                kfree(light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                light->debug_data = 0;
                return false;
            }
        }

        if (scene->state >= SCENE_STATE_LOADED) {
            if (!debug_box3d_load(&debug->box)) {
                KERROR("debug box failed to load.");
                kfree(light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                light->debug_data = 0;
            }
        }
    }

    darray_push(scene->point_lights, light);

    return true;
}

b8 scene_mesh_add(scene *scene, const char *name, struct mesh *m) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene || !m) {
        return false;
    }

    if (scene->state > SCENE_STATE_INITIALIZED) {
        if (!mesh_initialize(m)) {
            KERROR("Mesh failed to initialize.");
            return false;
        }
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        if (!mesh_load(m)) {
            KERROR("Mesh failed to load.");
            return false;
        }
    }

    // TODO: Generate handles/nodes, etc.
    darray_push(scene->meshes, m);

    return true;
}

b8 scene_skybox_add(scene *scene, const char *name,
                    struct skybox *sb) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene) {
        return false;
    }

    // TODO: if one already exists, do we do anything with it?
    scene->sb = sb;
    if (scene->state > SCENE_STATE_INITIALIZED) {
        if (!skybox_initialize(sb)) {
            KERROR("Skybox failed to initialize.");
            scene->sb = 0;
            return false;
        }
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        if (!skybox_load(sb)) {
            KERROR("Skybox failed to load.");
            scene->sb = 0;
            return false;
        }
    }

    return true;
}

b8 scene_terrain_add(scene *scene, const char *name,
                     struct terrain *t) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene || !t) {
        return false;
    }

    if (scene->state > SCENE_STATE_INITIALIZED) {
        if (!terrain_initialize(t)) {
            KERROR("Terrain failed to initialize.");
            return false;
        }
    }

    if (scene->state >= SCENE_STATE_LOADED) {
        if (!terrain_load(t)) {
            KERROR("Terrain failed to load.");
            return false;
        }
    }

    darray_push(scene->terrains, t);

    return true;
}

b8 scene_directional_light_remove(scene *scene,
                                  const char *name) {
    // TODO: This needs to be added via a node w/ attachment(s).
    if (!scene || !name) {
        return false;
    }

    if (!scene->dir_light || !strings_equal(scene->dir_light->name, name)) {
        KWARN(
            "Cannot remove directional light from scene that is not part of the "
            "scene.");
        return false;
    }

    if (!light_system_directional_remove(scene->dir_light)) {
        KERROR("Failed to remove directional light from light system.");
        return false;
    } else {
        // Unload directional light debug if it exists.
        if (scene->dir_light->debug_data) {
            scene_debug_data *debug = scene->dir_light->debug_data;

            debug_line3d_unload(&debug->line);
            debug_line3d_destroy(&debug->line);

            kfree(scene->dir_light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            scene->dir_light->debug_data = 0;
        }
    }

    kfree(scene->dir_light, sizeof(directional_light), MEMORY_TAG_SCENE);
    scene->dir_light = 0;

    return true;
}

b8 scene_point_light_remove(scene *scene, const char *name) {
    if (!scene || !name) {
        return false;
    }

    u32 light_count = darray_length(scene->point_lights);
    for (u32 i = 0; i < light_count; ++i) {
        if (strings_equal(scene->point_lights[i].name, name)) {
            if (!light_system_point_remove(&scene->point_lights[i])) {
                KERROR("Failed to remove point light from light system.");
                return false;
            } else {
                // Destroy debug data if it exists.
                if (scene->point_lights[i].debug_data) {
                    scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;
                    debug_box3d_unload(&debug->box);
                    debug_box3d_destroy(&debug->box);
                    kfree(scene->point_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    scene->point_lights[i].debug_data = 0;
                }
            }

            point_light rubbish = {0};
            darray_pop_at(scene->point_lights, i, &rubbish);

            return true;
        }
    }

    KERROR("Cannot remove a point light from a scene of which it is not a part.");
    return false;
}

b8 scene_mesh_remove(scene *scene, const char *name) {
    if (!scene || !name) {
        return false;
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (strings_equal(scene->meshes[i].name, name)) {
            // Unload any debug data.
            if (scene->meshes[i].debug_data) {
                scene_debug_data *debug = scene->meshes[i].debug_data;

                debug_box3d_unload(&debug->box);
                debug_box3d_destroy(&debug->box);

                kfree(scene->meshes[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene->meshes[i].debug_data = 0;
            }
            // Unload the mesh itself.
            if (!mesh_unload(&scene->meshes[i])) {
                KERROR("Failed to unload mesh");
                return false;
            }

            mesh rubbish = {0};
            darray_pop_at(scene->meshes, i, &rubbish);

            return true;
        }
    }

    KERROR("Cannot remove a mesh from a scene of which it is not a part.");
    return false;
}

b8 scene_skybox_remove(scene *scene, const char *name) {
    if (!scene || !name) {
        return false;
    }

    // TODO: name?
    if (!scene->sb) {
        KWARN("Cannot remove skybox from a scene of which it is not a part.");
        return false;
    }

    scene->sb = 0;

    return true;
}

b8 scene_terrain_remove(scene *scene, const char *name) {
    if (!scene || !name) {
        return false;
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        if (strings_equal(scene->terrains[i].name, name)) {
            if (!terrain_unload(&scene->terrains[i])) {
                KERROR("Failed to unload terrain");
                return false;
            }

            terrain rubbish = {0};
            darray_pop_at(scene->terrains, i, &rubbish);

            return true;
        }
    }

    KERROR("Cannot remove a terrain from a scene of which it is not a part.");
    return false;
}

struct directional_light *
scene_directional_light_get(scene *scene, const char *name) {
    if (!scene) {
        return 0;
    }

    return scene->dir_light;
}

struct point_light *scene_point_light_get(scene *scene,
                                          const char *name) {
    if (!scene) {
        return 0;
    }

    u32 length = darray_length(scene->point_lights);
    for (u32 i = 0; i < length; ++i) {
        if (strings_nequal(name, scene->point_lights[i].name, 256)) {
            return &scene->point_lights[i];
        }
    }

    KWARN("Simple scene does not contain a point light called '%s'.", name);
    return 0;
}

struct mesh *scene_mesh_get(scene *scene, const char *name) {
    if (!scene) {
        return 0;
    }

    u32 length = darray_length(scene->meshes);
    for (u32 i = 0; i < length; ++i) {
        if (strings_nequal(name, scene->meshes[i].name, 256)) {
            return &scene->meshes[i];
        }
    }

    KWARN("Simple scene does not contain a mesh called '%s'.", name);
    return 0;
}

struct skybox *scene_skybox_get(scene *scene, const char *name) {
    if (!scene) {
        return 0;
    }

    return scene->sb;
}

struct terrain *scene_terrain_get(scene *scene,
                                  const char *name) {
    if (!scene || !name) {
        return 0;
    }

    u32 length = darray_length(scene->terrains);
    for (u32 i = 0; i < length; ++i) {
        if (strings_nequal(name, scene->terrains[i].name, 256)) {
            return &scene->terrains[i];
        }
    }

    KWARN("Simple scene does not contain a terrain called '%s'.", name);
    return 0;
}

b8 scene_debug_render_data_query(scene *scene, u32 *data_count, geometry_render_data **debug_geometries) {
    if (!scene || !data_count) {
        return false;
    }

    *data_count = 0;

    // TODO: Check if grid exists.
    {
        if (debug_geometries) {
            geometry_render_data data = {0};
            data.model = mat4_identity();

            geometry *g = &scene->grid.geo;
            data.material = g->material;
            data.vertex_count = g->vertex_count;
            data.vertex_buffer_offset = g->vertex_buffer_offset;
            data.index_count = g->index_count;
            data.index_buffer_offset = g->index_buffer_offset;
            data.unique_id = INVALID_ID;

            (*debug_geometries)[(*data_count)] = data;
        }
        (*data_count)++;
    }

    // Directional light.
    {
        if (scene->dir_light && scene->dir_light->debug_data) {
            if (debug_geometries) {
                scene_debug_data *debug = scene->dir_light->debug_data;

                // Debug line 3d
                geometry_render_data data = {0};
                data.model = transform_world_get(&debug->line.xform);
                geometry *g = &debug->line.geo;
                data.material = g->material;
                data.vertex_count = g->vertex_count;
                data.vertex_buffer_offset = g->vertex_buffer_offset;
                data.index_count = g->index_count;
                data.index_buffer_offset = g->index_buffer_offset;
                data.unique_id = debug->line.id.uniqueid;

                (*debug_geometries)[(*data_count)] = data;
            }
            (*data_count)++;
        }
    }

    // Point lights
    {
        u32 point_light_count = darray_length(scene->point_lights);
        for (u32 i = 0; i < point_light_count; ++i) {
            if (scene->point_lights[i].debug_data) {
                if (debug_geometries) {
                    scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;

                    // Debug box 3d
                    geometry_render_data data = {0};
                    data.model = transform_world_get(&debug->box.xform);
                    geometry *g = &debug->box.geo;
                    data.material = g->material;
                    data.vertex_count = g->vertex_count;
                    data.vertex_buffer_offset = g->vertex_buffer_offset;
                    data.index_count = g->index_count;
                    data.index_buffer_offset = g->index_buffer_offset;
                    data.unique_id = debug->box.id.uniqueid;

                    (*debug_geometries)[(*data_count)] = data;
                }
                (*data_count)++;
            }
        }
    }

    // Mesh debug shapes
    {
        u32 mesh_count = darray_length(scene->meshes);
        for (u32 i = 0; i < mesh_count; ++i) {
            if (scene->meshes[i].debug_data) {
                if (debug_geometries) {
                    scene_debug_data *debug = (scene_debug_data *)scene->meshes[i].debug_data;

                    // Debug box 3d
                    geometry_render_data data = {0};
                    data.model = transform_world_get(&debug->box.xform);
                    geometry *g = &debug->box.geo;
                    data.material = g->material;
                    data.vertex_count = g->vertex_count;
                    data.vertex_buffer_offset = g->vertex_buffer_offset;
                    data.index_count = g->index_count;
                    data.index_buffer_offset = g->index_buffer_offset;
                    data.unique_id = debug->box.id.uniqueid;

                    (*debug_geometries)[(*data_count)] = data;
                }
                (*data_count)++;
            }
        }
    }

    return true;
}

b8 scene_mesh_render_data_query_from_line(const scene *scene, vec3 direction, vec3 center, f32 radius, frame_data *p_frame_data, u32 *out_count, struct geometry_render_data **out_geometries) {
    if (!scene) {
        return false;
    }

    geometry_distance *transparent_geometries = darray_create_with_allocator(geometry_distance, &p_frame_data->allocator);

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        mesh *m = &scene->meshes[i];
        if (m->generation != INVALID_ID_U8) {
            mat4 model = transform_world_get(&m->transform);
            b8 winding_inverted = m->transform.determinant < 0;

            for (u32 j = 0; j < m->geometry_count; ++j) {
                geometry *g = m->geometries[j];

                // TODO: cache this somewhere...
                //
                // Translate/scale the extents.
                vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                vec3 extents_max = vec3_mul_mat4(g->extents.max, model);
                // Translate/scale the center.
                vec3 transformed_center = vec3_mul_mat4(g->center, model);
                // Find the one furthest from the center.
                f32 mesh_radius = KMAX(vec3_distance(extents_min, transformed_center), vec3_distance(extents_max, transformed_center));

                f32 dist_to_line = vec3_distance_to_line(transformed_center, center, direction);

                // Is within distance, so include it
                if ((dist_to_line - mesh_radius) <= radius) {
                    // Add it to the list to be rendered.
                    geometry_render_data data = {0};
                    data.model = model;
                    data.material = g->material;
                    data.vertex_count = g->vertex_count;
                    data.vertex_buffer_offset = g->vertex_buffer_offset;
                    data.index_count = g->index_count;
                    data.index_buffer_offset = g->index_buffer_offset;
                    data.unique_id = m->id.uniqueid;
                    data.winding_inverted = winding_inverted;

                    // Check if transparent. If so, put into a separate, temp array to be
                    // sorted by distance from the camera. Otherwise, put into the
                    // ext_data->geometries array directly.
                    b8 has_transparency = false;
                    if (g->material->type == MATERIAL_TYPE_PBR) {
                        // Check diffuse map (slot 0).
                        has_transparency = ((g->material->maps[0].texture->flags & TEXTURE_FLAG_HAS_TRANSPARENCY) != 0);
                    }

                    if (has_transparency) {
                        // For meshes _with_ transparency, add them to a separate list to be sorted by distance later.
                        // Get the center, extract the global position from the model matrix and add it to the center,
                        // then calculate the distance between it and the camera, and finally save it to a list to be sorted.
                        // NOTE: This isn't perfect for translucent meshes that intersect, but is enough for our purposes now.
                        vec3 geometry_center = vec3_transform(g->center, 1.0f, model);
                        f32 distance = vec3_distance(geometry_center, center);

                        geometry_distance gdist;
                        gdist.distance = kabs(distance);
                        gdist.g = data;
                        darray_push(transparent_geometries, gdist);
                    } else {
                        darray_push(*out_geometries, data);
                    }
                    p_frame_data->drawn_mesh_count++;
                }
            }
        }
    }

    // Sort opaque geometries by material.
    kquick_sort(sizeof(geometry_render_data), *out_geometries, 0, darray_length(*out_geometries) - 1, geometry_render_data_compare);

    // Sort transparent geometries, then add them to the ext_data->geometries array.
    u32 transparent_geometry_count = darray_length(transparent_geometries);
    kquick_sort(sizeof(geometry_distance), transparent_geometries, 0, transparent_geometry_count - 1, geometry_distance_compare);
    for (u32 i = 0; i < transparent_geometry_count; ++i) {
        darray_push(*out_geometries, transparent_geometries[i].g);
    }

    *out_count = darray_length(*out_geometries);

    return true;
}

b8 scene_terrain_render_data_query_from_line(const scene *scene, vec3 direction, vec3 center, f32 radius, struct frame_data *p_frame_data, u32 *out_count, struct geometry_render_data **out_geometries) {
    if (!scene) {
        return false;
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        terrain *t = &scene->terrains[i];
        mat4 model = transform_world_get(&t->xform);
        b8 winding_inverted = t->xform.determinant < 0;

        // Check each chunk to see if it is in view.
        for (u32 c = 0; c < t->chunk_count; ++c) {
            terrain_chunk *chunk = &t->chunks[c];

            if (chunk->generation != INVALID_ID_U16) {
                // TODO: cache this somewhere...
                //
                // Translate/scale the extents.
                vec3 extents_min = vec3_mul_mat4(chunk->extents.min, model);
                vec3 extents_max = vec3_mul_mat4(chunk->extents.max, model);
                // Translate/scale the center.
                vec3 transformed_center = vec3_mul_mat4(chunk->center, model);
                // Find the one furthest from the center.
                f32 mesh_radius = KMAX(vec3_distance(extents_min, transformed_center), vec3_distance(extents_max, transformed_center));

                f32 dist_to_line = vec3_distance_to_line(transformed_center, center, direction);

                // Is within distance, so include it
                if ((dist_to_line - mesh_radius) <= radius) {
                    // Add it to the list to be rendered.
                    geometry_render_data data = {0};
                    data.model = model;
                    data.material = chunk->material;
                    data.vertex_count = chunk->total_vertex_count;
                    data.vertex_buffer_offset = chunk->vertex_buffer_offset;

                    // Use the indices for the current LOD.
                    data.index_count = chunk->lods[chunk->current_lod].total_index_count;
                    data.index_buffer_offset = chunk->lods[chunk->current_lod].index_buffer_offset;
                    data.index_element_size = sizeof(u32);
                    data.unique_id = t->id.uniqueid;
                    data.winding_inverted = winding_inverted;

                    darray_push(*out_geometries, data);
                }
            }
        }
    }

    *out_count = darray_length(*out_geometries);

    return true;
}

b8 scene_mesh_render_data_query(const scene *scene, const frustum *f, vec3 center, frame_data *p_frame_data, u32 *out_count, struct geometry_render_data **out_geometries) {
    if (!scene) {
        return false;
    }

    geometry_distance *transparent_geometries = darray_create_with_allocator(geometry_distance, &p_frame_data->allocator);

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        mesh *m = &scene->meshes[i];
        if (m->generation != INVALID_ID_U8) {
            mat4 model = transform_world_get(&m->transform);
            b8 winding_inverted = m->transform.determinant < 0;

            for (u32 j = 0; j < m->geometry_count; ++j) {
                geometry *g = m->geometries[j];

                // TODO: Distance-from-line detection per object (e.g. light direction and center pos, then distance check from that line.)
                //
                // // Bounding sphere calculation.
                // {
                //     // Translate/scale the extents.
                //     vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                //     vec3 extents_max = vec3_mul_mat4(g->extents.max, model);

                //     f32 min = KMIN(KMIN(extents_min.x, extents_min.y),
                //     extents_min.z); f32 max = KMAX(KMAX(extents_max.x,
                //     extents_max.y), extents_max.z); f32 diff = kabs(max - min);
                //     f32 radius = diff * 0.5f;

                //     // Translate/scale the center.
                //     vec3 center = vec3_mul_mat4(g->center, model);

                //     if (frustum_intersects_sphere(&state->camera_frustum,
                //     &center, radius)) {
                //         // Add it to the list to be rendered.
                //         geometry_render_data data = {0};
                //         data.model = model;
                //         data.geometry = g;
                //         data.unique_id = m->unique_id;
                //         darray_push(game_inst->frame_data.world_geometries,
                //         data);

                //         draw_count++;
                //     }
                // }

                // AABB calculation
                {
                    // Translate/scale the extents.
                    // vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                    vec3 extents_max = vec3_mul_mat4(g->extents.max, model);

                    // Translate/scale the center.
                    vec3 g_center = vec3_mul_mat4(g->center, model);
                    vec3 half_extents = {
                        kabs(extents_max.x - g_center.x),
                        kabs(extents_max.y - g_center.y),
                        kabs(extents_max.z - g_center.z),
                    };

                    if (!f || frustum_intersects_aabb(f, &g_center, &half_extents)) {
                        // Add it to the list to be rendered.
                        geometry_render_data data = {0};
                        data.model = model;
                        data.material = g->material;
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;
                        data.unique_id = m->id.uniqueid;
                        data.winding_inverted = winding_inverted;

                        // Check if transparent. If so, put into a separate, temp array to be
                        // sorted by distance from the camera. Otherwise, put into the
                        // ext_data->geometries array directly.
                        b8 has_transparency = false;
                        if (g->material->type == MATERIAL_TYPE_PBR) {
                            // Check diffuse map (slot 0).
                            has_transparency = ((g->material->maps[0].texture->flags & TEXTURE_FLAG_HAS_TRANSPARENCY) != 0);
                        }

                        if (has_transparency) {
                            // For meshes _with_ transparency, add them to a separate list to be sorted by distance later.
                            // Get the center, extract the global position from the model matrix and add it to the center,
                            // then calculate the distance between it and the camera, and finally save it to a list to be sorted.
                            // NOTE: This isn't perfect for translucent meshes that intersect, but is enough for our purposes now.
                            f32 distance = vec3_distance(g_center, center);

                            geometry_distance gdist;
                            gdist.distance = kabs(distance);
                            gdist.g = data;
                            darray_push(transparent_geometries, gdist);
                        } else {
                            darray_push(*out_geometries, data);
                        }
                        p_frame_data->drawn_mesh_count++;
                    }
                }
            }
        }
    }

    // Sort opaque geometries by material.
    kquick_sort(sizeof(geometry_render_data), *out_geometries, 0, darray_length(*out_geometries) - 1, geometry_render_data_compare);

    // Sort transparent geometries, then add them to the ext_data->geometries array.
    u32 transparent_geometry_count = darray_length(transparent_geometries);
    kquick_sort(sizeof(geometry_distance), transparent_geometries, 0, transparent_geometry_count - 1, geometry_distance_compare);
    for (u32 i = 0; i < transparent_geometry_count; ++i) {
        darray_push(*out_geometries, transparent_geometries[i].g);
    }

    *out_count = darray_length(*out_geometries);

    return true;
}

b8 scene_terrain_render_data_query(const scene *scene, const frustum *f, vec3 center, frame_data *p_frame_data, u32 *out_count, struct geometry_render_data **out_terrain_geometries) {
    if (!scene) {
        return false;
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        terrain *t = &scene->terrains[i];
        mat4 model = transform_world_get(&t->xform);
        b8 winding_inverted = t->xform.determinant < 0;

        // Check each chunk to see if it is in view.
        for (u32 c = 0; c < t->chunk_count; c++) {
            terrain_chunk *chunk = &t->chunks[c];

            if (chunk->generation != INVALID_ID_U16) {
                // AABB calculation
                vec3 g_center, half_extents;

                if (f) {
                    // TODO: cache this somewhere...
                    //
                    // Translate/scale the extents.
                    // vec3 extents_min = vec3_mul_mat4(g->extents.min, model);
                    vec3 extents_max = vec3_mul_mat4(chunk->extents.max, model);

                    // Translate/scale the center.
                    g_center = vec3_mul_mat4(chunk->center, model);
                    half_extents = (vec3){
                        kabs(extents_max.x - g_center.x),
                        kabs(extents_max.y - g_center.y),
                        kabs(extents_max.z - g_center.z),
                    };
                }

                if (!f || frustum_intersects_aabb(f, &g_center, &half_extents)) {
                    geometry_render_data data = {0};
                    data.model = model;
                    data.material = chunk->material;
                    data.vertex_count = chunk->total_vertex_count;
                    data.vertex_buffer_offset = chunk->vertex_buffer_offset;
                    data.vertex_element_size = sizeof(terrain_vertex);

                    // Use the indices for the current LOD.
                    data.index_count = chunk->lods[chunk->current_lod].total_index_count;
                    data.index_buffer_offset = chunk->lods[chunk->current_lod].index_buffer_offset;
                    data.index_element_size = sizeof(u32);
                    data.unique_id = t->id.uniqueid;
                    data.winding_inverted = winding_inverted;

                    darray_push(*out_terrain_geometries, data);
                }
            }
        }
    }

    *out_count = darray_length(*out_terrain_geometries);

    return true;
}

static void scene_actual_unload(scene *scene) {
    if (scene->sb) {
        if (!skybox_unload(scene->sb)) {
            KERROR("Failed to unload skybox");
        }
        skybox_destroy(scene->sb);
        scene->sb = 0;
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (scene->meshes[i].generation != INVALID_ID_U8) {
            // Unload any debug data.
            if (scene->meshes[i].debug_data) {
                scene_debug_data *debug = scene->meshes[i].debug_data;

                debug_box3d_unload(&debug->box);
                debug_box3d_destroy(&debug->box);

                kfree(scene->meshes[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene->meshes[i].debug_data = 0;
            }

            // Unload the mesh itself
            if (!mesh_unload(&scene->meshes[i])) {
                KERROR("Failed to unload mesh.");
            }
            mesh_destroy(&scene->meshes[i]);
        }
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        if (!terrain_unload(&scene->terrains[i])) {
            KERROR("Failed to unload terrain.");
        }
        terrain_destroy(&scene->terrains[i]);
    }

    // Debug grid.
    if (!debug_grid_unload(&scene->grid)) {
        KWARN("Debug grid unload failed.");
    }

    if (scene->dir_light) {
        if (!scene_directional_light_remove(scene, scene->dir_light->name)) {
            KERROR("Failed to unload/remove directional light.");
        }

        if (scene->dir_light && scene->dir_light->debug_data) {
            scene_debug_data *debug = (scene_debug_data *)scene->dir_light->debug_data;
            // Unload directional light line data.
            debug_line3d_unload(&debug->line);
            debug_line3d_destroy(&debug->line);
            kfree(scene->dir_light->debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            scene->dir_light->debug_data = 0;
        }
    }

    u32 p_light_count = darray_length(scene->point_lights);
    for (u32 i = 0; i < p_light_count; ++i) {
        if (!light_system_point_remove(&scene->point_lights[i])) {
            KWARN("Failed to remove point light from light system.");
        }

        // Destroy debug data if it exists.
        if (scene->point_lights[i].debug_data) {
            scene_debug_data *debug = (scene_debug_data *)scene->point_lights[i].debug_data;
            debug_box3d_unload(&debug->box);
            debug_box3d_destroy(&debug->box);
            kfree(scene->point_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            scene->point_lights[i].debug_data = 0;
        }
    }

    // Update the state to show the scene is initialized.
    scene->state = SCENE_STATE_UNLOADED;

    // Also destroy the scene.
    scene->dir_light = 0;
    scene->sb = 0;

    if (scene->point_lights) {
        darray_destroy(scene->point_lights);
    }

    if (scene->meshes) {
        darray_destroy(scene->meshes);
    }

    if (scene->terrains) {
        darray_destroy(scene->terrains);
    }

    kzero_memory(scene, sizeof(scene));
}

struct transform *scene_transform_get_by_id(scene *scene, u64 unique_id) {
    if (!scene) {
        return 0;
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (scene->meshes[i].id.uniqueid == unique_id) {
            return &scene->meshes[i].transform;
        }
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        if (scene->terrains[i].id.uniqueid == unique_id) {
            return &scene->terrains[i].xform;
        }
    }

    return 0;
}

b8 scene_save(scene *scene) {
    if (!scene) {
        KERROR("scene_save requires a valid pointer to a scene.");
        return false;
    }

    // Create a simple scene config based on the objects currently in the scene.
    scene_config config = {0};
    config.name = string_duplicate(scene->name);
    config.description = string_duplicate(scene->description);
    if (scene->sb) {
        config.skybox_config.name = string_duplicate(scene->config->skybox_config.name);
        config.skybox_config.cubemap_name = string_duplicate(scene->config->skybox_config.cubemap_name);
    }
    if (scene->dir_light) {
        config.directional_light_config.name = string_duplicate(typed_attachment->name);
        config.directional_light_config.colour = scene->dir_light->data.colour;
        config.directional_light_config.direction = scene->dir_light->data.direction;
        config.directional_light_config.shadow_split_mult = scene->dir_light->data.shadow_split_mult;
        config.directional_light_config.shadow_fade_distance = scene->dir_light->data.shadow_fade_distance;
        config.directional_light_config.shadow_distance = scene->dir_light->data.shadow_distance;
    }

    u32 mesh_count = darray_length(scene->meshes);
    config.meshes = darray_create(mesh_scene_config);
    for (u32 i = 0; i < mesh_count; ++i) {
        mesh_scene_config mesh = {0};
        mesh.name = string_duplicate(scene->meshes[i].name);
        mesh.transform = scene->meshes[i].transform;
        mesh.resource_name = scene->meshes[i].config.resource_name;
        // TODO: Parent could have changed... Re-walk the tree and see what the parent name is
        // But, the parent info isn't saved at that time, only transform parenting is done on load.
        // Rethink parenting and how that is stored.
        mesh.parent_name = scene->meshes[i].config.parent_name;
        darray_push(config.meshes, mesh);
    }
    u32 terrain_count = darray_length(scene->terrains);
    config.terrains = darray_create(terrain_scene_config);
    for (u32 i = 0; i < terrain_count; ++i) {
        terrain_scene_config terrain = {0};
        terrain.name = string_duplicate(scene->terrains[i].name);
        terrain.xform = scene->terrains[i].xform;
        // TODO: same issue as above.
        /* terrain.resource_name = scene->terrains[i].terrain.parent_name = scene->meshes[i].config.parent_name; */
        darray_push(config.meshes, terrain);
    }

    // Call the resource system to write that config.

    // Destroy the config.

    return true;
}

static scene_attachment *scene_attachment_acquire(scene *s) {
    if (s) {
        u32 attachment_count = darray_length(s->attachments);
        for (u32 i = 0; i < attachment_count; ++i) {
            if (!k_handle_is_invalid(s->attachments[i].hierarchy_node_handle)) {
                // Found one.
                return &s->attachments[i];
            }
        }

        // No more space, push a new one and return it.
        scene_attachment new_attachment = {0};
        darray_push(s->attachments, new_attachment);
        return &s->attachments[attachment_count];
    }
    KERROR("scene_attachment_acquire requires a valid pointer to a scene.");
    return 0;
}

static void scene_attachment_release(scene *s, scene_attachment *attachment) {
    if (s && attachment) {
        // Look up the attachment type and release the attachment itself.
        switch (attachment->attachment_type) {
            case SCENE_NODE_ATTACHMENT_TYPE_SKYBOX:
                skybox_destroy(&s->skyboxes[attachment->resource_handle.handle_index]);
                break;
            case SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH:
                // TODO: destroy this
                break;
            case SCENE_NODE_ATTACHMENT_TYPE_TERRAIN:
                // TODO: destroy this
                break;
            case SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT:
                // TODO: destroy this
                break;
            case SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT:
                // TODO: destroy this
                break;
            case SCENE_NODE_ATTACHMENT_TYPE_
