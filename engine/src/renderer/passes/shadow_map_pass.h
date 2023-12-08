
#ifndef _SHADOW_MAP_PASS_H_
#define _SHADOW_MAP_PASS_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"

struct rendergraph_pass;
struct frame_data;
struct texture;

typedef struct shadow_map_pass_extended_data {
    mat4 projection;
    struct directional_light* light;
    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;
    u32 geometry_count;
    struct geometry_render_data* geometries;
} shadow_map_pass_extended_data;

typedef struct shadow_map_pass_config {
    u16 resolution;
    f32 near_clip;
    f32 far_clip;
    renderer_projection_matrix_type matrix_type;
    rect_2d bounds;
    // Used for non-orthographic projections.
    f32 fov;
} shadow_map_pass_config;

KAPI b8 shadow_map_pass_create(struct rendergraph_pass* self, void* config);
KAPI b8 shadow_map_pass_initialize(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_load_resources(struct rendergraph_pass* self);
KAPI b8 shadow_map_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
KAPI void shadow_map_pass_destroy(struct rendergraph_pass* self);

KAPI struct texture* shadow_map_pass_attachment_texture_get(struct rendergraph_pass* self, enum render_target_attachment_type attachment_type, u8 frame_number);

#endif
