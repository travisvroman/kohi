#include "scene.h"

#include <core_resource_types.h>

#include "assets/kasset_types.h"
#include "audio/audio_frontend.h"
#include "audio/kaudio_types.h"
#include "containers/darray.h"
#include "core/console.h"
#include "core/engine.h"
#include "core/frame_data.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "graphs/hierarchy_graph.h"
#include "identifiers/identifier.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/geometry_3d.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_types.h"
#include "resources/debug/debug_box3d.h"
#include "resources/debug/debug_line3d.h"
#include "resources/debug/debug_sphere3d.h"
#include "resources/skybox.h"
#include "resources/terrain.h"
#include "resources/water_plane.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "strings/kstring_id.h"
#include "systems/asset_system.h"
#include "systems/kmaterial_system.h"
#include "systems/ktransform_system.h"
#include "systems/light_system.h"
#include "systems/static_mesh_system.h"
#include "utils/ksort.h"

static void scene_actual_unload(scene* scene);
static void scene_node_metadata_ensure_allocated(scene* s, khierarchy_node node);

static u32 global_scene_id = 0;
static kmaterial_instance default_material;

typedef struct scene_debug_data {
    debug_box3d box;
    debug_line3d line;
    debug_sphere3d sphere;
} scene_debug_data;

typedef struct scene_audio_emitter {
    kname name;

    // Handle to the emitter within the audio system.
    kaudio_emitter emitter;

    // debug rendering data.
    scene_debug_data* debug_data;
    u32 generation;
    kname resource_name;
    kname package_name;
    b8 is_streaming;
} scene_audio_emitter;

typedef struct scene_volume {
    scene_volume_type type;
    scene_volume_shape_type shape_type;

    union {
        f32 radius;
        vec3 extents;
    } shape_config;

    const char* on_enter_command;
    const char* on_leave_command;
    const char* on_update_command;

    u32 hit_sphere_tag_count;
    kname* hit_sphere_tags;

    // debug rendering data.
    scene_debug_data* debug_data;

    // darray of indices into the scene's hit_spheres array.
    u32* hit_sphere_indices;
} scene_volume;

typedef struct scene_hit_sphere {
    f32 radius;

    // debug rendering data.
    scene_debug_data* debug_data;
} scene_hit_sphere;

/** @brief A private structure used to sort geometry by distance from the camera. */
typedef struct geometry_distance {
    /** @brief The geometry render data. */
    geometry_render_data g;
    /** @brief The distance from the camera. */
    f32 distance;
} geometry_distance;

static i32 geometry_render_data_compare(void* a, void* b) {
    geometry_render_data* a_typed = a;
    geometry_render_data* b_typed = b;
    return a_typed->material.base_material - b_typed->material.base_material;
}

static i32 geometry_distance_compare(void* a, void* b) {
    geometry_distance* a_typed = a;
    geometry_distance* b_typed = b;
    if (a_typed->distance > b_typed->distance) {
        return 1;
    } else if (a_typed->distance < b_typed->distance) {
        return -1;
    }
    return 0;
}

b8 scene_create(kasset_scene* config, scene_flags flags, scene* out_scene) {
    if (!out_scene) {
        KERROR("scene_create(): A valid pointer to out_scene is required.");
        return false;
    }

    // NOTE: If not done already, take an instance of the default material.
    if (!default_material.instance_id) {
        default_material = kmaterial_system_get_default_standard(engine_systems_get()->material_system);
    }

    kzero_memory(out_scene, sizeof(scene));

    out_scene->flags = flags;
    out_scene->enabled = false;
    out_scene->state = SCENE_STATE_UNINITIALIZED;
    global_scene_id++;
    out_scene->id = global_scene_id;

    // Internal "lists" of renderable objects.
    out_scene->dir_lights = darray_create(directional_light);
    out_scene->point_lights = darray_create(point_light);
    out_scene->audio_emitters = darray_create(scene_audio_emitter);
    out_scene->static_meshes = darray_create(kstatic_mesh_instance);
    out_scene->terrains = darray_create(terrain);
    out_scene->skyboxes = darray_create(skybox);
    out_scene->water_planes = darray_create(water_plane);
    out_scene->volumes = darray_create(scene_volume);
    out_scene->hit_spheres = darray_create(scene_hit_sphere);

    // Internal lists of attachments.
    /* out_scene->attachments = darray_create(scene_attachment); */
    out_scene->mesh_attachments = darray_create(scene_attachment);
    out_scene->terrain_attachments = darray_create(scene_attachment);
    out_scene->skybox_attachments = darray_create(scene_attachment);
    out_scene->directional_light_attachments = darray_create(scene_attachment);
    out_scene->point_light_attachments = darray_create(scene_attachment);
    out_scene->audio_emitter_attachments = darray_create(scene_attachment);
    out_scene->water_plane_attachments = darray_create(scene_attachment);
    out_scene->volume_attachments = darray_create(scene_attachment);
    out_scene->hit_sphere_attachments = darray_create(scene_attachment);

    b8 is_readonly = ((out_scene->flags & SCENE_FLAG_READONLY) != 0);
    if (!is_readonly) {
        out_scene->mesh_metadata = darray_create(scene_static_mesh_metadata);
        out_scene->terrain_metadata = darray_create(scene_terrain_metadata);
        out_scene->skybox_metadata = darray_create(scene_skybox_metadata);
        out_scene->water_plane_metadata = darray_create(scene_water_plane_metadata);
    }

    if (!hierarchy_graph_create(&out_scene->hierarchy, 0)) {
        KERROR("Failed to create hierarchy graph");
        return false;
    }

    if (config) {
        out_scene->config = config;
    }

    debug_grid_config grid_config = {0};
    grid_config.orientation = GRID_ORIENTATION_XZ;
    grid_config.segment_count_dim_0 = 100;
    grid_config.segment_count_dim_1 = 100;
    grid_config.segment_size = 1.0f;
    grid_config.name = kname_create("debug_grid");
    grid_config.use_third_axis = true;

    if (!debug_grid_create(&grid_config, &out_scene->grid)) {
        return false;
    }

    return true;
}

void scene_destroy(scene* s) {
    if (s) {
        if (s->state == SCENE_STATE_LOADED) {
            scene_actual_unload(s);
        }

        // Also destroy the scene.
        if (s->skyboxes) {
            darray_destroy(s->skyboxes);
        }
        if (s->skybox_attachments) {
            darray_destroy(s->skybox_attachments);
        }
        if (s->skybox_metadata) {
            darray_destroy(s->skybox_metadata);
        }

        if (s->dir_lights) {
            darray_destroy(s->dir_lights);
        }
        if (s->directional_light_attachments) {
            darray_destroy(s->directional_light_attachments);
        }

        if (s->point_lights) {
            darray_destroy(s->point_lights);
        }
        if (s->point_light_attachments) {
            darray_destroy(s->point_light_attachments);
        }

        if (s->audio_emitters) {
            darray_destroy(s->audio_emitters);
        }
        if (s->audio_emitter_attachments) {
            darray_destroy(s->audio_emitter_attachments);
        }

        if (s->static_meshes) {
            darray_destroy(s->static_meshes);
        }
        if (s->mesh_attachments) {
            darray_destroy(s->mesh_attachments);
        }
        if (s->mesh_metadata) {
            darray_destroy(s->mesh_metadata);
        }

        if (s->terrains) {
            darray_destroy(s->terrains);
        }
        if (s->terrain_attachments) {
            darray_destroy(s->terrain_attachments);
        }
        if (s->terrain_metadata) {
            darray_destroy(s->terrain_metadata);
        }

        if (s->water_planes) {
            darray_destroy(s->water_planes);
        }
        if (s->water_plane_attachments) {
            darray_destroy(s->water_plane_attachments);
        }
        if (s->water_plane_metadata) {
            darray_destroy(s->water_plane_metadata);
        }

        if (s->volumes) {
            darray_destroy(s->volumes);
        }
        if (s->volume_attachments) {
            darray_destroy(s->volume_attachments);
        }

        if (s->hit_spheres) {
            darray_destroy(s->hit_spheres);
        }
        if (s->hit_sphere_attachments) {
            darray_destroy(s->hit_sphere_attachments);
        }

        kzero_memory(s, sizeof(scene));

        s->state = SCENE_STATE_UNINITIALIZED;
    }
}

void scene_node_initialize(scene* s, khierarchy_node parent_handle, scene_node_config* node_config) {
    if (node_config) {
        b8 is_readonly = ((s->flags & SCENE_FLAG_READONLY) != 0);

        // Obtain the ktransform if one is configured.
        ktransform ktransform_handle = {0};
        if (node_config->ktransform_source) {
            if (!ktransform_from_string(node_config->ktransform_source, &ktransform_handle)) {
                KWARN("Failed to create ktransform. See logs for details.");
            }
        } else {
            ktransform_handle = KTRANSFORM_INVALID;
        }

        // Add a node in the heirarchy.
        khierarchy_node hierarchy_node_handle = hierarchy_graph_child_add_with_ktransform(&s->hierarchy, parent_handle, ktransform_handle);

        if (!is_readonly) {
            scene_node_metadata_ensure_allocated(s, hierarchy_node_handle);
            if (node_config->name) {
                scene_node_metadata* m = &s->node_metadata[hierarchy_node_handle];
                m->index = hierarchy_node_handle;
                m->name = node_config->name;
            }
        }

        // Process attachment configs by type.

        // Skyboxes
        if (node_config->skybox_configs) {
            u32 count = darray_length(node_config->skybox_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_skybox_config* typed_attachment_config = &node_config->skybox_configs[i];

                // Create a skybox config and use it to create the skybox.
                skybox_config sb_config = {0};
                sb_config.cubemap_name = typed_attachment_config->cubemap_image_asset_name;
                skybox sb;
                if (!skybox_create(sb_config, &sb)) {
                    KWARN("Failed to create skybox.");
                }

                // Initialize the skybox.
                if (!skybox_initialize(&sb)) {
                    KERROR("Failed to initialize skybox. See logs for details.");
                } else {
                    // Find a free skybox slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 skybox_count = darray_length(s->skyboxes);
                    for (u32 sbi = 0; sbi < skybox_count; ++sbi) {
                        if (s->skyboxes[sbi].state == SKYBOX_STATE_UNDEFINED) {
                            // Found a slot, use it.
                            index = sbi;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->skyboxes, (skybox){0});
                        darray_push(s->skybox_attachments, (scene_attachment){0});
                        if (!is_readonly) {
                            darray_push(s->skybox_metadata, (scene_skybox_metadata){0});
                        }

                        index = skybox_count;
                    }

                    // Fill out the structs.
                    s->skyboxes[index] = sb;

                    scene_attachment* attachment = &s->skybox_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_SKYBOX;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // For "edit" mode, retain metadata.
                    if (!is_readonly) {
                        scene_skybox_metadata* meta = &s->skybox_metadata[index];
                        meta->cubemap_name = typed_attachment_config->cubemap_image_asset_name;
                        meta->package_name = typed_attachment_config->cubemap_image_asset_package_name;
                    }
                }
            }
        }

        // Directional lights
        if (node_config->dir_light_configs) {
            u32 count = darray_length(node_config->dir_light_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_directional_light_config* typed_attachment_config = &node_config->dir_light_configs[i];

                directional_light new_light = {0};
                new_light.name = typed_attachment_config->base.name;
                new_light.data.colour = typed_attachment_config->colour;
                new_light.data.direction = typed_attachment_config->direction;
                new_light.data.shadow_distance = typed_attachment_config->shadow_distance;
                new_light.data.shadow_fade_distance = typed_attachment_config->shadow_fade_distance;
                new_light.data.shadow_split_mult = typed_attachment_config->shadow_split_mult;
                new_light.generation = 0;

                // Add debug data and initialize it.
                new_light.debug_data = KALLOC_TYPE(scene_debug_data, MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = new_light.debug_data;

                // Generate the line points based on the light direction.
                // The first point will always be at the scene's origin.
                vec3 point_0 = vec3_zero();
                vec3 point_1 = vec3_mul_scalar(vec3_normalized(vec3_from_vec4(new_light.data.direction)), -1.0f);

                if (!debug_line3d_create(point_0, point_1, KTRANSFORM_INVALID, &debug->line)) {
                    KERROR("Failed to create debug line for directional light.");
                }
                if (!debug_line3d_initialize(&debug->line)) {
                    KERROR("Failed to create debug line for directional light.");
                } else {
                    // Find a free slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 directional_light_count = darray_length(s->dir_lights);
                    for (u32 dli = 0; dli < directional_light_count; ++dli) {
                        if (s->dir_lights[dli].generation == INVALID_ID) {
                            // Found a slot, use it.
                            index = dli;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->dir_lights, (directional_light){0});
                        darray_push(s->directional_light_attachments, (scene_attachment){0});
                        index = directional_light_count;
                    }

                    s->dir_lights[index] = new_light;

                    scene_attachment* attachment = &s->directional_light_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // NOTE: These dont have metadata. If metadata is required, get it here.
                    /* if (!is_readonly) {
                        scene_directional_light_metadata* meta = 0;
                    } */
                }
            }
        }

        // Point lights
        if (node_config->point_light_configs) {
            u32 count = darray_length(node_config->point_light_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_point_light_config* typed_attachment_config = &node_config->point_light_configs[i];

                point_light new_light = {0};
                new_light.name = typed_attachment_config->base.name;
                new_light.data.colour = typed_attachment_config->colour;
                new_light.data.position = typed_attachment_config->position;
                new_light.data.constant_f = typed_attachment_config->constant_f;
                new_light.data.linear = typed_attachment_config->linear;
                new_light.data.quadratic = typed_attachment_config->quadratic;
                new_light.generation = 0;

                // Add debug data and initialize it.
                new_light.debug_data = KALLOC_TYPE(scene_debug_data, MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = new_light.debug_data;

                if (!debug_box3d_create((vec3){0.2f, 0.2f, 0.2f}, KTRANSFORM_INVALID, &debug->box)) {
                    KERROR("Failed to create debug box for point light.");
                } else {
                    ktransform_position_set(debug->box.ktransform, vec3_from_vec4(new_light.data.position));
                }
                if (!debug_box3d_initialize(&debug->box)) {
                    KERROR("Failed to create debug box for point light.");
                } else {
                    // Find a free slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 point_light_count = darray_length(s->point_lights);
                    for (u32 dli = 0; dli < point_light_count; ++dli) {
                        if (s->point_lights[dli].generation == INVALID_ID) {
                            // Found a slot, use it.
                            index = dli;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->point_lights, (point_light){0});
                        darray_push(s->point_light_attachments, (scene_attachment){0});
                        index = point_light_count;
                    }

                    s->point_lights[index] = new_light;

                    scene_attachment* attachment = &s->point_light_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // NOTE: These dont have metadata. If metadata is required, get it here.
                    /* if (!is_readonly) {
                        scene_point_light_metadata* meta = 0;
                    } */
                }
            }
        }

        // Audio emitters
        if (node_config->audio_emitter_configs) {
            u32 count = darray_length(node_config->audio_emitter_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_audio_emitter_config* typed_attachment_config = &node_config->audio_emitter_configs[i];

                /* vec3 pos = vec3_zero();
                if (khandle_is_valid(ktransform_handle)) {
                    pos = ktransform_position_get(ktransform_handle);
                } */

                scene_audio_emitter new_emitter = {0};
                new_emitter.name = typed_attachment_config->base.name;
                if (!kaudio_emitter_create(
                        engine_systems_get()->audio_system,
                        typed_attachment_config->inner_radius,
                        typed_attachment_config->outer_radius,
                        typed_attachment_config->volume,
                        typed_attachment_config->falloff,
                        typed_attachment_config->is_looping,
                        typed_attachment_config->is_streaming,
                        typed_attachment_config->audio_resource_name,
                        typed_attachment_config->audio_resource_package_name,
                        &new_emitter.emitter)) {
                    KERROR("Failed to create audio emitter. See logs for details.");
                }

                // Add debug data and initialize it.
                new_emitter.debug_data = KALLOC_TYPE(scene_debug_data, MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = new_emitter.debug_data;

                if (!debug_sphere3d_create(typed_attachment_config->outer_radius, (vec4){1.0f, 0.5f, 0.0f, 1.0f}, KTRANSFORM_INVALID, &debug->sphere)) {
                    KERROR("Failed to create debug sphere for audio emitter.");
                } else {
                    // ktransform_position_set(debug->sphere.ktransform, new_emitter.data.position);
                }
                if (!debug_sphere3d_initialize(&debug->sphere)) {
                    KERROR("Failed to create debug sphere for audio emitter.");
                } else {
                    // Find a free slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 emitter_count = darray_length(s->audio_emitters);
                    for (u32 dli = 0; dli < emitter_count; ++dli) {
                        if (s->audio_emitters[dli].generation == INVALID_ID) {
                            // Found a slot, use it.
                            index = dli;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->audio_emitters, (scene_audio_emitter){0});
                        darray_push(s->audio_emitter_attachments, (scene_attachment){0});
                        index = emitter_count;
                    }

                    s->audio_emitters[index] = new_emitter;

                    scene_attachment* attachment = &s->audio_emitter_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_AUDIO_EMITTER;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // NOTE: These dont have metadata. If metadata is required, get it here.
                    /* if (!is_readonly) {
                        scene_point_light_metadata* meta = 0;
                    } */
                }
            }
        }

        // Static meshes
        if (node_config->static_mesh_configs) {
            u32 count = darray_length(node_config->static_mesh_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_static_mesh_config* typed_attachment_config = &node_config->static_mesh_configs[i];

                if (!typed_attachment_config->asset_name) {
                    KWARN("Invalid static mesh config, asset_name is required.");
                    return;
                }

                kstatic_mesh_instance new_static_mesh = static_mesh_instance_acquire(engine_systems_get()->static_mesh_system, typed_attachment_config->asset_name, 0, 0);
                // Find a free slot and take it, or push a new one.
                u32 index = INVALID_ID;
                u32 static_mesh_count = darray_length(s->static_meshes);
                for (u32 smi = 0; smi < static_mesh_count; ++smi) {
                    if (s->static_meshes[i].instance_id == INVALID_ID_U16) {
                        // Found a slot, use it.
                        index = smi;
                        break;
                    }
                }
                if (index == INVALID_ID) {
                    // No empty slot found, so push empty entries and obtain pointers.
                    darray_push(s->static_meshes, (kstatic_mesh_instance){0});
                    darray_push(s->mesh_attachments, (scene_attachment){0});
                    if (!is_readonly) {
                        darray_push(s->mesh_metadata, (scene_static_mesh_metadata){0});
                    }

                    index = static_mesh_count;
                }

                // Fill out the structs.
                s->static_meshes[index] = new_static_mesh;

                scene_attachment* attachment = &s->mesh_attachments[index];
                attachment->resource_handle = khandle_create(index);
                attachment->hierarchy_node_handle = hierarchy_node_handle;
                attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
                attachment->tag_count = typed_attachment_config->base.tag_count;
                if (attachment->tag_count) {
                    attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                    KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                }

                // For "edit" mode, retain metadata.
                if (!is_readonly) {
                    scene_static_mesh_metadata* meta = &s->mesh_metadata[index];
                    meta->resource_name = typed_attachment_config->asset_name;
                    meta->package_name = typed_attachment_config->package_name;
                }
            }
        }

        // Heightmap terrains
        if (node_config->heightmap_terrain_configs) {
            u32 count = darray_length(node_config->heightmap_terrain_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_heightmap_terrain_config* typed_attachment_config = &node_config->heightmap_terrain_configs[i];

                if (!typed_attachment_config->asset_name) {
                    KWARN("Invalid heightmap terrain config, asset_name is required.");
                    return;
                }

                const engine_system_states* systems = engine_systems_get();

                // Request the asset.
                kasset_heightmap_terrain* terrain_asset = asset_system_request_heightmap_terrain_from_package_sync(
                    systems->asset_state,
                    kname_string_get(typed_attachment_config->package_name),
                    kname_string_get(typed_attachment_config->asset_name));

                // Create the terrain.
                terrain new_terrain = {0};
                b8 result = terrain_create(terrain_asset, &new_terrain);

                // Release the asset.
                asset_system_release_heightmap_terrain(systems->asset_state, terrain_asset);

                if (!result) {
                    KWARN("Failed to load heightmap terrain.");
                    return;
                }

                if (!terrain_initialize(&new_terrain)) {
                    KERROR("Failed to initialize heightmap terrain.");
                    return;
                } else {
                    // Find a free slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 heightmap_terrain_count = darray_length(s->terrains);
                    for (u32 hti = 0; hti < heightmap_terrain_count; ++hti) {
                        if (s->terrains[i].state == TERRAIN_STATE_UNDEFINED) {
                            // Found a slot, use it.
                            index = hti;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->terrains, (terrain){0});
                        darray_push(s->terrain_attachments, (scene_attachment){0});
                        if (!is_readonly) {
                            darray_push(s->mesh_metadata, (scene_terrain_metadata){0});
                        }

                        index = heightmap_terrain_count;
                    }

                    // Fill out the structs.
                    s->terrains[index] = new_terrain;

                    scene_attachment* attachment = &s->terrain_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // For "edit" mode, retain metadata.
                    if (!is_readonly) {
                        scene_terrain_metadata* meta = &s->terrain_metadata[index];
                        meta->resource_name = typed_attachment_config->asset_name;
                        meta->package_name = typed_attachment_config->package_name;
                    }
                }
            }
        }

        // Water planes
        if (node_config->water_plane_configs) {
            u32 count = darray_length(node_config->water_plane_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_water_plane_config* typed_attachment_config = &node_config->water_plane_configs[i];

                /* if (!typed_attachment_config->asset_name) {
                    KWARN("Invalid heightmap terrain config, asset_name is required.");
                    return;
                } */

                water_plane wp;
                if (!water_plane_create(&wp)) {
                    KWARN("Failed to create water plane.");
                }

                // Initialize the water plane.
                if (!water_plane_initialize(&wp)) {
                    KERROR("Failed to initialize water plane. See logs for details.");
                } else {
                    // Find a free slot and take it, or push a new one.
                    u32 index = INVALID_ID;
                    u32 water_plane_count = darray_length(s->water_planes);
                    for (u32 wpi = 0; wpi < water_plane_count; ++wpi) {
                        if (s->terrains[i].state == TERRAIN_STATE_UNDEFINED) {
                            // Found a slot, use it.
                            index = wpi;
                            break;
                        }
                    }
                    if (index == INVALID_ID) {
                        // No empty slot found, so push empty entries and obtain pointers.
                        darray_push(s->water_planes, (water_plane){0});
                        darray_push(s->water_plane_attachments, (scene_attachment){0});
                        if (!is_readonly) {
                            darray_push(s->water_plane_metadata, (scene_water_plane_metadata){0});
                        }

                        index = water_plane_count;
                    }

                    // Fill out the structs.
                    s->water_planes[index] = wp;

                    scene_attachment* attachment = &s->water_plane_attachments[index];
                    attachment->resource_handle = khandle_create(index);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // For "edit" mode, retain metadata.
                    if (!is_readonly) {
                        scene_water_plane_metadata* meta = &s->water_plane_metadata[index];
                        meta->reserved = 0;
                        // NOTE: expand this when config is added.
                        /* meta->resource_name = typed_attachment_config->asset_name;
                        meta->package_name = typed_attachment_config->package_name; */
                    }
                }
            }
        }

        // Volumes
        if (node_config->volume_configs) {
            u32 count = darray_length(node_config->volume_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_volume_config* typed_attachment_config = &node_config->volume_configs[i];

                scene_volume new_volume = {0};
                new_volume.type = typed_attachment_config->volume_type;
                new_volume.shape_type = typed_attachment_config->shape_type;

                // tags
                new_volume.hit_sphere_tag_count = typed_attachment_config->hit_sphere_tag_count;
                if (new_volume.hit_sphere_tag_count) {
                    new_volume.hit_sphere_tags = KALLOC_TYPE_CARRAY(kname, new_volume.hit_sphere_tag_count);
                    KCOPY_TYPE_CARRAY(new_volume.hit_sphere_tags, typed_attachment_config->hit_sphere_tags, kname, new_volume.hit_sphere_tag_count);
                } else {
                    new_volume.hit_sphere_tags = 0;
                }

                // Add debug data and initialize it.
                new_volume.debug_data = KALLOC_TYPE(scene_debug_data, MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = new_volume.debug_data;
                b8 init_result = false;
                switch (new_volume.shape_type) {
                case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                    new_volume.shape_config.radius = typed_attachment_config->shape_config.radius;
                    // Create debug sphere TODO: colour?
                    if (!debug_sphere3d_create(typed_attachment_config->shape_config.radius, (vec4){1.0f, 0.0f, 1.0f, 1.0f}, ktransform_handle, &debug->sphere)) {
                        KERROR("Failed to create debug sphere for sphere volume.");
                    } else {
                        // ktransform_position_set(debug->sphere.ktransform, new_emitter.data.position);
                    }
                    init_result = debug_sphere3d_initialize(&debug->sphere);
                    break;
                case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                    new_volume.shape_config.extents = typed_attachment_config->shape_config.extents;
                    if (!debug_box3d_create(typed_attachment_config->shape_config.extents, ktransform_handle, &debug->box)) {
                        KERROR("Failed to create debug box for rectangle volume.");
                    } else {
                        /* ktransform_position_set(debug->box.ktransform, vec3_from_vec4(new_volume.data.position)); */
                    }
                    init_result = debug_box3d_initialize(&debug->box);

                    if (init_result) {
                        debug_box3d_colour_set(&debug->box, (vec4){1.0f, 0.0f, 1.0f, 1.0f});
                        debug_box3d_update(&debug->box);
                    }
                    break;
                }

                if (init_result) {

                    // Commands

                    if (typed_attachment_config->on_enter_command) {
                        new_volume.on_enter_command = string_duplicate(typed_attachment_config->on_enter_command);
                    }
                    if (typed_attachment_config->on_leave_command) {
                        new_volume.on_leave_command = string_duplicate(typed_attachment_config->on_leave_command);
                    }
                    if (typed_attachment_config->on_update_command) {
                        new_volume.on_update_command = string_duplicate(typed_attachment_config->on_update_command);
                    }

                    // Find a free slot and take it, or push a new one.
                    u32 volume_count = darray_length(s->volumes);
                    darray_push(s->volumes, new_volume);
                    darray_push(s->volume_attachments, (scene_attachment){0});
                    scene_attachment* attachment = &s->volume_attachments[volume_count];
                    attachment->resource_handle = khandle_create(volume_count);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_VOLUME;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // NOTE: These dont have metadata. If metadata is required, get it here.
                    /* if (!is_readonly) {
                        scene_point_light_metadata* meta = 0;
                    } */
                } else {
                    KERROR("Failed to create debug shape for volume.");
                }
            }
        }

        // Hit spheres
        if (node_config->hit_sphere_configs) {
            u32 count = darray_length(node_config->hit_sphere_configs);
            for (u32 i = 0; i < count; ++i) {
                scene_node_attachment_hit_sphere_config* typed_attachment_config = &node_config->hit_sphere_configs[i];

                scene_hit_sphere new_hit_sphere = {0};
                new_hit_sphere.radius = typed_attachment_config->radius;

                // Add debug data and initialize it.
                new_hit_sphere.debug_data = KALLOC_TYPE(scene_debug_data, MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = new_hit_sphere.debug_data;
                // Create debug sphere TODO: colour?
                if (!debug_sphere3d_create(typed_attachment_config->radius, (vec4){0.0f, 0.5f, 1.0f, 1.0f}, KTRANSFORM_INVALID, &debug->sphere)) {
                    KERROR("Failed to create debug sphere for hit sphere.");
                } else {
                    // ktransform_position_set(debug->sphere.ktransform, new_emitter.data.position);
                }

                if (debug_sphere3d_initialize(&debug->sphere)) {

                    // Find a free slot and take it, or push a new one.
                    u32 hit_sphere_count = darray_length(s->hit_spheres);
                    darray_push(s->hit_spheres, new_hit_sphere);
                    darray_push(s->hit_sphere_attachments, (scene_attachment){0});
                    scene_attachment* attachment = &s->hit_sphere_attachments[hit_sphere_count];
                    attachment->resource_handle = khandle_create(hit_sphere_count);
                    attachment->hierarchy_node_handle = hierarchy_node_handle;
                    attachment->attachment_type = SCENE_NODE_ATTACHMENT_TYPE_HIT_SPHERE;
                    attachment->tag_count = typed_attachment_config->base.tag_count;
                    if (attachment->tag_count) {
                        attachment->tags = KALLOC_TYPE_CARRAY(kname, attachment->tag_count);
                        KCOPY_TYPE_CARRAY(attachment->tags, typed_attachment_config->base.tags, kname, attachment->tag_count);
                    }

                    // NOTE: These dont have metadata. If metadata is required, get it here.
                    /* if (!is_readonly) {
                        scene_point_light_metadata* meta = 0;
                    } */
                } else {
                    KERROR("Failed to create debug shape for hit sphere.");
                }
            }
        }

        // Process children.
        if (node_config->children) {
            u32 child_count = node_config->child_count;
            for (u32 i = 0; i < child_count; ++i) {
                scene_node_initialize(s, hierarchy_node_handle, &node_config->children[i]);
            }
        }
    }
}

b8 scene_initialize(scene* scene) {
    if (!scene) {
        KERROR("scene_initialize requires a valid pointer to a scene.");
        return false;
    }

    // Process configuration and setup hierarchy.
    if (scene->config) {
        kasset_scene* config = scene->config;
        if (scene->config->name) {
            scene->name = scene->config->name;
        }
        if (scene->config->description) {
            scene->description = string_duplicate(scene->config->description);
        }

        // Process root nodes.
        if (config->nodes) {
            u32 node_count = config->node_count;
            // An invalid handle means there is no parent, which is true for root nodes.
            for (u32 i = 0; i < node_count; ++i) {
                scene_node_initialize(scene, KHIERARCHY_NODE_INVALID, &config->nodes[i]);
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

b8 scene_load(scene* scene) {
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
    if (scene->static_meshes) {
        u32 mesh_count = darray_length(scene->static_meshes);
        for (u32 i = 0; i < mesh_count; ++i) {
            // TODO: is this needed anymore?
            /* if (!mesh_load(&scene->static_meshes[i])) {
                KERROR("Mesh failed to load.");
            } */
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

    // Load water planes
    if (scene->water_planes) {
        u32 water_plane_count = darray_length(scene->water_planes);
        for (u32 i = 0; i < water_plane_count; ++i) {
            if (!water_plane_load(&scene->water_planes[i])) {
                KERROR("Failed to load water plane. See logs for details.");
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
                    scene_debug_data* debug = scene->dir_lights[i].debug_data;
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
                scene_debug_data* debug = (scene_debug_data*)scene->point_lights[i].debug_data;
                if (!debug_box3d_load(&debug->box)) {
                    KERROR("debug box failed to load.");
                    kfree(scene->point_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    scene->point_lights[i].debug_data = 0;
                }
            }
        }
    }

    if (scene->audio_emitters) {
        struct kaudio_system_state* audio_state = engine_systems_get()->audio_system;
        u32 emitter_count = darray_length(scene->audio_emitters);
        for (u32 i = 0; i < emitter_count; ++i) {
            scene_audio_emitter* emitter = &scene->audio_emitters[i];

            scene_attachment* emitter_attachment = &scene->audio_emitter_attachments[i];
            ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, emitter_attachment->hierarchy_node_handle);

            mat4 world;
            if (ktransform_handle != KTRANSFORM_INVALID) {
                world = ktransform_world_get(ktransform_handle);
            } else {
                // TODO: traverse tree to try and find a ancestor node with a transform.
                world = mat4_identity();
            }

            // Get world position for the audio emitter based on it's owning node's ktransform.
            vec3 emitter_world_pos = mat4_position(world);
            kaudio_emitter_world_position_set(audio_state, emitter->emitter, emitter_world_pos);

            if (!kaudio_emitter_load(audio_state, emitter->emitter)) {
                KWARN("Failed to load audio emitter.");
            } else {
                // Load debug data if it was setup.
                scene_debug_data* debug = (scene_debug_data*)scene->audio_emitters[i].debug_data;
                if (!debug_sphere3d_load(&debug->sphere)) {
                    KERROR("debug sphere failed to load.");
                    kfree(scene->audio_emitters[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                    scene->audio_emitters[i].debug_data = 0;
                }
            }
        }
    }

    if (scene->volumes) {
        u32 volume_count = darray_length(scene->volumes);
        for (u32 i = 0; i < volume_count; ++i) {
            // Load debug data if it was setup.
            scene_debug_data* debug = (scene_debug_data*)scene->volumes[i].debug_data;
            b8 load_result = false;
            switch (scene->volumes[i].shape_type) {
            case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                load_result = debug_sphere3d_load(&debug->sphere);
                break;
            case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                load_result = debug_box3d_load(&debug->box);
                break;
            }

            if (!load_result) {
                KERROR("debug shape failed to load.");

                kfree(scene->volumes[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene->volumes[i].debug_data = 0;
            }
        }
    }

    if (scene->hit_spheres) {
        u32 hit_sphere_count = darray_length(scene->hit_spheres);
        for (u32 i = 0; i < hit_sphere_count; ++i) {
            // Load debug data if it was setup.
            scene_debug_data* debug = (scene_debug_data*)scene->hit_spheres[i].debug_data;

            if (!debug_sphere3d_load(&debug->sphere)) {
                KERROR("debug shape failed to load.");

                kfree(scene->hit_spheres[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene->hit_spheres[i].debug_data = 0;
            }
        }
    }

    // Update the state to show the scene is fully loaded.
    scene->state = SCENE_STATE_LOADED;

    return true;
}

b8 scene_unload(scene* scene, b8 immediate) {
    if (!scene) {
        return false;
    }

    // Always immediately set the state to unloading.
    scene->state = SCENE_STATE_UNLOADING;

    // If immediate, trigger the unload right away. Otherwise it will happen on the next frame.
    if (immediate) {
        scene_actual_unload(scene);
        return true;
    }

    return true;
}

static b8 any_tags_match(const kname* list_0, u32 count_0, const kname* list_1, u32 count_1) {
    for (u32 i = 0; i < count_0; ++i) {
        for (u32 j = 0; j < count_1; ++j) {
            if (list_0[i] == list_0[j]) {
                return true;
            }
        }
    }
    return false;
}

b8 scene_update(scene* scene, const struct frame_data* p_frame_data) {
    if (!scene) {
        return false;
    }

    if (scene->state == SCENE_STATE_UNLOADING) {
        scene_actual_unload(scene);
        return true;
    }

    if (scene->state == SCENE_STATE_LOADED) {
        hierarchy_graph_update(&scene->hierarchy);

        // Update volumes.
        if (scene->volumes) {
            u32 volume_count = darray_length(scene->volumes);
            u32 hit_sphere_count = darray_length(scene->hit_spheres);
            for (u32 i = 0; i < volume_count; ++i) {
                scene_volume* volume = &scene->volumes[i];

                // Get world position of the volume.

                scene_attachment* vol_attachment = &scene->volume_attachments[i];
                /* khandle vol_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, vol_attachment->hierarchy_node_handle); */

                // World position and rotation.
                /* vec3 vol_world_pos = hierarchy_graph_world_position_get(&scene->hierarchy, vol_ktransform_handle); */
                /* quat vol_world_rot = hierarchy_graph_world_rotation_get(&scene->hierarchy, vol_ktransform_handle); */

                // HACK:
                ktransform vol_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, vol_attachment->hierarchy_node_handle);

                mat4 world;
                if (vol_ktransform_handle != KTRANSFORM_INVALID) {
                    world = ktransform_world_get(vol_ktransform_handle);
                } else {
                    // TODO: traverse tree to try and find a ancestor node with a transform.
                    world = mat4_identity();
                }
                vec3 vol_world_pos = mat4_position(world);
                // end hack

                // TODO: Iterate hit_spheres with matching tags
                for (u32 j = 0; j < hit_sphere_count; ++j) {
                    scene_hit_sphere* hit_sphere = &scene->hit_spheres[j];
                    scene_attachment* hs_attachment = &scene->hit_sphere_attachments[j];

                    // Check for matching tags. TODO: Need to change this to use a DOD-style lookup
                    b8 matches_tag = any_tags_match(volume->hit_sphere_tags, volume->hit_sphere_tag_count, hs_attachment->tags, hs_attachment->tag_count);
                    if (matches_tag) {
                        // Check if there is an overlap.

                        // Get world position of the current hit sphere.
                        /* scene_attachment* hs_attachment = &scene->hit_sphere_attachments[j];
                        khandle hs_hierarchy_node_handle = hs_attachment->hierarchy_node_handle;
                        khandle hs_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, hs_hierarchy_node_handle);

                        // NOTE: Ignoring rotation and scale.
                        vec3 hit_sphere_world_pos = hierarchy_graph_world_position_get(&scene->hierarchy, hs_ktransform_handle); */

                        // HACK: see if this works
                        scene_attachment* hs_attachment = &scene->hit_sphere_attachments[j];
                        ktransform hs_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, hs_attachment->hierarchy_node_handle);

                        mat4 world;
                        if (hs_ktransform_handle != KTRANSFORM_INVALID) {
                            world = ktransform_world_get(hs_ktransform_handle);
                        } else {
                            // TODO: traverse tree to try and find a ancestor node with a transform.
                            world = mat4_identity();
                        }
                        vec3 hs_world_pos = mat4_position(world);
                        // end hack

                        // Check if the hit sphere already exists in the array.
                        i32 index = -1;
                        if (volume->hit_sphere_indices) {
                            u32 hs_count = darray_length(volume->hit_sphere_indices);
                            for (u32 k = 0; k < hs_count; ++k) {
                                if (volume->hit_sphere_indices[k] == j) {
                                    index = k;
                                    break;
                                }
                            }
                        }

                        // Check for collision.
                        b8 has_collision = false;
                        switch (volume->shape_type) {
                        case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                            has_collision = (vec3_distance(hs_world_pos, vol_world_pos) <= (hit_sphere->radius + volume->shape_config.radius));
                            break;
                        case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                            // TODO: Bring point into OBB space and check if colliding.
                            has_collision = false;
                            break;
                        }

                        if (has_collision) {
                            // Have a hit.
                            if (index == -1) {
                                if (volume->on_enter_command) {
                                    console_command_execute(volume->on_enter_command);
                                }

                                // Add to the list.
                                if (!volume->hit_sphere_indices) {
                                    volume->hit_sphere_indices = darray_create(u32);
                                }
                                darray_push(volume->hit_sphere_indices, (u32)j);
                            } else {
                                if (volume->on_update_command) {
                                    console_command_execute(volume->on_update_command);
                                }
                            }
                        } else if (index > -1) {
                            if (volume->on_leave_command) {
                                console_command_execute(volume->on_leave_command);
                            }
                            u32 rubbish = 0;
                            darray_pop_at(volume->hit_sphere_indices, (u32)index, &rubbish);
                        }
                    }
                }
            }
        }

        if (scene->dir_lights) {
            u32 directional_light_count = darray_length(scene->dir_lights);
            for (u32 i = 0; i < directional_light_count; ++i) {
                // TODO: Only update directional light if changed.
                if (scene->dir_lights[i].generation != INVALID_ID && scene->dir_lights[i].debug_data) {
                    scene_debug_data* debug = scene->dir_lights[i].debug_data;
                    if (debug->line.geometry.generation != INVALID_ID_U16) {
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
                // Update the point light's data position (world position) to take into account
                // the owning node's transform.
                scene_attachment* point_light_attachment = &scene->point_light_attachments[i];
                ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, point_light_attachment->hierarchy_node_handle);

                mat4 world;
                if (ktransform_handle != KTRANSFORM_INVALID) {
                    world = ktransform_world_get(ktransform_handle);
                } else {
                    // TODO: traverse tree to try and find a ancestor node with a transform.
                    world = mat4_identity();
                }

                // Calculate world position for the point light.
                /* scene->point_lights[i].data.position = vec4_mul_mat4(scene->point_lights[i].position, world); */
                // TODO: the below method works, the above does not. But hwhy?
                vec3 pos = vec3_from_vec4(scene->point_lights[i].position);
                scene->point_lights[i].data.position = vec4_from_vec3(vec3_transform(pos, 1.0f, world), 1.0f);

                // Debug box info update.
                if (scene->point_lights[i].debug_data) {
                    // TODO: Only update point light if changed.
                    scene_debug_data* debug = (scene_debug_data*)scene->point_lights[i].debug_data;
                    if (debug->box.geometry.generation != INVALID_ID_U16) {
                        // Update transform.
                        ktransform_position_set(debug->box.ktransform, vec3_from_vec4(scene->point_lights[i].data.position));

                        // Update colour. NOTE: doing this every frame might be expensive if we have to reload the geometry all the time.
                        // TODO: Perhaps there is another way to accomplish this, like a shader that uses a uniform for colour?
                        debug_box3d_colour_set(&debug->box, scene->point_lights[i].data.colour);
                    }
                }
            }
        }

        // Update audio emitter debug boxes.
        if (scene->audio_emitters) {
            struct kaudio_system_state* audio_state = engine_systems_get()->audio_system;
            u32 emitter_count = darray_length(scene->audio_emitters);
            for (u32 i = 0; i < emitter_count; ++i) {
                scene_audio_emitter* emitter = &scene->audio_emitters[i];
                // Update the audio emitter's data position (world position) to take into account
                // the owning node's transform.
                scene_attachment* emitter_attachment = &scene->audio_emitter_attachments[i];
                ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, emitter_attachment->hierarchy_node_handle);

                mat4 world;
                if (ktransform_handle != KTRANSFORM_INVALID) {
                    world = ktransform_world_get(ktransform_handle);
                } else {
                    // TODO: traverse tree to try and find a ancestor node with a transform.
                    world = mat4_identity();
                }

                // Get world position for the audio emitter based on it's owning node's ktransform and set it.
                vec3 emitter_world_pos = mat4_position(world);
                kaudio_emitter_world_position_set(audio_state, emitter->emitter, emitter_world_pos);
            }
        }

        // Check meshes to see if they have debug data. If not, add it here and init/load it.
        // Doing this here because mesh loading is multi-threaded, and may not yet be available
        // even though the object is present in the scene.
        u32 mesh_count = darray_length(scene->static_meshes);
        for (u32 i = 0; i < mesh_count; ++i) {
            // TODO: debug data - refactor this or find another way to handle it.
            /* static_mesh_instance* m = &scene->static_meshes[i];
            if (m->generation == INVALID_ID_U8) {
                continue;
            }
            if (!m->debug_data) {
                m->debug_data = kallocate(sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                scene_debug_data* debug = m->debug_data;

                if (!debug_box3d_create((vec3){0.2f, 0.2f, 0.2f}, khandle_invalid(), &debug->box)) {
                    KERROR("Failed to create debug box for mesh '%s'.", m->name);
                } else {
                    // Lookup the attachment to get the ktransform handle to set as the parent.
                    scene_attachment* attachment = &scene->mesh_attachments[i];
                    ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                    // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                    // the ktransform here, using the node's world ktransform as the parent.
                    ktransform_calculate_local(debug->box.ktransform);
                    mat4 local = ktransform_local_get(debug->box.ktransform);
                    mat4 parent_world = ktransform_world_get(ktransform_handle);
                    mat4 model = mat4_mul(local, parent_world);
                    ktransform_world_set(debug->box.ktransform, model);

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
            } */
        }

        /* // Update volume debug shapes.
        if (scene->volumes) {
            u32 volume_count = darray_length(scene->volumes);
            for (u32 i = 0; i < volume_count; ++i) {
                // Update the point light's data position (world position) to take into account
                // the owning node's transform.
                scene_attachment* volume_attachment = &scene->volume_attachments[i];
                ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, volume_attachment->hierarchy_node_handle);

                mat4 world;
                if (ktransform_handle != KTRANSFORM_INVALID) {
                    world = ktransform_world_get(ktransform_handle);
                } else {
                    // TODO: traverse tree to try and find a ancestor node with a transform.
                    world = mat4_identity();
                }

                // Calculate world position for the point light.
                vec3 pos = vec3_from_vec4(scene->point_lights[i].position);
                scene->point_lights[i].data.position = vec4_from_vec3(vec3_transform(pos, 1.0f, world), 1.0f);

                // Debug box info update.
                if (scene->point_lights[i].debug_data) {
                    // TODO: Only update point light if changed.
                    scene_debug_data* debug = (scene_debug_data*)scene->point_lights[i].debug_data;
                    if (debug->box.geometry.generation != INVALID_ID_U16) {
                        // Update transform.
                        ktransform_position_set(debug->box.ktransform, vec3_from_vec4(scene->point_lights[i].data.position));

                        // Update colour. NOTE: doing this every frame might be expensive if we have to reload the geometry all the time.
                        // TODO: Perhaps there is another way to accomplish this, like a shader that uses a uniform for colour?
                        debug_box3d_colour_set(&debug->box, scene->point_lights[i].data.colour);
                    }
                }
            }
        } */
    }

    return true;
}

void scene_render_frame_prepare(scene* scene, const struct frame_data* p_frame_data) {
    if (!scene) {
        return;
    }

    if (scene->state == SCENE_STATE_LOADED) {
        if (scene->dir_lights) {
            u32 directional_light_count = darray_length(scene->dir_lights);
            for (u32 i = 0; i < directional_light_count; ++i) {
                if (scene->dir_lights[i].generation != INVALID_ID && scene->dir_lights[i].debug_data) {
                    scene_debug_data* debug = scene->dir_lights[i].debug_data;
                    debug_line3d_render_frame_prepare(&debug->line, p_frame_data);
                }
            }
        }

        // Update point light debug boxes.
        if (scene->point_lights) {
            u32 point_light_count = darray_length(scene->point_lights);
            for (u32 i = 0; i < point_light_count; ++i) {
                if (scene->point_lights[i].debug_data) {
                    scene_debug_data* debug = (scene_debug_data*)scene->point_lights[i].debug_data;

                    // Lookup the attachment to get the ktransform handle to set as the parent.
                    scene_attachment* attachment = &scene->point_light_attachments[i];
                    ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                    // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                    // the ktransform here, using the node's world ktransform as the parent.
                    ktransform_calculate_local(debug->box.ktransform);
                    mat4 local = ktransform_local_get(debug->box.ktransform);
                    mat4 parent_world = ktransform_world_get(ktransform_handle);
                    mat4 model = mat4_mul(local, parent_world);
                    ktransform_world_set(debug->box.ktransform, model);

                    debug_box3d_render_frame_prepare(&debug->box, p_frame_data);
                }
            }
        }

        // Update audio emitter debug spheres.
        if (scene->audio_emitters) {
            u32 emitter_count = darray_length(scene->audio_emitters);
            for (u32 i = 0; i < emitter_count; ++i) {
                if (scene->audio_emitters[i].debug_data) {
                    scene_debug_data* debug = (scene_debug_data*)scene->audio_emitters[i].debug_data;

                    // Lookup the attachment to get the ktransform handle to set as the parent.
                    scene_attachment* attachment = &scene->audio_emitter_attachments[i];
                    ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                    // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                    // the ktransform here, using the node's world ktransform as the parent.
                    ktransform_calculate_local(debug->sphere.ktransform);
                    mat4 local = ktransform_local_get(debug->sphere.ktransform);
                    mat4 parent_world = ktransform_world_get(ktransform_handle);
                    mat4 model = mat4_mul(local, parent_world);
                    ktransform_world_set(debug->sphere.ktransform, model);

                    debug_sphere3d_render_frame_prepare(&debug->sphere, p_frame_data);
                }
            }
        }

        // Check meshes to see if they have debug data.
        if (scene->static_meshes) {
            u32 mesh_count = darray_length(scene->static_meshes);
            for (u32 i = 0; i < mesh_count; ++i) {
                // TODO: debug data - refactor or find another way to do it.
                /* static_mesh_instance* m = &scene->static_meshes[i];
                if (m->generation == INVALID_ID_U8) {
                    continue;
                }
                if (m->debug_data) {
                    scene_debug_data* debug = m->debug_data;

                    // Lookup the attachment to get the ktransform handle to set as the parent.
                    scene_attachment* attachment = &scene->mesh_attachments[i];
                    ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                    // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                    // the ktransform here, using the node's world ktransform as the parent.
                    ktransform_calculate_local(debug->box.ktransform);
                    mat4 local = ktransform_local_get(debug->box.ktransform);
                    mat4 parent_world = ktransform_world_get(ktransform_handle);
                    mat4 model = mat4_mul(local, parent_world);
                    ktransform_world_set(debug->box.ktransform, model);

                    debug_box3d_render_frame_prepare(&debug->box, p_frame_data);
                } */
            }
        }

        // Update spawn point debug boxes/spheres.
        {
            if (scene->volumes) {
                u32 volume_count = darray_length(scene->volumes);
                for (u32 i = 0; i < volume_count; ++i) {
                    if (scene->volumes[i].debug_data) {
                        scene_debug_data* debug = (scene_debug_data*)scene->volumes[i].debug_data;

                        // Lookup the attachment to get the ktransform handle to set as the parent.
                        scene_attachment* attachment = &scene->volume_attachments[i];
                        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                        // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                        // the ktransform here, using the node's world ktransform as the parent.
                        ktransform d_ktransform;
                        switch (scene->volumes[i].shape_type) {
                        case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                        default:
                            d_ktransform = debug->sphere.ktransform;
                            break;
                        case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                            d_ktransform = debug->box.ktransform;
                            break;
                        }
                        ktransform_calculate_local(d_ktransform);
                        mat4 local = ktransform_local_get(d_ktransform);
                        mat4 parent_world = ktransform_world_get(ktransform_handle);
                        mat4 model = mat4_mul(local, parent_world);
                        ktransform_world_set(d_ktransform, model);

                        switch (scene->volumes[i].shape_type) {
                        case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                            debug_sphere3d_render_frame_prepare(&debug->sphere, p_frame_data);
                            break;
                        case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                            debug_box3d_render_frame_prepare(&debug->box, p_frame_data);
                            break;
                        }
                    }
                }
            }
        }

        // Update audio emitter debug spheres.
        if (scene->audio_emitters) {
            u32 emitter_count = darray_length(scene->audio_emitters);
            for (u32 i = 0; i < emitter_count; ++i) {
                if (scene->audio_emitters[i].debug_data) {
                    scene_debug_data* debug = (scene_debug_data*)scene->audio_emitters[i].debug_data;

                    // Lookup the attachment to get the ktransform handle to set as the parent.
                    scene_attachment* attachment = &scene->audio_emitter_attachments[i];
                    ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                    // Since debug objects aren't actually added to the hierarchy or as attachments, need to manually update
                    // the ktransform here, using the node's world ktransform as the parent.
                    ktransform_calculate_local(debug->sphere.ktransform);
                    mat4 local = ktransform_local_get(debug->sphere.ktransform);
                    mat4 parent_world = ktransform_world_get(ktransform_handle);
                    mat4 model = mat4_mul(local, parent_world);
                    ktransform_world_set(debug->sphere.ktransform, model);

                    debug_sphere3d_render_frame_prepare(&debug->sphere, p_frame_data);
                }
            }
        }
    }
}

void scene_update_lod_from_view_position(scene* scene, const frame_data* p_frame_data, vec3 view_position, f32 near_clip, f32 far_clip) {
    if (!scene) {
        return;
    }

    if (scene->state == SCENE_STATE_LOADED) {
        // Update terrain chunk LODs
        u32 terrain_count = darray_length(scene->terrains);
        for (u32 i = 0; i < terrain_count; ++i) {
            terrain* t = &scene->terrains[i];

            // Perform a lookup into the attachments array to get the hierarchy node.
            scene_attachment* attachment = &scene->terrain_attachments[i];
            ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
            mat4 model = ktransform_world_get(ktransform_handle);

            // Calculate LOD splits based on clip range.
            f32 range = far_clip - near_clip;

            // The first split distance is always 0.
            f32* splits = p_frame_data->allocator.allocate(sizeof(f32) * (t->lod_count + 1));
            splits[0] = 0.0f;
            for (u32 l = 0; l < t->lod_count; ++l) {
                f32 pct = (l + 1) / (f32)t->lod_count;
                // Just do linear splits for now.
                splits[l + 1] = (near_clip + range) * pct;
            }

            // Calculate chunk LODs based on distance from camera position.
            for (u32 c = 0; c < t->chunk_count; c++) {
                terrain_chunk* chunk = &t->chunks[c];

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

b8 scene_raycast(scene* scene, const struct ray* r, struct raycast_result* out_result) {
    if (!scene || !r || !out_result || scene->state != SCENE_STATE_LOADED) {
        return false;
    }

    // Only create if needed.
    out_result->hits = 0;

    // Iterate meshes in the scene.
    // TODO: This needs to be optimized. We need some sort of spatial partitioning to speed this up.
    // Otherwise a scene with thousands of objects will be super slow!
    u32 mesh_count = darray_length(scene->static_meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        kstatic_mesh_instance mi = scene->static_meshes[i];

        // Only count loaded meshes.
        if (!static_mesh_is_loaded(engine_systems_get()->static_mesh_system, mi.mesh)) {
            continue;
        }
        // Perform a lookup into the attachments array to get the hierarchy node.
        scene_attachment* attachment = &scene->mesh_attachments[i];
        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
        mat4 model = ktransform_world_get(ktransform_handle);
        f32 dist;

        extents_3d sm_extents = {0};
        if (static_mesh_extents_get(engine_systems_get()->static_mesh_system, mi.mesh, &sm_extents)) {
            if (raycast_oriented_extents(sm_extents, model, r, &dist)) {
                // Hit
                if (!out_result->hits) {
                    out_result->hits = darray_create(raycast_hit);
                }

                raycast_hit hit = {0};
                hit.distance = dist;
                hit.type = RAYCAST_HIT_TYPE_OBB;
                hit.position = vec3_add(r->origin, vec3_mul_scalar(r->direction, hit.distance));

                hit.ktransform_handle = ktransform_handle;
                hit.node_handle = attachment->hierarchy_node_handle;

                // Get parent ktransform handle if one exists.
                hit.ktransform_parent_handle = hierarchy_graph_parent_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
                // TODO: Indicate selection node attachment type somehow?

                darray_push(out_result->hits, hit);
            }
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

b8 scene_debug_render_data_query(scene* scene, u32* data_count, geometry_render_data** debug_geometries) {
    if (!scene || !data_count) {
        return false;
    }

    *data_count = 0;

    if (scene->state > SCENE_STATE_LOADED) {
        return true;
    }

    // TODO: Check if grid exists.
    // TODO: flag for toggling grid on and off.
    {
        if (debug_geometries) {
            geometry_render_data data = {0};
            data.model = mat4_identity();

            kgeometry* g = &scene->grid.geometry;
            data.material.base_material = KMATERIAL_INVALID;        // debug geometries don't need a material.
            data.material.instance_id = KMATERIAL_INSTANCE_INVALID; // debug geometries don't need a material.
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
        if (scene->dir_lights) {
            u32 directional_light_count = darray_length(scene->dir_lights);
            for (u32 i = 0; i < directional_light_count; ++i) {
                if (scene->dir_lights[i].debug_data) {
                    if (debug_geometries) {
                        scene_debug_data* debug = scene->dir_lights[i].debug_data;

                        // Debug line 3d
                        geometry_render_data data = {0};
                        data.model = ktransform_world_get(debug->line.ktransform);
                        kgeometry* g = &debug->line.geometry;
                        data.material.base_material = KMATERIAL_INVALID;        // debug geometries don't need a material.
                        data.material.instance_id = KMATERIAL_INSTANCE_INVALID; // debug geometries don't need a material.
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;

                        (*debug_geometries)[(*data_count)] = data;
                    }
                    (*data_count)++;
                }
            }
        }
    }

    // Point lights
    {
        if (scene->point_lights) {
            u32 point_light_count = darray_length(scene->point_lights);
            for (u32 i = 0; i < point_light_count; ++i) {
                if (scene->point_lights[i].debug_data) {
                    if (debug_geometries) {
                        scene_debug_data* debug = (scene_debug_data*)scene->point_lights[i].debug_data;

                        // Debug box 3d
                        geometry_render_data data = {0};
                        data.model = ktransform_world_get(debug->box.ktransform);
                        kgeometry* g = &debug->box.geometry;
                        data.material.base_material = KMATERIAL_INVALID;        // debug geometries don't need a material.
                        data.material.instance_id = KMATERIAL_INSTANCE_INVALID; // debug geometries don't need a material.
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;

                        (*debug_geometries)[(*data_count)] = data;
                    }
                    (*data_count)++;
                }
            }
        }
    }

    // Audio emitters
    {
        if (scene->audio_emitters) {
            u32 emitter_count = darray_length(scene->audio_emitters);
            for (u32 i = 0; i < emitter_count; ++i) {
                if (scene->audio_emitters[i].debug_data) {
                    if (debug_geometries) {
                        scene_debug_data* debug = (scene_debug_data*)scene->audio_emitters[i].debug_data;

                        // Debug sphere 3d
                        geometry_render_data data = {0};
                        // Only want the ultimate position of this, since we don't want the debug data
                        // to scale or rotate along with any parent it may be under.
                        mat4 sphere_world = ktransform_world_get(debug->sphere.ktransform);
                        vec3 world_pos = mat4_position(sphere_world);
                        data.model = mat4_translation(world_pos);

                        kgeometry* g = &debug->sphere.geometry;
                        data.material.base_material = KMATERIAL_INVALID;        // debug geometries don't need a material.
                        data.material.instance_id = KMATERIAL_INSTANCE_INVALID; // debug geometries don't need a material.
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;

                        (*debug_geometries)[(*data_count)] = data;
                    }
                    (*data_count)++;
                }
            }
        }
    }

    // Mesh debug shapes
    {
        if (scene->static_meshes) {
            u32 mesh_count = darray_length(scene->static_meshes);
            for (u32 i = 0; i < mesh_count; ++i) {
                // TODO: debug data - refactor or find another way to do it.
                /* if (scene->static_meshes[i].debug_data) {
                    if (debug_geometries) {
                        scene_debug_data* debug = (scene_debug_data*)scene->static_meshes[i].debug_data;

                        // Debug box 3d
                        geometry_render_data data = {0};
                        data.model = ktransform_world_get(debug->box.ktransform);
                        geometry* g = &debug->box.geo;
                        data.material = g->material;
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;
                        data.unique_id = debug->box.id.uniqueid;

                        (*debug_geometries)[(*data_count)] = data;
                    }
                    (*data_count)++;
                } */
            }
        }
    }

    // Volumes
    {
        if (scene->volumes) {
            u32 volume_count = darray_length(scene->volumes);
            for (u32 i = 0; i < volume_count; ++i) {
                if (scene->volumes[i].debug_data) {
                    if (debug_geometries) {
                        scene_debug_data* debug = (scene_debug_data*)scene->volumes[i].debug_data;

                        // Debug box 3d
                        geometry_render_data data = {0};

                        kgeometry* g = 0;
                        switch (scene->volumes[i].shape_type) {
                        case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                            data.model = ktransform_world_get(debug->sphere.ktransform);
                            g = &debug->sphere.geometry;
                            break;
                        case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                            data.model = ktransform_world_get(debug->box.ktransform);
                            g = &debug->box.geometry;
                            break;
                        }

                        data.material.base_material = KMATERIAL_INVALID;        // debug geometries don't need a material.
                        data.material.instance_id = KMATERIAL_INSTANCE_INVALID; // debug geometries don't need a material.
                        data.vertex_count = g->vertex_count;
                        data.vertex_buffer_offset = g->vertex_buffer_offset;
                        data.index_count = g->index_count;
                        data.index_buffer_offset = g->index_buffer_offset;

                        (*debug_geometries)[(*data_count)] = data;
                    }
                    (*data_count)++;
                }
            }
        }
    }

    return true;
}

b8 scene_mesh_render_data_query_from_line(const scene* scene, vec3 direction, vec3 center, f32 radius, frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries) {
    if (!scene) {
        return false;
    }
    if (scene->state > SCENE_STATE_LOADED) {
        *out_count = 0;
        return true;
    }

    geometry_distance* transparent_geometries = darray_create_with_allocator(geometry_distance, &p_frame_data->allocator);

    u32 mesh_count = darray_length(scene->static_meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        kstatic_mesh_instance* m = &scene->static_meshes[i];

        // Only count loaded meshes.
        if (!static_mesh_is_loaded(engine_systems_get()->static_mesh_system, m->mesh)) {
            continue;
        }

        scene_attachment* attachment = &scene->mesh_attachments[i];
        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
        mat4 model = ktransform_world_get(ktransform_handle);

        // TODO: Cache this somewhere instead of calculating all the time.
        f32 determinant = mat4_determinant(model);
        b8 winding_inverted = determinant < 0;

        u16 submesh_count = 0;
        static_mesh_submesh_count_get(engine_systems_get()->static_mesh_system, m->mesh, &submesh_count);
        for (u16 j = 0; j < submesh_count; ++j) {
            const kgeometry* g = static_mesh_submesh_geometry_get_at(engine_systems_get()->static_mesh_system, m->mesh, j);
            const kmaterial_instance* m_inst = static_mesh_submesh_material_instance_get_at(engine_systems_get()->static_mesh_system, *m, j);
            if (!kmaterial_is_loaded_get(engine_systems_get()->material_system, m_inst->base_material)) {
                m_inst = &default_material;
            }

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
                data.material = *m_inst;
                data.vertex_count = g->vertex_count;
                data.vertex_buffer_offset = g->vertex_buffer_offset;
                data.index_count = g->index_count;
                data.index_buffer_offset = g->index_buffer_offset;
                data.unique_id = 0; // m->id.uniqueid; FIXME: Need this for pixel selection.
                data.winding_inverted = winding_inverted;

                // Check if transparent. If so, put into a separate, temp array to be
                // sorted by distance from the camera. Otherwise, put into the
                // out_geometries array directly.
                b8 has_transparency = kmaterial_flag_get(engine_systems_get()->material_system, m_inst->base_material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT);
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

b8 scene_terrain_render_data_query_from_line(const scene* scene, vec3 direction, vec3 center, f32 radius, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries) {
    if (!scene) {
        return false;
    }
    if (scene->state > SCENE_STATE_LOADED) {
        *out_count = 0;
        return true;
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        terrain* t = &scene->terrains[i];
        scene_attachment* attachment = &scene->terrain_attachments[i];
        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
        mat4 model = ktransform_world_get(ktransform_handle);

        // TODO: Cache this somewhere instead of calculating all the time.
        f32 determinant = mat4_determinant(model);
        b8 winding_inverted = determinant < 0;

        // Check each chunk to see if it is in view.
        for (u32 c = 0; c < t->chunk_count; ++c) {
            terrain_chunk* chunk = &t->chunks[c];

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

b8 scene_mesh_render_data_query(const scene* scene, const kfrustum* f, vec3 center, frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries) {
    if (!scene) {
        return false;
    }
    if (scene->state > SCENE_STATE_LOADED) {
        *out_count = 0;
        return true;
    }

    geometry_distance* transparent_geometries = darray_create_with_allocator(geometry_distance, &p_frame_data->allocator);

    // Iterate all meshes in the scene.
    u32 mesh_count = darray_length(scene->static_meshes);
    for (u32 resource_index = 0; resource_index < mesh_count; ++resource_index) {
        kstatic_mesh_instance* m = &scene->static_meshes[resource_index];

        // Only count loaded meshes.
        if (!static_mesh_is_loaded(engine_systems_get()->static_mesh_system, m->mesh)) {
            continue;
        }
        // Attachment lookup - by resource index.
        scene_attachment* attachment = &scene->mesh_attachments[resource_index];
        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
        mat4 model = ktransform_world_get(ktransform_handle);

        // TODO: Cache this somewhere instead of calculating all the time.
        f32 determinant = mat4_determinant(model);
        b8 winding_inverted = determinant < 0;

        u16 submesh_count = 0;
        static_mesh_submesh_count_get(engine_systems_get()->static_mesh_system, m->mesh, &submesh_count);
        for (u16 j = 0; j < submesh_count; ++j) {
            const kgeometry* g = static_mesh_submesh_geometry_get_at(engine_systems_get()->static_mesh_system, m->mesh, j);
            const kmaterial_instance* m_inst = static_mesh_submesh_material_instance_get_at(engine_systems_get()->static_mesh_system, *m, j);
            if (!kmaterial_is_loaded_get(engine_systems_get()->material_system, m_inst->base_material)) {
                m_inst = &default_material;
            }

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
                vec3 extents_max = mat4_mul_vec3(model, g->extents.max);

                // Translate/scale the center.
                vec3 g_center = mat4_mul_vec3(model, g->center);
                vec3 half_extents = {
                    kabs(extents_max.x - g_center.x),
                    kabs(extents_max.y - g_center.y),
                    kabs(extents_max.z - g_center.z),
                };

                if (!f || kfrustum_intersects_aabb(f, &g_center, &half_extents)) {
                    // Add it to the list to be rendered.
                    geometry_render_data data = {0};
                    data.model = model;
                    data.material = *m_inst;
                    data.vertex_count = g->vertex_count;
                    data.vertex_buffer_offset = g->vertex_buffer_offset;
                    data.index_count = g->index_count;
                    data.index_buffer_offset = g->index_buffer_offset;
                    data.unique_id = 0; // m->id.uniqueid; FIXME: needed for per-pixel selection
                    data.winding_inverted = winding_inverted;

                    // Check if transparent. If so, put into a separate, temp array to be
                    // sorted by distance from the camera. Otherwise, put into the
                    // out_geometries array directly.
                    b8 has_transparency = kmaterial_flag_get(engine_systems_get()->material_system, m_inst->base_material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT);
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

b8 scene_terrain_render_data_query(const scene* scene, const kfrustum* f, vec3 center, frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_terrain_geometries) {
    if (!scene) {
        return false;
    }
    if (scene->state > SCENE_STATE_LOADED) {
        *out_count = 0;
        return true;
    }

    u32 terrain_count = darray_length(scene->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        terrain* t = &scene->terrains[i];
        scene_attachment* attachment = &scene->terrain_attachments[i];
        ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
        mat4 model = ktransform_world_get(ktransform_handle);

        // TODO: Cache this somewhere instead of calculating all the time.
        f32 determinant = mat4_determinant(model);
        b8 winding_inverted = determinant < 0;

        // Check each chunk to see if it is in view.
        for (u32 c = 0; c < t->chunk_count; c++) {
            terrain_chunk* chunk = &t->chunks[c];

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

                if (!f || kfrustum_intersects_aabb(f, &g_center, &half_extents)) {
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

/**
 * @brief Gets a count and optionally an array of water planes from the given scene.
 *
 * @param scene A constant pointer to the scene.
 * @param f A constant pointer to the frustum to use for culling.
 * @param center The center view point.
 * @param p_frame_data A pointer to the current frame's data.
 * @param out_count A pointer to hold the count.
 * @param out_water_planes A pointer to an array of pointers to water planes. Pass 0 if just obtaining the count.
 * @return True on success; otherwise false.
 */
b8 scene_water_plane_query(const scene* scene, const kfrustum* f, vec3 center, frame_data* p_frame_data, u32* out_count, water_plane*** out_water_planes) {
    if (!scene) {
        return false;
    }
    *out_count = 0;
    if (scene->state > SCENE_STATE_LOADED) {
        return true;
    }

    u32 count = 0;
    u32 water_plane_count = darray_length(scene->water_planes);
    for (u32 i = 0; i < water_plane_count; ++i) {
        if (out_water_planes) {
            // scene_attachment* attachment = &scene->mesh_attachments[i];
            // ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
            // mat4 model = ktransform_world_get(ktransform_handle);

            water_plane* wp = &scene->water_planes[i];
            scene_attachment* attachment = &scene->water_plane_attachments[i];
            ktransform ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, attachment->hierarchy_node_handle);
            // FIXME: World should work here, but for some reason isn't being updated...
            wp->model = ktransform_local_get(ktransform_handle);
            darray_push(*out_water_planes, wp);
        }
        count++;
    }

    *out_count = count;

    return true;
}

b8 scene_node_ktransform_get_by_name(const scene* scene, kname name, ktransform* out_ktransform_handle) {
    if (!scene || !name || !out_ktransform_handle) {
        return false;
    }

    for (u32 i = 0; i < scene->node_metadata_count; ++i) {
        scene_node_metadata* node_meta = &scene->node_metadata[i];
        if (node_meta->name == name) {
            khierarchy_node hierarchy_node_handle = node_meta->index;
            *out_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, hierarchy_node_handle);
            return true;
        }
    }

    return false;
}

b8 scene_node_ktransform_get(const scene* scene, khierarchy_node node_handle, ktransform* out_ktransform_handle) {
    if (!scene || node_handle == KHIERARCHY_NODE_INVALID || !out_ktransform_handle) {
        return false;
    }

    *out_ktransform_handle = hierarchy_graph_ktransform_handle_get(&scene->hierarchy, node_handle);
    return true;
}

b8 scene_node_local_matrix_get(const scene* scene, khierarchy_node node_handle, mat4* out_matrix) {
    if (!scene || node_handle == KHIERARCHY_NODE_INVALID || !out_matrix) {
        return false;
    }

    scene_node_metadata* node_meta = &scene->node_metadata[node_handle];
    khierarchy_node hierarchy_node_handle = node_meta->index;
    return hierarchy_graph_ktransform_local_matrix_get(&scene->hierarchy, hierarchy_node_handle, out_matrix);
}

b8 scene_node_local_matrix_get_by_name(const scene* scene, kname name, mat4* out_matrix) {
    if (!scene || !name || !out_matrix) {
        return false;
    }

    for (u32 i = 0; i < scene->node_metadata_count; ++i) {
        scene_node_metadata* node_meta = &scene->node_metadata[i];
        if (node_meta->name == name) {
            khierarchy_node hierarchy_node_handle = node_meta->index;
            return scene_node_local_matrix_get(scene, hierarchy_node_handle, out_matrix);
        }
    }

    return false;
}

b8 scene_node_exists(const scene* s, kname name) {
    if (!s || !name) {
        return false;
    }

    for (u32 i = 0; i < s->node_metadata_count; ++i) {
        scene_node_metadata* node_meta = &s->node_metadata[i];
        if (node_meta->name == name) {
            return true;
        }
    }

    return false;
}

b8 scene_node_child_count_get(const scene* s, kname name, u32* out_child_count) {
    if (!s || !name || !out_child_count) {
        return false;
    }

    // TODO: BST lookup
    for (u32 i = 0; i < s->node_metadata_count; ++i) {
        scene_node_metadata* node_meta = &s->node_metadata[i];
        if (node_meta->name == name) {
            khierarchy_node hierarchy_node_handle = node_meta->index;
            *out_child_count = hierarchy_graph_child_count_get(&s->hierarchy, hierarchy_node_handle);
            return true;
        }
    }

    return false;
}

b8 scene_node_child_name_get_by_index(const scene* s, kname name, u32 index, kname* out_child_name) {
    if (!s || !name || !out_child_name) {
        return false;
    }

    // TODO: BST lookup
    for (u32 i = 0; i < s->node_metadata_count; ++i) {
        scene_node_metadata* node_meta = &s->node_metadata[i];
        if (node_meta->name == name) {
            khierarchy_node parent_node_handle = node_meta->index;

            khierarchy_node child_handle = {0};
            if (hierarchy_graph_child_get_by_index(&s->hierarchy, parent_node_handle, index, &child_handle)) {
                scene_node_metadata* child_meta = &s->node_metadata[child_handle];
                *out_child_name = child_meta->name;
                return true;
            }
        }
    }

    return false;
}

static void scene_actual_unload(scene* s) {
    u32 skybox_count = darray_length(s->skyboxes);
    for (u32 i = 0; i < skybox_count; ++i) {
        if (!skybox_unload(&s->skyboxes[i])) {
            KERROR("Failed to unload skybox");
        }
        skybox_destroy(&s->skyboxes[i]);
        s->skyboxes[i].state = SKYBOX_STATE_UNDEFINED;
    }

    u32 mesh_count = darray_length(s->static_meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (s->static_meshes[i].instance_id != INVALID_ID_U16) {
            // Unload any debug data.
            // TODO: debug data
            /* if (s->static_meshes[i].debug_data) {
                scene_debug_data* debug = s->static_meshes[i].debug_data;

                debug_box3d_unload(&debug->box);
                debug_box3d_destroy(&debug->box);

                kfree(s->static_meshes[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                s->static_meshes[i].debug_data = 0;
            } */

            static_mesh_instance_release(engine_systems_get()->static_mesh_system, &s->static_meshes[i]);

            s->static_meshes->instance_id = INVALID_ID_U16;
        }
    }

    u32 terrain_count = darray_length(s->terrains);
    for (u32 i = 0; i < terrain_count; ++i) {
        if (!terrain_unload(&s->terrains[i])) {
            KERROR("Failed to unload terrain.");
        }
        terrain_destroy(&s->terrains[i]);
    }

    // Debug grid.
    if (!debug_grid_unload(&s->grid)) {
        KWARN("Debug grid unload failed.");
    }

    u32 directional_light_count = darray_length(s->dir_lights);
    for (u32 i = 0; i < directional_light_count; ++i) {
        if (!light_system_directional_remove(&s->dir_lights[i])) {
            KERROR("Failed to unload/remove directional light.");
        }
        s->dir_lights[i].generation = INVALID_ID;

        if (s->dir_lights[i].debug_data) {
            scene_debug_data* debug = (scene_debug_data*)s->dir_lights[i].debug_data;
            // Unload directional light line data.
            debug_line3d_unload(&debug->line);
            debug_line3d_destroy(&debug->line);
            kfree(s->dir_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            s->dir_lights[i].debug_data = 0;
        }
    }

    u32 p_light_count = darray_length(s->point_lights);
    for (u32 i = 0; i < p_light_count; ++i) {
        if (!light_system_point_remove(&s->point_lights[i])) {
            KWARN("Failed to remove point light from light system.");
        }

        // Destroy debug data if it exists.
        if (s->point_lights[i].debug_data) {
            scene_debug_data* debug = (scene_debug_data*)s->point_lights[i].debug_data;
            debug_box3d_unload(&debug->box);
            debug_box3d_destroy(&debug->box);
            kfree(s->point_lights[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            s->point_lights[i].debug_data = 0;
        }
    }

    u32 audio_emitter_count = darray_length(s->audio_emitters);
    for (u32 i = 0; i < audio_emitter_count; ++i) {
        // Unload the emitter.
        kaudio_emitter_unload(engine_systems_get()->audio_system, s->audio_emitters[i].emitter);

        // Destroy debug data if it exists.
        if (s->audio_emitters[i].debug_data) {
            scene_debug_data* debug = (scene_debug_data*)s->audio_emitters[i].debug_data;
            debug_sphere3d_unload(&debug->sphere);
            debug_sphere3d_destroy(&debug->sphere);
            kfree(s->audio_emitters[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
            s->audio_emitters[i].debug_data = 0;
        }
    }

    u32 water_plane_count = darray_length(s->water_planes);
    for (u32 i = 0; i < water_plane_count; ++i) {
        if (!water_plane_unload(&s->water_planes[i])) {
            KERROR("Failed to unload water plane");
        }
        water_plane_destroy(&s->water_planes[i]);
        // s->water_planes[i].state = WATER_PLANE_STATE_UNDEFINED;
    }

    {
        u32 volume_count = darray_length(s->volumes);
        for (u32 i = 0; i < volume_count; ++i) {

            // Destroy debug data if it exists.
            if (s->volumes[i].debug_data) {
                scene_debug_data* debug = (scene_debug_data*)s->volumes[i].debug_data;
                switch (s->volumes[i].shape_type) {
                case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                    debug_sphere3d_unload(&debug->sphere);
                    debug_sphere3d_destroy(&debug->sphere);
                    break;
                case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                    debug_box3d_unload(&debug->box);
                    debug_box3d_destroy(&debug->box);
                    break;
                }
                kfree(s->volumes[i].debug_data, sizeof(scene_debug_data), MEMORY_TAG_RESOURCE);
                s->volumes[i].debug_data = 0;
            }
        }
    }

    // Destroy the hierarchy graph.
    hierarchy_graph_destroy(&s->hierarchy);

    // Update the state to show the scene is unloaded.
    s->state = SCENE_STATE_UNLOADED;

    KDEBUG("Scene unloading done.");
}

static b8 scene_serialize_node(const scene* s, const hierarchy_graph_view* view, const hierarchy_graph_view_node* view_node, kson_property* node) {
    if (!s || !view || !view_node) {
        return false;
    }

    // Serialize top-level node metadata, etc.
    scene_node_metadata* node_meta = &s->node_metadata[view_node->node_handle];

    // Node name
    kson_object_value_add_kname_as_string(&node->value.o, "name", node_meta->name);

    // ktransform is optional, so make sure there is a valid handle to one before serializing.
    if (view_node->ktransform_handle != KTRANSFORM_INVALID) {
        kson_object_value_add_string(&node->value.o, "ktransform", ktransform_to_string(view_node->ktransform_handle));
    }

    // Attachments
    kson_property attachments_prop = {0};
    attachments_prop.type = KSON_PROPERTY_TYPE_ARRAY;
    attachments_prop.name = kstring_id_create("attachments");
    attachments_prop.value.o.type = KSON_OBJECT_TYPE_ARRAY;
    attachments_prop.value.o.properties = darray_create(kson_property);

    // Look through each attachment type and see if the hierarchy_node_handle matches the node
    // handle of the current node being serialized. If it does, use the resource_handle to
    // index into the resource and/or metadata arrays to obtain needed data.
    // TODO: A relational view that allows for easy lookups of attachments for a particular node.

    // Meshes
    u32 mesh_count = darray_length(s->mesh_attachments);
    for (u32 m = 0; m < mesh_count; ++m) {
        if (s->mesh_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "static_mesh");
            kson_object_value_add_kname_as_string(&attachment.value.o, "asset_name", s->mesh_metadata[m].resource_name);
            kson_object_value_add_kname_as_string(&attachment.value.o, "package_name", s->mesh_metadata[m].package_name);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // skyboxes
    u32 skybox_count = darray_length(s->skybox_attachments);
    for (u32 m = 0; m < skybox_count; ++m) {
        if (s->skybox_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "skybox");
            kson_object_value_add_kname_as_string(&attachment.value.o, "cubemap_image_asset_name", s->skybox_metadata[m].cubemap_name);
            kson_object_value_add_kname_as_string(&attachment.value.o, "cubemap_image_asset_package_name", s->mesh_metadata[m].package_name);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // Terrains
    u32 terrain_count = darray_length(s->terrain_attachments);
    for (u32 m = 0; m < terrain_count; ++m) {
        if (s->terrain_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "terrain");
            kson_object_value_add_kname_as_string(&attachment.value.o, "name", s->terrain_metadata[m].name);
            kson_object_value_add_kname_as_string(&attachment.value.o, "asset_name", s->terrain_metadata[m].resource_name);
            kson_object_value_add_kname_as_string(&attachment.value.o, "package_name", s->terrain_metadata[m].package_name);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // Audio emitters
    u32 audio_emitter_count = darray_length(s->audio_emitter_attachments);
    for (u32 m = 0; m < audio_emitter_count; ++m) {
        if (s->audio_emitter_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            KASSERT_MSG(false, "Implement serialization of audio emitters");
            /* kson_object_value_add_string(&attachment.value.o, "type", "audio_emitter");
            kson_object_value_add_float(&attachment.value.o, "inner_radius", s->audio_emitters[m].data.inner_radius);
            kson_object_value_add_float(&attachment.value.o, "outer_radius", s->audio_emitters[m].data.outer_radius);
            kson_object_value_add_float(&attachment.value.o, "falloff", s->audio_emitters[m].data.falloff); */

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // Point lights
    u32 point_light_count = darray_length(s->point_light_attachments);
    for (u32 m = 0; m < point_light_count; ++m) {
        if (s->point_light_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "point_light");
            kson_object_value_add_string(&attachment.value.o, "colour", vec4_to_string(s->point_lights[m].data.colour));

            // NOTE: use the base light position, not the .data.positon since .data.position is the
            // recalculated world position based on inherited transforms form parent node(s).
            kson_object_value_add_string(&attachment.value.o, "position", vec4_to_string(s->point_lights[m].position));
            kson_object_value_add_float(&attachment.value.o, "constant_f", s->point_lights[m].data.constant_f);
            kson_object_value_add_float(&attachment.value.o, "linear", s->point_lights[m].data.linear);
            kson_object_value_add_float(&attachment.value.o, "quadratic", s->point_lights[m].data.quadratic);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // Directional lights
    u32 directional_light_count = darray_length(s->directional_light_attachments);
    for (u32 m = 0; m < directional_light_count; ++m) {
        if (s->directional_light_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "directional_light");
            kson_object_value_add_string(&attachment.value.o, "colour", vec4_to_string(s->dir_lights[m].data.colour));
            kson_object_value_add_string(&attachment.value.o, "direction", vec4_to_string(s->dir_lights[m].data.direction));
            kson_object_value_add_float(&attachment.value.o, "shadow_distance", s->dir_lights[m].data.shadow_distance);
            kson_object_value_add_float(&attachment.value.o, "shadow_fade_distance", s->dir_lights[m].data.shadow_fade_distance);
            kson_object_value_add_float(&attachment.value.o, "shadow_split_mult", s->dir_lights[m].data.shadow_split_mult);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // water planes
    u32 water_plane_count = darray_length(s->water_plane_attachments);
    for (u32 m = 0; m < water_plane_count; ++m) {
        if (s->water_plane_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "water_plane");
            kson_object_value_add_int(&attachment.value.o, "reserved", s->water_plane_metadata[m].reserved);

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    // volumes
    u32 volume_count = darray_length(s->water_plane_attachments);
    for (u32 m = 0; m < volume_count; ++m) {
        if (s->volume_attachments[m].hierarchy_node_handle == view_node->node_handle) {
            // Found one!

            // Create the object array entry.
            kson_property attachment = kson_object_property_create(0);

            // Add properties to it.
            kson_object_value_add_string(&attachment.value.o, "type", "volume");
            switch (s->volumes[m].type) {
            case SCENE_VOLUME_TYPE_TRIGGER:
                kson_object_value_add_string(&attachment.value.o, "volume_type", "trigger");
                break;
            }

            switch (s->volumes[m].shape_type) {
            case SCENE_VOLUME_SHAPE_TYPE_SPHERE:
                kson_object_value_add_string(&attachment.value.o, "shape_type", "sphere");
                kson_object_value_add_float(&attachment.value.o, "radius", s->volumes[m].shape_config.radius);
                break;
            case SCENE_VOLUME_SHAPE_TYPE_RECTANGLE:
                kson_object_value_add_string(&attachment.value.o, "shape_type", "rectangle");
                kson_object_value_add_vec3(&attachment.value.o, "extents", s->volumes[m].shape_config.extents);
                break;
            }

            if (s->volumes[m].on_enter_command) {
                kson_object_value_add_string(&attachment.value.o, "on_enter", s->volumes[m].on_enter_command);
            }

            if (s->volumes[m].on_leave_command) {
                kson_object_value_add_string(&attachment.value.o, "on_leave", s->volumes[m].on_leave_command);
            }

            if (s->volumes[m].on_update_command) {
                kson_object_value_add_string(&attachment.value.o, "on_update", s->volumes[m].on_update_command);
            }

            // Push it into the attachments array
            darray_push(attachments_prop.value.o.properties, attachment);
        }
    }

    darray_push(node->value.o.properties, attachments_prop);

    // Serialize children
    if (view_node->children) {
        u32 child_count = darray_length(view_node->children);

        if (child_count > 0) {
            // Only create the children property if the node actually has them.
            kson_property children_prop = {0};
            children_prop.type = KSON_PROPERTY_TYPE_ARRAY;
            children_prop.name = kstring_id_create("children");
            children_prop.value.o.type = KSON_OBJECT_TYPE_ARRAY;
            children_prop.value.o.properties = darray_create(kson_property);
            for (u32 i = 0; i < child_count; ++i) {
                u32 index = view_node->children[i];
                hierarchy_graph_view_node* view_node = &view->nodes[index];

                kson_property child_node = {0};
                child_node.type = KSON_PROPERTY_TYPE_OBJECT;
                child_node.name = 0; // No name for array elements.
                child_node.value.o.type = KSON_OBJECT_TYPE_OBJECT;
                child_node.value.o.properties = darray_create(kson_property);

                if (!scene_serialize_node(s, view, view_node, &child_node)) {
                    KERROR("Failed to serialize node, see logs for details.");
                    return false;
                }

                // Add the node to the array.
                darray_push(children_prop.value.o.properties, child_node);
            }

            darray_push(node->value.o.properties, children_prop);
        }
    }

    return true;
}

b8 scene_save(scene* s) {
    if (!s) {
        KERROR("scene_save requires a valid pointer to a scene.");
        return false;
    }

    if (s->flags & SCENE_FLAG_READONLY) {
        KERROR("Cannot save scene that is marked as read-only.");
        return false;
    }

    kson_tree tree = {0};
    // The root of the tree.
    tree.root.type = KSON_OBJECT_TYPE_OBJECT;
    tree.root.properties = darray_create(kson_property);

    // Properties property
    kson_property properties = kson_object_property_create("properties");

    kson_object_value_add_kname_as_string(&properties.value.o, "name", s->name);
    kson_object_value_add_string(&properties.value.o, "description", s->description);

    darray_push(tree.root.properties, properties);

    // nodes
    kson_property nodes_prop = kson_array_property_create("nodes");

    hierarchy_graph_view* view = &s->hierarchy.view;
    if (view->root_indices) {
        u32 root_count = darray_length(view->root_indices);
        for (u32 i = 0; i < root_count; ++i) {
            u32 index = view->root_indices[i];
            hierarchy_graph_view_node* view_node = &view->nodes[index];

            kson_property node = {0};
            node.type = KSON_PROPERTY_TYPE_OBJECT;
            node.name = 0; // No name for array elements.
            node.value.o.type = KSON_OBJECT_TYPE_OBJECT;
            node.value.o.properties = darray_create(kson_property);

            if (!scene_serialize_node(s, view, view_node, &node)) {
                KERROR("Failed to serialize node, see logs for details.");
                return false;
            }

            // Add the node to the array.
            darray_push(nodes_prop.value.o.properties, node);
        }
    }

    // Push the nodes array object into the root properties.
    darray_push(tree.root.properties, nodes_prop);

    // Write the contents of the tree to a string.
    const char* file_content = kson_tree_to_string(&tree);
    KTRACE("File content: \n%s", file_content);

    // Cleanup the tree.
    kson_tree_cleanup(&tree);

    // Write to file

    b8 result = true;
    // TODO: request the asset system to write the scene asset - (via resource system?)
    // TODO: Validate resource path and/or retrieve based on resource type and resource_name.
    /* KINFO("Writing scene '%s' to file '%s'...", s->name, s->resource_full_path);
    file_handle f;
    if (!filesystem_open(s->resource_full_path, FILE_MODE_WRITE, false, &f)) {
        KERROR("scene_save - unable to open scene file for writing: '%s'.", s->resource_full_path);
        result = false;
        goto scene_save_file_cleanup;
    }

    u32 content_length = string_length(file_content);
    u64 bytes_written = 0;
    result = filesystem_write(&f, sizeof(char) * content_length, file_content, &bytes_written);
    if (!result) {
        KERROR("Failed to write scene file.");
    } */

    /* scene_save_file_cleanup: */
    string_free((char*)file_content);

    // Close the file.
    /* filesystem_close(&f); */
    return result;
}

static void scene_node_metadata_ensure_allocated(scene* s, khierarchy_node node) {
    if (s && node != KHIERARCHY_NODE_INVALID) {
        u64 new_count = node + 1;
        if (s->node_metadata_count < new_count) {
            scene_node_metadata* new_array = kallocate(sizeof(scene_node_metadata) * new_count, MEMORY_TAG_SCENE);
            if (s->node_metadata) {
                kcopy_memory(new_array, s->node_metadata, sizeof(scene_node_metadata) * s->node_metadata_count);
                kfree(s->node_metadata, sizeof(scene_node_metadata) * s->node_metadata_count, MEMORY_TAG_SCENE);
            }
            s->node_metadata = new_array;

            // Invalidate all new entries.
            for (u32 i = s->node_metadata_count; i < new_count; ++i) {
                s->node_metadata[i].index = INVALID_ID;
                s->node_metadata[i].name = INVALID_KNAME;
            }

            s->node_metadata_count = new_count;
        }
    } else {
        KWARN("scene_node_metadata_ensure_allocated requires a valid pointer to a scene, and a valid handle index.");
    }
}
