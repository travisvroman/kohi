#include "kscene.h"

#include <containers/darray.h>
#include <math/kmath.h>
#include <strings/kstring.h>

#include "core/engine.h"
#include "core_resource_types.h"
#include "defines.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "systems/kresource_system.h"

static u32 get_node_config_attachment_count_r(scene_node_config* node_config, u32 total_attachment_count) {
    // FIXME: Probably need to rework the way that these configs are handled at this level to
    // make this easier to traverse.
    if (node_config->audio_emitter_configs) {
        total_attachment_count += darray_length(node_config->audio_emitter_configs);
    }
    if (node_config->dir_light_configs) {
        total_attachment_count += darray_length(node_config->dir_light_configs);
    }
    if (node_config->point_light_configs) {
        total_attachment_count += darray_length(node_config->point_light_configs);
    }
    if (node_config->heightmap_terrain_configs) {
        total_attachment_count += darray_length(node_config->heightmap_terrain_configs);
    }
    if (node_config->skybox_configs) {
        total_attachment_count += darray_length(node_config->skybox_configs);
    }
    if (node_config->static_mesh_configs) {
        total_attachment_count += darray_length(node_config->static_mesh_configs);
    }
    if (node_config->water_plane_configs) {
        total_attachment_count += darray_length(node_config->water_plane_configs);
    }
    if (node_config->volume_configs) {
        total_attachment_count += darray_length(node_config->volume_configs);
    }
    if (node_config->hit_sphere_configs) {
        total_attachment_count += darray_length(node_config->hit_sphere_configs);
    }

    // Recurse children.
    if (node_config->children && node_config->child_count) {
        for (u32 i = 0; i < node_config->child_count; ++i) {
            total_attachment_count += get_node_config_attachment_count_r(&node_config->children[i], total_attachment_count);
        }
    }

    return total_attachment_count;
}

b8 kscene_create(kresource_scene* config, kscene_flags flags, kscene* out_scene) {
    if (!config || !out_scene) {
        return false;
    }

    out_scene->state = KSCENE_STATE_UNINITIALIZED;
    out_scene->config = config;
    out_scene->name = out_scene->config->base.name;
    out_scene->flags = flags;

    return true;
}

b8 kscene_initialize(kscene* scene) {
    if (!scene) {
        return false;
    }

    if (scene->config->description) {
        scene->description = string_duplicate(scene->config->description);
    }

    // TODO: May want these values to be configurable, but for now base the original allocated sizes
    // on what the scene actually contains (with some padding), and expand if needed.
    u32 total_node_count = scene->config->node_count * 1.5;

    // Transforms
    {
        scene->transforms.allocated_count = total_node_count;
        // Track dirty transforms by id.
        scene->transforms.dirty_ids = darray_create(u32);

        scene->transforms.positions = KALLOC_TYPE_CARRAY(vec3, total_node_count);
        scene->transforms.scales = KALLOC_TYPE_CARRAY(vec3, total_node_count);
        scene->transforms.rotations = KALLOC_TYPE_CARRAY(quat, total_node_count);
        scene->transforms.world_matrices = KALLOC_TYPE_CARRAY(mat4, total_node_count);
        for (u32 i = 0; i < total_node_count; ++i) {
            scene->transforms.positions[i] = vec3_zero();
            scene->transforms.scales[i] = vec3_one();
            scene->transforms.rotations[i] = quat_identity();
            scene->transforms.world_matrices[i] = mat4_identity();
        }
    }

    // Nodes
    {
        scene->nodes.allocated_count = total_node_count;
        scene->nodes.names = KALLOC_TYPE_CARRAY(kname, total_node_count);
        scene->nodes.uniqueids = KALLOC_TYPE_CARRAY(u64, total_node_count);
        scene->nodes.parent_ids = KALLOC_TYPE_CARRAY(u32, total_node_count);
        scene->nodes.first_child_ids = KALLOC_TYPE_CARRAY(u32, total_node_count);
        scene->nodes.next_sibling_ids = KALLOC_TYPE_CARRAY(u32, total_node_count);
        scene->nodes.transform_ids = KALLOC_TYPE_CARRAY(u32, total_node_count);
        for (u32 i = 0; i < total_node_count; ++i) {
            scene->nodes.names[i] = INVALID_KNAME;
            scene->nodes.uniqueids[i] = INVALID_ID_U64;
            scene->nodes.parent_ids[i] = INVALID_ID;
            scene->nodes.first_child_ids[i] = INVALID_ID;
            scene->nodes.next_sibling_ids[i] = INVALID_ID;
            scene->nodes.transform_ids[i] = INVALID_ID;
        }
    }

    // Node Tags
    {
        scene->node_tags.allocated_count = 10; // NOTE: Could iterate all nodes and count tags instead...
        scene->node_tags.names = KALLOC_TYPE_CARRAY(kname, scene->node_tags.allocated_count);
        scene->node_tags.node_ids = KALLOC_TYPE_CARRAY(u32*, scene->node_tags.allocated_count);
        for (u32 i = 0; i < scene->node_tags.allocated_count; ++i) {
            scene->node_tags.names[i] = INVALID_KNAME;
            scene->node_tags.node_ids[i] = 0;
        }
    }

    // Attachments
    {
        // Traverse the config and get a total number of attachments for all nodes, recursively.
        u32 total_attachment_count = 0;
        for (u32 i = 0; i < total_node_count; ++i) {
            total_attachment_count += get_node_config_attachment_count_r(&scene->config->nodes[i], total_attachment_count);
        }

        scene->attachments.allocated_count = total_attachment_count * 1.5; // Allocate with some room for more.
        scene->attachments.names = KALLOC_TYPE_CARRAY(kname, scene->attachments.allocated_count);
        scene->attachments.owner_node_ids = KALLOC_TYPE_CARRAY(u32, scene->attachments.allocated_count);
        for (u32 i = 0; i < scene->attachments.allocated_count; ++i) {
            scene->attachments.owner_node_ids[i] = INVALID_ID;
            scene->attachments.names[i] = INVALID_KNAME;
        }
    }

    // Attachment types
    {
        // Setup type info for known attachment types first.
        scene->attachment_types.allocated_count = KSCENE_KNOWN_ATTACHMENT_TYPE_COUNT;
        scene->attachment_types.names = KALLOC_TYPE_CARRAY(kname, scene->attachment_types.allocated_count);
        scene->attachment_types.attachment_ids = KALLOC_TYPE_CARRAY(u32*, scene->attachment_types.allocated_count);
    }

    scene->state = KSCENE_STATE_INITIALIZED;

    return true;
}
b8 kscene_load(kscene* scene) {
    if (!scene) {
        return false;
    }

    scene->state = KSCENE_STATE_LOADING;

    // TODO: kick off loading (jobify?)

    scene->state = KSCENE_STATE_LOADED;

    return true;
}
void kscene_unload(kscene* scene) {
    if (scene) {
        scene->state = KSCENE_STATE_UNLOADING;

        // TODO: unload all the things.
        //
        scene->state = KSCENE_STATE_UNLOADED;
    }
}
void kscene_destroy(kscene* scene) {
    if (scene) {
        // Release the config.
        if (scene->config) {
            kresource_system_release(engine_systems_get()->kresource_state, scene->config->base.name);
            scene->config = 0;
        }

        if (scene->description) {
            string_free(scene->description);
        }

        scene->state = KSCENE_STATE_UNINITIALIZED;
    }
}

b8 kscene_update(kscene* scene, const struct frame_data* p_frame_data) {
    if (!scene) {
        return false;
    }

    scene->state = KSCENE_STATE_INITIALIZED;

    return true;
}

b8 kscene_node_exists(kscene* scene, kname name);
b8 kscene_node_get(kscene* scene, khandle* out_node);
b8 kscene_node_has_transform(kscene* scene, khandle node);
b8 kscene_node_has_children(kscene* scene, khandle node);
b8 kscene_node_child_count_get(kscene* scene, khandle node, u32* out_count);

b8 kscene_node_local_transform_get(kscene* scene, khandle node, mat4* out_local_transform);
b8 kscene_node_world_transform_get(kscene* scene, khandle node, mat4* out_world_transform);

b8 kscene_node_children_traverse(kscene* scene, khandle parent_node, PFN_kscene_node_traverse_callback callback);

b8 kscene_node_create(kscene* scene, kname name, khandle parent_node, khandle* out_node);
b8 kscene_node_create_with_position(kscene* scene, kname name, khandle parent_node, vec3 position, khandle* out_node);
b8 kscene_node_create_with_rotation(kscene* scene, kname name, khandle parent_node, quat rotation, khandle* out_node);
b8 kscene_node_create_with_scale(kscene* scene, kname name, khandle parent_node, vec3 scale, khandle* out_node);
b8 kscene_node_create_with_position_rotation(kscene* scene, kname name, khandle parent_node, vec3 position, quat rotation, khandle* out_node);
b8 kscene_node_create_with_position_rotation_scale(kscene* scene, kname name, khandle parent_node, vec3 position, quat rotation, vec3 scale, khandle* out_node);

// Gets a handle to an attachment of the given node by name.
b8 kscene_node_attachment_get(kscene* scene, khandle node, kname attachment_name, khandle* out_attachment);
// Adds an attachment to the given node.
b8 kscene_node_attachment_add(kscene* scene, khandle node, khandle attachment);
b8 kscene_node_attachment_remove(kscene* scene, khandle node, khandle attachment);

b8 kscene_node_child_remove(kscene* scene, khandle parent_node, khandle child_node);

// Also recursively destroys children.
b8 kscene_node_destroy(kscene* scene, khandle node);

b8 kscene_node_name_set(kscene* scene, khandle node, kname name);
b8 kscene_node_parent_set(kscene* scene, khandle node, khandle parent_node);

b8 kscene_node_position_set(kscene* scene, khandle node, vec3 position);
b8 kscene_node_translate(kscene* scene, khandle node, vec3 translation);

b8 kscene_node_rotation_set(kscene* scene, khandle node, quat rotation);
b8 kscene_node_rotate(kscene* scene, khandle node, quat rotation);

b8 kscene_node_scale_set(kscene* scene, khandle node, vec3 scale);
b8 kscene_node_scale(kscene* scene, khandle node, vec3 scale);

b8 kscene_node_position_rotation_set(kscene* scene, khandle node, vec3 position, quat rotation);
b8 kscene_node_translate_rotate(kscene* scene, khandle node, vec3 translation, quat rotation);

b8 kscene_node_position_rotation_scale_set(kscene* scene, khandle node, vec3 position, quat rotation, vec3 scale);
b8 kscene_node_translate_rotate_scale(kscene* scene, khandle node, vec3 translation, quat rotation, vec3 scale);

b8 kscene_attachment_create(kscene* scene, kname name, kscene_attachment_type type, khandle owning_node, khandle* out_attachment);
// Auto unloads, detaches, and destroys.
b8 kscene_attachment_destroy(kscene* scene, khandle attachment);

// Traverse all attachments in scene of a given type.
b8 kscene_attachment_traverse_by_type(kscene* scene, kscene_attachment_type type, PFN_kscene_attachment_traverse_callback callback);

b8 kscene_save(kscene* scene);
