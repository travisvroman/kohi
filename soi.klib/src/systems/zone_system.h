#pragma once

#include "containers/u64_bst.h"
#include "defines.h"
#include "resources/scene.h"
#include "strings/kname.h"

typedef struct zone {
    kname name;
    const char* display_name;
    kname scene_name;
    kname scene_package_name;

    scene zone_scene;

    u32 spawn_point_count;
    kname* spawn_point_names;
    vec3* spawn_point_positions;
    quat* spawn_point_rotations;
} zone;

typedef struct zone_system_state {
    u32 zone_count;
    zone* zones;

    // Lookup for zone by name. The kname is the key, value is index into zones array.
    bt_node* lookup;

    zone* current_zone;
} zone_system_state;

// Configuration for a given zone.
typedef struct zone_config {
    kname name;
    const char* display_name;
    kname scene_name;
    kname scene_package_name;
} zone_config;

// Configuration for the zone system.
typedef struct zone_system_config {
    u32 zone_count;
    zone_config* zones;

} zone_system_config;

b8 zone_system_deserialize_config(const char* config_str, zone_system_config* out_config);

b8 zone_system_intialize(zone_system_state* state, const zone_system_config* config);
void zone_system_shutdown(zone_system_state* state);

void zone_system_unload_current(zone_system_state* state, b8 immediate);
b8 zone_system_load(zone_system_state* state, kname zone_name, u8 spawn_point_id);

b8 zone_system_transition(zone_system_state* state, kname zone_name, u8 spawn_point_id);

b8 zone_system_current_zone_spawn_get(zone_system_state* state, u8 spawn_point_id, vec3* out_position, quat* out_rotation);
