#include "zone_system.h"
#include "containers/u64_bst.h"
#include "core/engine.h"
#include "core/event.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "resources/scene.h"
#include "soi_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"
#include "systems/xform_system.h"

b8 zone_system_deserialize_config(const char* config_str, zone_system_config* out_config) {
    if (!config_str || !out_config) {
        return false;
    };

    kson_tree temp = {0};
    if (!kson_tree_from_string(config_str, &temp)) {
        KERROR("Failed to parse zone system config. See logs for details.");
        return false;
    }

    kson_array zones_array = {0};
    if (!kson_object_property_value_get_array(&temp.root, "zones", &zones_array)) {
        KERROR("Required field 'zones' not found in zone config. Parsing failed.");
        return false;
    }

    if (!kson_array_element_count_get(&zones_array, &out_config->zone_count) || !out_config->zone_count) {
        KERROR("At least one zone is required in zone config.");
        return false;
    }

    out_config->zones = KALLOC_TYPE_CARRAY(zone_config, out_config->zone_count);

    for (u32 i = 0; i < out_config->zone_count; ++i) {
        kson_object zone_obj = {0};
        if (!kson_array_element_value_get_object(&zones_array, i, &zone_obj)) {
            KERROR("Failed to get zone object at index %u", i);
            return false;
        }

        zone_config* c = &out_config->zones[i];

        // Extract name. Required.
        if (!kson_object_property_value_get_string_as_kname(&zone_obj, "name", &c->name)) {
            KERROR("Failed to extract zone 'name', a required field. Index=%u", i);
            return false;
        }

        // Extract display name. Optional.
        if (!kson_object_property_value_get_string(&zone_obj, "name", &c->display_name)) {
            // If display name is not provided, warn about it, but fall back to using the name.
            c->display_name = string_duplicate(kname_string_get(c->name));
        }

        // Extract scene name. Required.
        if (!kson_object_property_value_get_string_as_kname(&zone_obj, "scene_name", &c->scene_name)) {
            KERROR("Failed to extract zone 'scene_name', a required field. Index=%u", i);
            return false;
        }

        // Extract scene package name. Optional.
        if (!kson_object_property_value_get_string_as_kname(&zone_obj, "scene_package_name", &c->scene_package_name)) {
            // If not provided, default to the game package name.
            c->scene_package_name = kname_create(PACKAGE_NAME_SOI);
        }
    }

    return true;
}

b8 zone_system_intialize(zone_system_state* state, const zone_system_config* config) {
    if (!state || !config) {
        return false;
    }

    if (!config->zone_count) {
        KERROR("Must have at least one zone.");
        return false;
    }

    state->lookup = 0;

    state->zone_count = config->zone_count;
    state->zones = KALLOC_TYPE_CARRAY(zone, state->zone_count);

    for (u32 i = 0; i < state->zone_count; ++i) {
        zone_config* c = &config->zones[i];
        zone* z = &state->zones[i];

        z->name = c->name;
        z->display_name = string_duplicate(c->display_name);
        z->scene_name = c->scene_name;
        z->scene_package_name = c->scene_package_name;

        // Add to the lookup by name.
        bt_node_value value = {0};
        value.u32 = i;
        bt_node* new_node = u64_bst_insert(state->lookup, c->name, value);
        if (!state->lookup) {
            state->lookup = new_node;
        }
    }

    return true;
}

void zone_system_shutdown(zone_system_state* state) {
    if (state) {
        // Make sure currently loaded zone(s) are unloaded.
        zone_system_unload_current(state, true);

        // Free zone resources.
        for (u32 i = 0; i < state->zone_count; ++i) {
            zone* z = &state->zones[i];

            if (z->spawn_point_count) {
                if (z->spawn_point_names) {
                    KFREE_TYPE_CARRAY(z->spawn_point_names, kname, z->spawn_point_count);
                }
                if (z->spawn_point_positions) {
                    KFREE_TYPE_CARRAY(z->spawn_point_positions, vec3, z->spawn_point_count);
                }
                if (z->spawn_point_rotations) {
                    KFREE_TYPE_CARRAY(z->spawn_point_rotations, quat, z->spawn_point_count);
                }
            }

            if (z->display_name) {
                string_free(z->display_name);
            }

            // Ensure the scene for each zone is destroyed.
            scene_destroy(&z->zone_scene);
        }

        // Destroy state
        KFREE_TYPE_CARRAY(state->zones, zone, state->zone_count);

        u64_bst_cleanup(state->lookup);

        kzero_memory(state, sizeof(zone_system_state));
    }
}

void zone_system_unload_current(zone_system_state* state, b8 immediate) {
    if (state && state->current_zone) {
        KDEBUG("Unloading zone '%s' scene...", state->current_zone->display_name);
        scene_unload(&state->current_zone->zone_scene, immediate);

        state->current_zone = 0;
    }
}

b8 zone_system_load(zone_system_state* state, kname zone_name, u8 spawn_point_id) {
    if (!state || zone_name == INVALID_KNAME) {
        return false;
    }

    // Lookup the zone.
    const bt_node* node = u64_bst_find(state->lookup, zone_name);
    if (!node) {
        KERROR("A zone named '%s' does not exist.", kname_string_get(zone_name));
        return false;
    }

    u32 index = node->value.u32;
    if (index >= state->zone_count) {
        KERROR("Zone '%s' lookup was successful, but is outside the range of available zones.", kname_string_get(zone_name));
        return false;
    }

    zone* z = &state->zones[index];

    if (z->zone_scene.state == SCENE_STATE_UNINITIALIZED || z->zone_scene.state == SCENE_STATE_UNLOADED) {
        KDEBUG("Loading zone '%s' ...", z->display_name);

        kresource_scene_request_info request_info = {0};
        request_info.base.type = KRESOURCE_TYPE_SCENE;
        request_info.base.synchronous = true; // HACK: use a callback instead.
        request_info.base.assets = array_kresource_asset_info_create(1);
        kresource_asset_info* asset = &request_info.base.assets.data[0];
        asset->type = KASSET_TYPE_SCENE;
        asset->asset_name = zone_name;
        asset->package_name = kname_create(PACKAGE_NAME_SOI);

        kresource_scene* scene_resource = (kresource_scene*)kresource_system_request(
            engine_systems_get()->kresource_state,
            zone_name,
            (kresource_request_info*)&request_info);
        if (!scene_resource) {
            KERROR("Failed to request zone scene resource. See logs for details.");
            return false;
        }

        // Create the scene.
        scene_flags scene_load_flags = 0;
        /* scene_load_flags |= SCENE_FLAG_READONLY;  // NOTE: to enable "editor mode", turn this flag off. */
        if (!scene_create(scene_resource, scene_load_flags, &z->zone_scene)) {
            KERROR("Failed to create zone scene");
            return false;
        }

        // Initialize
        if (!scene_initialize(&z->zone_scene)) {
            KERROR("Failed initialize zone scene, aborting game.");
            return false;
        }

        // Search for a node in the scene called "spawn_points" and
        // extract the position and rotation from each. This is required.
        {
            kname spawn_points_name = kname_create("spawn_points");
            if (!scene_node_exists(&z->zone_scene, spawn_points_name)) {
                KERROR("Zone does not contain required node named 'spawn_points'.");
                return false;
            }

            // Verify that there are spawn points
            if (!scene_node_child_count_get(&z->zone_scene, spawn_points_name, &z->spawn_point_count) || !z->spawn_point_count) {
                KERROR("Zone has 'spawn_points', but no children spawn points are actually defined.");
                return false;
            }

            z->spawn_point_names = KALLOC_TYPE_CARRAY(kname, z->spawn_point_count);
            z->spawn_point_positions = KALLOC_TYPE_CARRAY(vec3, z->spawn_point_count);
            z->spawn_point_rotations = KALLOC_TYPE_CARRAY(quat, z->spawn_point_count);

            // Extract spawn point position/rotation
            for (u32 i = 0; i < z->spawn_point_count; ++i) {
                // Get the name.
                if (!scene_node_child_name_get_by_index(&z->zone_scene, spawn_points_name, i, &z->spawn_point_names[i])) {
                    KERROR("Zone config reported having spawn points, but failed to extract spawn point name at index %u.", i);
                    return false;
                }

                // Get a handle to the xform.
                khandle child_xform_handle = khandle_invalid();
                if (!scene_node_xform_get_by_name(&z->zone_scene, z->spawn_point_names[i], &child_xform_handle)) {
                    KWARN("No xform exists for spawn point named '%s'. Using identity transform.", kname_string_get(z->spawn_point_names[i]))
                    z->spawn_point_positions[i] = vec3_zero();
                    z->spawn_point_rotations[i] = quat_identity();
                } else {
                    // Extract from the xform.
                    z->spawn_point_positions[i] = xform_position_get(child_xform_handle);
                    z->spawn_point_rotations[i] = xform_rotation_get(child_xform_handle);
                }
            }
        }

        // Actually load the scene.
        if (!scene_load(&z->zone_scene)) {
            KERROR("Error loading zone scene.");
            return false;
        }
    } else {
        // Already loaded???
        KWARN("The zone '%s' might already be loaded...", z->display_name);
    }

    // Set as the current zone.
    state->current_zone = z;

    // Fire off an event to notify listeners that a new zone has loaded.
    event_context evt = {0};
    evt.data.u8[0] = spawn_point_id; // Send along the spawn point id.
    event_fire(GAME_EVENT_CODE_ZONE_LOADED, z, evt);

    return true;
}

b8 zone_system_transition(zone_system_state* state, kname zone_name, u8 spawn_point_id) {
    // Unload current zone first.
    zone_system_unload_current(state, true);

    // Load the next zone.
    return zone_system_load(state, zone_name, spawn_point_id);
}

b8 zone_system_current_zone_spawn_get(zone_system_state* state, u8 spawn_point_id, vec3* out_position, quat* out_rotation) {
    if (!state || !out_position || !out_rotation || spawn_point_id == INVALID_ID_U8) {
        return false;
    }

    if (!state->current_zone) {
        KWARN("%s - no current zone loaded. Using identity transform.", __FUNCTION__);
        return true; // Still count as a success, with a warning.
    }
    zone* z = state->current_zone;

    if (spawn_point_id >= z->spawn_point_count) {
        KWARN("%s - spawn_point_id %u is outside the range of spawn points (0-%u) for zone %s. Defaulting to the first spawn point.", __FUNCTION__, spawn_point_id, z->spawn_point_count, z->display_name);
        *out_position = z->spawn_point_positions[0];
        *out_rotation = z->spawn_point_rotations[0];
        return true;
    }

    // Use the appropriate spawn point.
    *out_position = z->spawn_point_positions[spawn_point_id];
    *out_rotation = z->spawn_point_rotations[spawn_point_id];
    return true;
}
