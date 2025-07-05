#ifndef _FORWARD_RENDERGRAPH_NODE_H_
#define _FORWARD_RENDERGRAPH_NODE_H_

#include "defines.h"
#include "math/math_types.h"
#include "renderer/viewport.h"

struct rendergraph;
struct rendergraph_node;
struct rendergraph_node_config;
struct frame_data;
struct kresource_texture;

struct directional_light;
struct geometry_render_data;
struct viewport;
struct water_plane;
struct skybox;

KAPI b8 forward_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config);
KAPI b8 forward_rendergraph_node_initialize(struct rendergraph_node* self);
KAPI b8 forward_rendergraph_node_load_resources(struct rendergraph_node* self);
KAPI b8 forward_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data);
KAPI void forward_rendergraph_node_destroy(struct rendergraph_node* self);
KAPI void forward_rendergraph_node_reset(struct rendergraph_node* self);

KAPI b8 forward_rendergraph_node_render_mode_set(struct rendergraph_node* self, u32 render_mode);
KAPI b8 forward_rendergraph_node_directional_light_set(struct rendergraph_node* self, const struct directional_light* light);
KAPI b8 forward_rendergraph_node_cascade_data_set(struct rendergraph_node* self, f32 split, mat4 dir_light_space, u8 cascade_index);
KAPI b8 forward_rendergraph_node_static_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries);
KAPI void forward_rendergraph_node_set_skybox(struct rendergraph_node* self, struct skybox* sb);
KAPI b8 forward_rendergraph_node_terrain_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries);
KAPI b8 forward_rendergraph_node_water_planes_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 count, struct water_plane** planes);
KAPI b8 forward_rendergraph_node_irradiance_texture_set(struct rendergraph_node* self, struct frame_data* p_frame_data, ktexture irradiance_cube_texture);

KAPI b8 forward_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v);
KAPI b8 forward_rendergraph_node_camera_projection_set(struct rendergraph_node* self, struct camera* view_camera, mat4 projection_matrix);

KAPI b8 forward_rendergraph_node_register_factory(void);

#endif
