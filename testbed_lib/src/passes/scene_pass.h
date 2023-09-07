#ifndef _SCENE_PASS_H_
#define _SCENE_PASS_H_

#include "defines.h"
#include "math/math_types.h"

struct rendergraph_pass;
struct frame_data;

struct geometry_render_data;

typedef struct scene_pass_extended_data {
    u32 render_mode;
    vec4 ambient_colour;

    u32 geometry_count;
    struct geometry_render_data* geometries;

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;

    u32 debug_geometry_count;
    struct geometry_render_data* debug_geometries;
} scene_pass_extended_data;

b8 scene_pass_create(struct rendergraph_pass* self);
b8 scene_pass_initialize(struct rendergraph_pass* self);
b8 scene_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
void scene_pass_destroy(struct rendergraph_pass* self);

#endif
