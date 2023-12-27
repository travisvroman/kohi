
#ifndef _SHADOW_MAP_PASS_H_
#define _SHADOW_MAP_PASS_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"

struct rendergraph_pass;
struct frame_data;
struct texture;

#define MAX_CASCADE_COUNT 4

typedef struct shadow_map_cascade_data {
    mat4 projection;
    mat4 view;
    f32 split_depth;
    i32 cascade_index;
} shadow_map_cascade_data;

typedef struct shadow_map_pass_extended_data {
    // Per-cascade data.
    shadow_map_cascade_data cascades[MAX_CASCADE_COUNT];
    struct directional_light* light;
    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;
    u32 geometry_count;
    struct geometry_render_data* geometries;
} shadow_map_pass_extended_data;

typedef struct shadow_map_pass_config {
    u16 resolution;
} shadow_map_pass_config;

KAPI b8 shadow_map_pass_create(struct rendergraph_pass* self, void* config);
KAPI b8 shadow_map_pass_initialize(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_load_resources(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
KAPI void shadow_map_pass_destroy(struct rendergraph_pass* self);

KAPI struct texture* shadow_map_pass_attachment_texture_get(struct rendergraph_pass* self, enum render_target_attachment_type attachment_type, u8 frame_number);

#endif
