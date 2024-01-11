
#ifndef _SHADOW_MAP_PASS_H_
#define _SHADOW_MAP_PASS_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"

struct rendergraph_pass;
struct rendergraph_source;
struct frame_data;
struct texture;

#define MAX_CASCADE_COUNT 4

typedef struct shadow_map_cascade_data {
    mat4 projection;
    mat4 view;
    f32 split_depth;
    i32 cascade_index;
    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;
    u32 geometry_count;
    struct geometry_render_data* geometries;
} shadow_map_cascade_data;

typedef struct shadow_map_pass_extended_data {
    struct directional_light* light;
    // Per-cascade data.
    shadow_map_cascade_data cascades[MAX_CASCADE_COUNT];
} shadow_map_pass_extended_data;

typedef struct shadow_map_pass_config {
    u16 resolution;
} shadow_map_pass_config;

KAPI b8 shadow_map_pass_create(struct rendergraph_pass* self, void* config);
KAPI b8 shadow_map_pass_initialize(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_load_resources(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
KAPI void shadow_map_pass_destroy(struct rendergraph_pass* self);

KAPI b8 shadow_map_pass_source_populate(struct rendergraph_pass* self, struct rendergraph_source* source);
KAPI b8 shadow_map_pass_attachment_populate(struct rendergraph_pass* self, render_target_attachment* attachment);

#endif
