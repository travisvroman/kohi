
// TODO:
// - multi-axis rotations.
// - Before editing begins, a copy of the transform should be taken beforehand to allow canceling of the operation.
// - Canceling can be done by pressing the right mouse button while manipulating or by presseing esc.
// - Undo will be handled later by an undo stack.

#include "editor_gizmo.h"

#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <systems/ktransform_system.h>

#include "core/engine.h"
#include "systems/kcamera_system.h"
#include "systems/kmatrix_system.h"

static void create_gizmo_mode_none (editor_gizmo *gizmo);
static void create_gizmo_mode_move (editor_gizmo *gizmo);
static void create_gizmo_mode_scale (editor_gizmo *gizmo);
static void create_gizmo_mode_rotate (editor_gizmo *gizmo);

const static u8 segments = 32;
const static f32 radius = 1.0f;
const static f32 axis_thickness = 0.05f;
const static f32 arrowhead_length = 0.25f;
const static f32 arrowhead_size = 0.125f;
const static u8 axis_sides = 6;
const static f32 axis_length = 2.0f;
const static f32 box_axis_length = 0.4f;

b8 editor_gizmo_create (editor_gizmo *out_gizmo) {
	if (!out_gizmo) {
		KERROR("Unable to create gizmo with an invalid out pointer.");
		return false;
	}

	out_gizmo->mode = EDITOR_GIZMO_MODE_NONE;
	out_gizmo->ktransform_handle = ktransform_create(0);
	out_gizmo->selected_transform = KTRANSFORM_INVALID;
	// Default orientation.
	out_gizmo->orientation = EDITOR_GIZMO_ORIENTATION_GLOBAL;

	// Initialize default values for all modes.
	for (u32 i = 0; i < EDITOR_GIZMO_MODE_MAX + 1; ++i) {
		out_gizmo->mode_data[i].vertex_count = 0;
		out_gizmo->mode_data[i].vertices = 0;

		out_gizmo->mode_data[i].index_count = 0;
		out_gizmo->mode_data[i].indices = 0;
	}

	out_gizmo->render_projection_id = kmatrix_system_add(engine_systems_get()->matrix_system, KMATRIX_TYPE_PROJECTION, mat4_identity());
	out_gizmo->render_model_id = kmatrix_system_add(engine_systems_get()->matrix_system, KMATRIX_TYPE_TRANSFORM, mat4_identity());

	return true;
}

void editor_gizmo_destroy (editor_gizmo *gizmo) {
	if (gizmo) {
		editor_gizmo_unload(gizmo);

		u8 mode_count = EDITOR_GIZMO_MODE_MAX + 1;
		for (u8 i = 0; i < mode_count; ++i) {
			editor_gizmo_mode_data *data = &gizmo->mode_data[i];
			geometry_destroy(&data->geo);
			kfree(data->mode_extents);
		}
	}
}

b8 editor_gizmo_initialize (editor_gizmo *gizmo) {
	if (!gizmo) {
		return false;
	}

	gizmo->mode = EDITOR_GIZMO_MODE_NONE;

	create_gizmo_mode_none(gizmo);
	create_gizmo_mode_move(gizmo);
	create_gizmo_mode_scale(gizmo);
	create_gizmo_mode_rotate(gizmo);

	return true;
}

b8 editor_gizmo_load (editor_gizmo *gizmo) {
	if (!gizmo) {
		return false;
	}

	for (u32 i = 0; i < EDITOR_GIZMO_MODE_MAX + 1; ++i) {
		kgeometry *g = &gizmo->mode_data[i].geo;
		editor_gizmo_mode_data *mode = &gizmo->mode_data[i];

		g->type = KGEOMETRY_TYPE_3D_STATIC_COLOUR;
		g->vertex_count = mode->vertex_count;
		g->vertex_element_size = sizeof(colour_vertex_3d);
		g->vertex_buffer_offset = 0;
		g->vertices = mode->vertices;
		g->index_count = mode->index_count;
		g->index_element_size = sizeof(u32);
		g->indices = mode->indices;
		g->index_buffer_offset = 0;
		g->generation = INVALID_ID_U16;

		if (!renderer_geometry_upload(g)) {
			KERROR("Failed to upload gizmo geometry type: '%u'", i);
			return false;
		}
		if (g->generation == INVALID_ID_U16) {
			g->generation = 0;
		} else {
			g->generation++;
		}
	}

#if KOHI_DEBUG
	debug_line3d_create(vec3_zero(), vec3_one(), KTRANSFORM_INVALID, &gizmo->plane_normal_line);
	debug_line3d_initialize(&gizmo->plane_normal_line);
	debug_line3d_load(&gizmo->plane_normal_line);
	// magenta
	debug_line3d_colour_set(&gizmo->plane_normal_line, (vec4){1.0f, 0, 1.0f, 1.0f});
#endif
	return true;
}

b8 editor_gizmo_unload (editor_gizmo *gizmo) {
	if (gizmo) {
#if KOHI_DEBUG
		debug_line3d_unload(&gizmo->plane_normal_line);
		debug_line3d_destroy(&gizmo->plane_normal_line);
#endif

		u8 mode_count = EDITOR_GIZMO_MODE_MAX + 1;
		for (u8 i = 0; i < mode_count; ++i) {
			renderer_geometry_destroy(&gizmo->mode_data[i].geo);
		}
	}
	return true;
}

void editor_gizmo_refresh (editor_gizmo *gizmo) {
	if (gizmo) {
		if (gizmo->selected_transform != KTRANSFORM_INVALID) {
			// Set the position.
			vec3 selected_world_position = ktransform_world_position_get(gizmo->selected_transform);
			ktransform_position_set(gizmo->ktransform_handle, selected_world_position);

			// If local, set rotation.
			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL) {
				// Local rotation isn't enough. Even though we are affecting the local pos/rotation/scale, the gizmo needs
				// to be oriented to the _global_ rotation of the object.
				quat selected_world_rotation = ktransform_world_rotation_get(gizmo->selected_transform);
				ktransform_rotation_set(gizmo->ktransform_handle, selected_world_rotation);
			} else {
				// Global is always axis-aligned.
				ktransform_rotation_set(gizmo->ktransform_handle, quat_identity());
			}
			// Ensure the scale is set.
			ktransform_scale_set(gizmo->ktransform_handle, vec3_one());
		} else {
			KINFO("refreshing gizmo with defaults.");
			// For now, reset.
			ktransform_position_set(gizmo->ktransform_handle, vec3_zero());
			ktransform_scale_set(gizmo->ktransform_handle, vec3_one());
			ktransform_rotation_set(gizmo->ktransform_handle, quat_identity());
		}
	}
}

editor_gizmo_orientation editor_gizmo_orientation_get (editor_gizmo *gizmo) {
	if (gizmo) {
		return gizmo->orientation;
	}

	KWARN("editor_gizmo_orientation_get was given no gizmo, returning default of global.");
	return EDITOR_GIZMO_ORIENTATION_GLOBAL;
}

void editor_gizmo_orientation_set (editor_gizmo *gizmo, editor_gizmo_orientation orientation) {
	if (gizmo) {
		gizmo->orientation = orientation;
#if KOHI_DEBUG
		switch (gizmo->orientation) {
		case EDITOR_GIZMO_ORIENTATION_GLOBAL:
			KTRACE("Setting editor gizmo to GLOBAL.");
			break;
		case EDITOR_GIZMO_ORIENTATION_LOCAL:
			KTRACE("Setting editor gizmo to LOCAL.");
			break;
		}
#endif
		editor_gizmo_refresh(gizmo);
	}
}
void editor_gizmo_selected_transform_set (editor_gizmo *gizmo, ktransform transform) {
	if (gizmo) {
		gizmo->selected_transform = transform;
		editor_gizmo_refresh(gizmo);
	}
}

static mat4 gizmo_generate_render_model (vec3 pos, quat rot, f32 world_scale, b8 local_mode) {

	// Determine rotation: local = object local, global = identity
	quat q = local_mode ? rot : quat_identity();
	q = quat_normalize(q);

	// Build rotation matrix in +Y up, -Z forward convention
	float x = q.x, y = q.y, z = q.z, w = q.w;

	mat4 rotation = {0};

	// Right (X)
	rotation.data[0] = 1 - 2 * y * y - 2 * z * z;
	rotation.data[1] = 2 * x * y + 2 * w * z;
	rotation.data[2] = 2 * x * z - 2 * w * y;
	rotation.data[3] = 0.0f;

	// Up (Y)
	rotation.data[4] = 2 * x * y - 2 * w * z;
	rotation.data[5] = 1 - 2 * x * x - 2 * z * z;
	rotation.data[6] = 2 * y * z + 2 * w * x;
	rotation.data[7] = 0.0f;

	// Forward (-Z)
	rotation.data[8] = -(2 * x * z + 2 * w * y);
	rotation.data[9] = -(2 * y * z - 2 * w * x);
	rotation.data[10] = -(1 - 2 * x * x - 2 * y * y);
	rotation.data[11] = 0.0f;

	// Translation identity
	rotation.data[12] = 0.0f;
	rotation.data[13] = 0.0f;
	rotation.data[14] = 0.0f;
	rotation.data[15] = 1.0f;

	// Build uniform scale matrix
	mat4 scale = mat4_scale(vec3_from_scalar(world_scale));

	// 4️⃣ Build translation matrix
	mat4 translation = mat4_translation(pos);

	// Compose TRS: S * R * T
	mat4 SR = mat4_mul(scale, rotation);
	mat4 SR_T = mat4_mul(SR, translation);

	return SR_T;
}

void editor_gizmo_update (editor_gizmo *gizmo, kcamera camera) {
	if (gizmo) {
		ktransform_calculate_local(gizmo->ktransform_handle);

		vec3 gizmo_pos = ktransform_position_get(gizmo->ktransform_handle);
		quat gizmo_rot = gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL ? ktransform_world_rotation_get(gizmo->selected_transform) : quat_identity();
		gizmo_rot = quat_normalize(gizmo_rot);

		vec3 cam_pos = kcamera_get_position(camera);
		f32 dist = vec3_distance(cam_pos, gizmo_pos);
		rect_2di vp_rect = kcamera_get_vp_rect(camera);

		mat4 proj =
			(gizmo->mode == EDITOR_GIZMO_MODE_ROTATE)
				// Setting the far clip to just beyond the gizmo distance from the camera "hides" the
				// backward parts of the loops in the rotation mode, making it far less confusing to look at.
				? kcamera_get_projection_far_clipped(camera, dist + 0.2f)
				// Otherwise just use the projection as normal.
				: kcamera_get_projection(camera);

		/* proj = kcamera_get_projection(camera); */

		kmatrix_system_update_by_id(engine_systems_get()->matrix_system, gizmo->render_projection_id, proj);

		// Calculate the gizmo's world/model matrix
		f32 proj_scale = proj.data[5];
		f32 desired_pixels = 200;
		gizmo->world_scale = (dist * desired_pixels) / (proj_scale * vp_rect.height);

		vec3 gizmo_scale = vec3_from_scalar(gizmo->world_scale);

		mat4 model = mat4_from_translation_rotation_scale(gizmo_pos, gizmo_rot, gizmo_scale);
		kmatrix_system_update_by_id(engine_systems_get()->matrix_system, gizmo->render_model_id, model);
	}
}

void editor_gizmo_render_frame_prepare (editor_gizmo *gizmo, const struct frame_data *p_frame_data) {
	if (gizmo && gizmo->is_dirty) {
		editor_gizmo_mode_data *data = &gizmo->mode_data[gizmo->mode];
		renderer_geometry_vertex_update(&data->geo, 0, data->vertex_count, data->vertices, false);
		gizmo->is_dirty = false;
	}
}

void editor_gizmo_mode_set (editor_gizmo *gizmo, editor_gizmo_mode mode) {
	if (gizmo) {
		gizmo->mode = mode;
		gizmo->is_dirty = true;
#if KOHI_DEBUG
		switch (gizmo->mode) {
		case EDITOR_GIZMO_MODE_NONE:
			KTRACE("Gizmo mode set to 'none'");
			break;
		case EDITOR_GIZMO_MODE_MOVE:
			KTRACE("Gizmo mode set to 'move'");
			break;
		case EDITOR_GIZMO_MODE_ROTATE:
			KTRACE("Gizmo mode set to 'rotate'");
			break;
		case EDITOR_GIZMO_MODE_SCALE:
			KTRACE("Gizmo mode set to 'scale'");
			break;
		}
#endif
	}
}

static void create_gizmo_mode_none (editor_gizmo *gizmo) {
	editor_gizmo_mode_data *data = &gizmo->mode_data[EDITOR_GIZMO_MODE_NONE];

	vec4 grey = (vec4){0.5f, 0.5f, 0.5f, 1.0f};

	u32 axis_vert_count = 0;
	u32 axis_index_count = 0;
	f32 base_offset = 0;
	generate_axis_geometry(AXIS_X, base_offset, axis_length, grey, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, false, &axis_vert_count, &axis_index_count, KNULL, KNULL, 0);

	data->vertex_count = axis_vert_count * 3;
	data->vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, data->vertex_count);
	data->index_count = axis_index_count * 3;
	data->indices = KALLOC_TYPE_CARRAY(u32, data->index_count);

	colour_vertex_3d *verts = data->vertices;
	u32 *inds = data->indices;
	generate_axis_geometry(AXIS_X, base_offset, axis_length, grey, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, false, KNULL, KNULL, verts, inds, (axis_vert_count * 0));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_Y, base_offset, axis_length, grey, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, false, KNULL, KNULL, verts, inds, (axis_vert_count * 1));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_Z, base_offset, axis_length, grey, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, false, KNULL, KNULL, verts, inds, (axis_vert_count * 2));
}

static void create_gizmo_mode_move (editor_gizmo *gizmo) {
	editor_gizmo_mode_data *data = &gizmo->mode_data[EDITOR_GIZMO_MODE_MOVE];

	data->current_axis_index = INVALID_ID_U8;
	colour4 r = {1, 0, 0, 1};
	colour4 g = {0, 1, 0, 1};
	colour4 b = {0, 0, 1, 1};
	f32 base_offset = 0.2f;

	// Get vertex/index counts per axis.
	// Base axis
	u32 axis_vert_count = 0;
	u32 axis_index_count = 0;

	generate_axis_geometry(AXIS_X, base_offset, axis_length, r, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, true, &axis_vert_count, &axis_index_count, KNULL, KNULL, 0);
	// Box along shared axes
	u32 box_vert_count = 0;
	u32 box_index_count = 0;
	generate_axis_geometry(AXIS_XY, box_axis_length, box_axis_length, r, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, false, &box_vert_count, &box_index_count, KNULL, KNULL, 0);

	// One main length and two short lengths for the center "box" per axis
	u32 total_axis_vert_count = (axis_vert_count + (box_vert_count * 2));
	u32 total_axis_index_count = (axis_index_count + (box_index_count * 2));

	data->vertex_count = total_axis_vert_count * 3;
	data->vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, data->vertex_count);
	data->index_count = total_axis_index_count * 3;
	data->indices = KALLOC_TYPE_CARRAY(u32, data->index_count);

	u32 v_offset = 0;
	u32 *inds = data->indices;

	// X
	generate_axis_geometry(AXIS_X, base_offset, axis_length, r, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, true, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_XY, box_axis_length, box_axis_length, r, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += box_vert_count;
	inds += box_index_count;
	generate_axis_geometry(AXIS_XZ, box_axis_length, box_axis_length, r, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += box_vert_count;
	inds += box_index_count;

	// Y
	KASSERT(v_offset == total_axis_vert_count);
	generate_axis_geometry(AXIS_Y, base_offset, axis_length, g, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, true, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_YX, box_axis_length, box_axis_length, g, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += box_vert_count;
	inds += box_index_count;
	generate_axis_geometry(AXIS_YZ, box_axis_length, box_axis_length, g, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += box_vert_count;
	inds += box_index_count;

	// Z
	KASSERT(v_offset == total_axis_vert_count * 2);
	generate_axis_geometry(AXIS_Z, base_offset, axis_length, b, axis_thickness, arrowhead_size, arrowhead_length, axis_sides, true, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_ZX, box_axis_length, box_axis_length, b, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);
	v_offset += box_vert_count;
	inds += box_index_count;
	generate_axis_geometry(AXIS_ZY, box_axis_length, box_axis_length, b, axis_thickness, axis_thickness, arrowhead_length, axis_sides, false, KNULL, KNULL, data->vertices + v_offset, inds, v_offset);

	data->extents_count = 7;
	data->mode_extents = kallocate(sizeof(extents_3d) * data->extents_count, MEMORY_TAG_ARRAY);

	// Create boxes for each axis
	// x
	extents_3d *ex = &data->mode_extents[EDITOR_GIZMO_AXIS_X];
	ex->min = vec3_create(0.4f, -0.2f, -0.2f);
	ex->max = vec3_create(2.1f, 0.2f, 0.2f);

	// y
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_Y];
	ex->min = vec3_create(-0.2f, 0.4f, -0.2f);
	ex->max = vec3_create(0.2f, 2.1f, 0.2f);

	// z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_Z];
	ex->min = vec3_create(-0.2f, -0.2f, 0.4f);
	ex->max = vec3_create(0.2f, 0.2f, 2.1f);

	// Boxes for combo axes.
	// x-y
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XY];
	ex->min = vec3_create(0.1f, 0.1f, -0.05f);
	ex->max = vec3_create(0.5f, 0.5f, 0.05f);

	// x-z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XZ];
	ex->min = vec3_create(0.1f, -0.05f, 0.1f);
	ex->max = vec3_create(0.5f, 0.05f, 0.5f);

	// y-z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_YZ];
	ex->min = vec3_create(-0.05f, 0.1f, 0.1f);
	ex->max = vec3_create(0.05f, 0.5f, 0.5f);

	// xyz
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XYZ];
	ex->min = vec3_create(-0.1f, -0.1f, -0.1f);
	ex->max = vec3_create(0.1f, 0.1f, 0.1f);
}

static void create_gizmo_mode_scale (editor_gizmo *gizmo) {
	editor_gizmo_mode_data *data = &gizmo->mode_data[EDITOR_GIZMO_MODE_SCALE];

	data->current_axis_index = INVALID_ID_U8;

	u32 axis_vert_count = 0;
	u32 axis_index_count = 0;
	colour4 r = {1, 0, 0, 1};
	colour4 g = {0, 1, 0, 1};
	colour4 b = {0, 0, 1, 1};
	f32 base_offset = 0.2f;
	generate_axis_geometry(AXIS_X, base_offset, axis_length, r, axis_thickness, arrowhead_length, arrowhead_size, axis_sides, true, &axis_vert_count, &axis_index_count, KNULL, KNULL, 0);

	data->vertex_count = axis_vert_count * 3;
	data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);
	data->index_count = axis_index_count * 3;
	data->indices = kallocate(sizeof(u32) * data->index_count, MEMORY_TAG_ARRAY);

	colour_vertex_3d *verts = data->vertices;
	u32 *inds = data->indices;
	generate_axis_geometry(AXIS_X, base_offset, axis_length, r, axis_thickness, arrowhead_length, arrowhead_size, axis_sides, true, KNULL, KNULL, verts, inds, (axis_vert_count * 0));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_Y, base_offset, axis_length, g, axis_thickness, arrowhead_length, arrowhead_size, axis_sides, true, KNULL, KNULL, verts, inds, (axis_vert_count * 1));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_geometry(AXIS_Z, base_offset, axis_length, b, axis_thickness, arrowhead_length, arrowhead_size, axis_sides, true, KNULL, KNULL, verts, inds, (axis_vert_count * 2));

	data->extents_count = 7;
	data->mode_extents = kallocate(sizeof(extents_3d) * data->extents_count, MEMORY_TAG_ARRAY);

	// Create boxes for each axis
	// x
	extents_3d *ex = &data->mode_extents[EDITOR_GIZMO_AXIS_X];
	ex->min = vec3_create(0.4f, -0.2f, -0.2f);
	ex->max = vec3_create(2.1f, 0.2f, 0.2f);

	// y
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_Y];
	ex->min = vec3_create(-0.2f, 0.4f, -0.2f);
	ex->max = vec3_create(0.2f, 2.1f, 0.2f);

	// z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_Z];
	ex->min = vec3_create(-0.2f, -0.2f, 0.4f);
	ex->max = vec3_create(0.2f, 0.2f, 2.1f);

	// Boxes for combo axes.
	// x-y
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XY];
	ex->min = vec3_create(0.1f, 0.1f, -0.05f);
	ex->max = vec3_create(0.5f, 0.5f, 0.05f);

	// x-z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XZ];
	ex->min = vec3_create(0.1f, -0.05f, 0.1f);
	ex->max = vec3_create(0.5f, 0.05f, 0.5f);

	// y-z
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_YZ];
	ex->min = vec3_create(-0.05f, 0.1f, 0.1f);
	ex->max = vec3_create(0.05f, 0.5f, 0.5f);

	// xyz
	ex = &data->mode_extents[EDITOR_GIZMO_AXIS_XYZ];
	ex->min = vec3_create(-0.1f, -0.1f, -0.1f);
	ex->max = vec3_create(0.1f, 0.1f, 0.1f);
}

static void create_gizmo_mode_rotate (editor_gizmo *gizmo) {
	editor_gizmo_mode_data *data = &gizmo->mode_data[EDITOR_GIZMO_MODE_ROTATE];

	u32 axis_vert_count = 0;
	u32 axis_index_count = 0;
	colour4 xcol = {1, 0, 0, 1};
	colour4 ycol = {0, 1, 0, 1};
	colour4 zcol = {0, 0, 1, 1};
	generate_axis_ring_geometry(AXIS_X, 1.0f, 0.1f, xcol, segments, 6, &axis_vert_count, &axis_index_count, KNULL, KNULL, 0);

	data->vertex_count = axis_vert_count * 3;
	data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);
	data->index_count = axis_index_count * 3;
	data->indices = kallocate(sizeof(u32) * data->index_count, MEMORY_TAG_ARRAY);

	colour_vertex_3d *verts = data->vertices;
	u32 *inds = data->indices;
	generate_axis_ring_geometry(AXIS_X, 1.0f, 0.1f, xcol, segments, 6, KNULL, KNULL, verts, inds, (axis_vert_count * 0));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_ring_geometry(AXIS_Y, 1.0f, 0.1f, ycol, segments, 6, KNULL, KNULL, verts, inds, (axis_vert_count * 1));
	verts += axis_vert_count;
	inds += axis_index_count;
	generate_axis_ring_geometry(AXIS_Z, 1.0f, 0.1f, zcol, segments, 6, KNULL, KNULL, verts, inds, (axis_vert_count * 2));

	// NOTE: Rotation gizmo uses discs, not extents, so this mode doesn't need them.
}

static void handle_highlighting (editor_gizmo *gizmo, editor_gizmo_mode_data *data, u8 hit_axis) {
	if (data->current_axis_index != hit_axis) {
		data->current_axis_index = hit_axis;

		u32 axis_vert_count = (data->vertex_count / 3);

		b8 hits[3] = {false, false, false};
		switch (hit_axis) {
		case EDITOR_GIZMO_AXIS_X:
			hits[AXIS_X] = true;
			break;
		case EDITOR_GIZMO_AXIS_XY:
			hits[AXIS_X] = true;
			hits[AXIS_Y] = true;
			break;
		case EDITOR_GIZMO_AXIS_Y:
			hits[AXIS_Y] = true;
			break;
		case EDITOR_GIZMO_AXIS_XZ:
			hits[AXIS_X] = true;
			hits[AXIS_Z] = true;
			break;
		case EDITOR_GIZMO_AXIS_Z:
			hits[AXIS_Z] = true;
			break;
		case EDITOR_GIZMO_AXIS_YZ:
			hits[AXIS_Y] = true;
			hits[AXIS_Z] = true;
			break;
		case EDITOR_GIZMO_AXIS_XYZ:
			hits[AXIS_X] = true;
			hits[AXIS_Y] = true;
			hits[AXIS_Z] = true;
			break;
		}

		// Main axis colours
		for (u32 i = 0; i < 3; ++i) {
			vec4 set_colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
			// Yellow for hit axis; otherwise original colour.
			if (hits[i]) {
				set_colour.r = 1.0f;
				set_colour.g = 1.0f;
			} else {
				set_colour.elements[i] = 1.0f;
			}

			u32 offset = axis_vert_count * i;

			for (u32 v = offset; v < offset + axis_vert_count; ++v) {
				data->vertices[v].colour = set_colour;
			}
		}
		gizmo->is_dirty = true;
	}
}

void editor_gizmo_interaction_begin (editor_gizmo *gizmo, kcamera c, struct ray *r, editor_gizmo_interaction_type interaction_type) {
	if (!gizmo || !r) {
		return;
	}

	gizmo->interaction = interaction_type;

	if (gizmo->interaction == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
		editor_gizmo_mode_data *data = &gizmo->mode_data[gizmo->mode];
		/* mat4 gizmo_local = ktransform_local_get(gizmo->ktransform_handle); */
		vec3 origin = ktransform_position_get(gizmo->ktransform_handle);

		// Take a copy of the current transform.
		gizmo->initial_position = origin;
		gizmo->initial_scale = ktransform_scale_get(gizmo->selected_transform);
		gizmo->initial_rotation = ktransform_rotation_get(gizmo->selected_transform);

		quat child_local_rotation = ktransform_rotation_get(gizmo->selected_transform);
		ktransform parent = ktransform_parent_get(gizmo->selected_transform);
		b8 has_parent = (parent != KTRANSFORM_INVALID);
		quat parent_world_rotation =
			has_parent
				? ktransform_world_rotation_get(parent)
				: quat_identity();

		quat world_rotation = child_local_rotation;
		if (has_parent) {
			world_rotation = quat_mul(parent_world_rotation, world_rotation);
		}
		quat inv_world_rotation = quat_inverse(world_rotation);

		// Interaction plane normal.
		vec3 plane_normal;
		if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE || gizmo->mode == EDITOR_GIZMO_MODE_SCALE) {
			// Create the interaction plane.

			switch (data->current_axis_index) {
			case EDITOR_GIZMO_AXIS_X:
				plane_normal = vec3_forward();
				break;
			case EDITOR_GIZMO_AXIS_XY:
				plane_normal = vec3_backward();
				break;
			case EDITOR_GIZMO_AXIS_Y:
				plane_normal = kcamera_backward(c);
				break;
			case EDITOR_GIZMO_AXIS_XYZ:
				plane_normal = kcamera_backward(c);
				break;
			case EDITOR_GIZMO_AXIS_Z:
				plane_normal = vec3_up();
				break;
			case EDITOR_GIZMO_AXIS_XZ:
				plane_normal = vec3_up();
				break;
			case EDITOR_GIZMO_AXIS_YZ:
				plane_normal = vec3_right();
				break;
			default:
				return;
			}
			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL) {
				if (data->current_axis_index != EDITOR_GIZMO_AXIS_Y && data->current_axis_index != EDITOR_GIZMO_AXIS_XYZ) {
					plane_normal = vec3_rotate(plane_normal, world_rotation);
				}
			}

			data->interaction_plane = plane_3d_create(origin, plane_normal);
			data->interaction_plane_back = plane_3d_create(origin, vec3_mul_scalar(plane_normal, -1.0f));

#if KOHI_DEBUG
			debug_line3d_points_set(&gizmo->plane_normal_line, origin, vec3_add(origin, plane_normal));
#endif

			// Get the initial intersection point of the ray on the plane.
			vec3 intersection = {0};
			f32 distance;
			if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
				// Try from the other direction.
				if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
					return;
				}
			}
			data->interaction_start_pos = intersection;
			data->last_interaction_pos = intersection;
		} else if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
			// NOTE: No interaction needed because no current axis.
			if (data->current_axis_index == INVALID_ID_U8) {
				return;
			}
			/* KINFO("starting rotate interaction"); */

			quat rr = gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL ? inv_world_rotation : quat_identity();

			// Create the interaction plane.
			switch (data->current_axis_index) {
			case EDITOR_GIZMO_AXIS_X:
				plane_normal = vec3_rotate(vec3_left(), rr);
				break;
			case EDITOR_GIZMO_AXIS_Y:
				plane_normal = vec3_rotate(vec3_up(), rr);
				break;
			case EDITOR_GIZMO_AXIS_Z:
				plane_normal = vec3_rotate(vec3_forward(), rr);
				break;
			}

			data->interaction_plane = plane_3d_create(origin, plane_normal);
			data->interaction_plane_back = plane_3d_create(origin, vec3_mul_scalar(plane_normal, -1.0f));

#if KOHI_DEBUG
			debug_line3d_points_set(&gizmo->plane_normal_line, origin, vec3_add(origin, plane_normal));
#endif

			// Get the initial intersection point of the ray on the plane.
			vec3 intersection = {0};
			f32 distance;
			if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
				// Try from the other direction.
				if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
					return;
				}
			}
			data->interaction_start_pos = intersection;
			data->last_interaction_pos = intersection;
		}
	}
}

void editor_gizmo_interaction_end (editor_gizmo *gizmo) {
	if (!gizmo) {
		return;
	}

	if (gizmo->interaction == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
		if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
			KINFO("Ending rotate interaction.");
			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
				// Reset rotation. Will be applied to selection already.
				ktransform_rotation_set(gizmo->ktransform_handle, quat_identity());
			}
		}
	}

	gizmo->interaction = EDITOR_GIZMO_INTERACTION_TYPE_NONE;
}

void editor_gizmo_handle_interaction (editor_gizmo *gizmo, kcamera camera, struct ray *r, editor_gizmo_interaction_type interaction_type) {
	if (!gizmo || !r) {
		return;
	}

	editor_gizmo_mode_data *data = &gizmo->mode_data[gizmo->mode];
	mat4 gizmo_local = ktransform_local_get(gizmo->ktransform_handle);
	vec3 origin = ktransform_position_get(gizmo->ktransform_handle);

	quat child_local_rotation = ktransform_rotation_get(gizmo->selected_transform);
	ktransform parent = ktransform_parent_get(gizmo->selected_transform);
	b8 has_parent = (parent != KTRANSFORM_INVALID);
	quat parent_world_rotation =
		has_parent
			? ktransform_world_rotation_get(parent)
			: quat_identity();

	quat world_rotation = child_local_rotation;
	if (has_parent) {
		world_rotation = quat_mul(parent_world_rotation, world_rotation);
	}
	quat inv_world_rotation = quat_inverse(world_rotation);

	f32 distance;
	vec3 intersection = {0};

	if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE) {
		if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
			// NOTE: Don't handle interaction if there's no current axis.
			if (data->current_axis_index == INVALID_ID_U8) {
				return;
			}

			if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
				// Try from the other direction.
				if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
					KTRACE("drag no hit");
					return;
				}
			}
			vec3 diff = vec3_sub(intersection, data->last_interaction_pos);

			vec3 gizmo_translation = vec3_zero();
			vec3 selected_translation = vec3_zero();
			vec3 direction = vec3_up();
			b8 direct = false;

			switch (data->current_axis_index) {
			case EDITOR_GIZMO_AXIS_X:
				direction = vec3_right();
				break;
			case EDITOR_GIZMO_AXIS_Y:
				direction = vec3_up();
				break;
			case EDITOR_GIZMO_AXIS_Z:
				direction = vec3_forward();
				break;
			case EDITOR_GIZMO_AXIS_XY:
			case EDITOR_GIZMO_AXIS_XZ:
			case EDITOR_GIZMO_AXIS_YZ:
			case EDITOR_GIZMO_AXIS_XYZ:
				gizmo_translation = diff;
				direct = true;
				break;
			default:
				return;
			}

			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL && data->current_axis_index < 3) {
				direction = vec3_normalized(vec3_rotate(direction, world_rotation));
			}

			// Apply translation to selection and gizmo.
			// FIXME: There seems to be a small discrepancy between the movement speed of the gizmo
			// and that of the selected object being moved. Realtively minor, but needs to be investigated.
			if (!direct) {
				if (has_parent) {
					// Project diff onto direction.
					gizmo_translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
					vec3 world_direction = vec3_rotate(direction, quat_inverse(parent_world_rotation));
					vec3_normalize(&world_direction);

					f32 d = ksign(vec3_dot(world_direction, direction));
					selected_translation = vec3_mul_scalar(world_direction, vec3_dot(diff, world_direction) * d);
				} else {
					gizmo_translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
					selected_translation = gizmo_translation;
				}
			} else {
				if (has_parent) {
					selected_translation = vec3_rotate(gizmo_translation, quat_inverse(parent_world_rotation));
				} else {
					selected_translation = gizmo_translation;
				}
				KTRACE("direct translation2=%V3.3", &selected_translation);
			}

			data->last_interaction_pos = intersection;

			ktransform_translate(gizmo->ktransform_handle, gizmo_translation);
			ktransform_translate(gizmo->selected_transform, selected_translation);
		} else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
			ktransform_calculate_local(gizmo->ktransform_handle);
			u8 hit_axis = INVALID_ID_U8;

			mat4 inv = mat4_inverse(kmatrix_system_get_by_id(engine_systems_get()->matrix_system, gizmo->render_model_id));

			ray transformed_ray = {
				.origin = vec3_transform(r->origin, 1.0f, inv),
				.direction = vec3_transform(r->direction, 0.0f, inv),
				.max_distance = r->max_distance,
				.flags = r->flags};

			// Loop through each axis/axis combo. Loop backwards to give priority to combos since
			// those hit boxes are much smaller.
			for (i32 i = 6; i > -1; --i) {
				f32 min, max;
				if (ray_intersects_aabb(data->mode_extents[i], transformed_ray.origin, transformed_ray.direction, transformed_ray.max_distance, &min, &max)) {
					hit_axis = i;
					break;
				}
			}

			handle_highlighting(gizmo, data, hit_axis);
		}
	} else if (gizmo->mode == EDITOR_GIZMO_MODE_SCALE) {
		if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
			// NOTE: Don't handle interaction if there's no current axis.
			if (data->current_axis_index == INVALID_ID_U8) {
				return;
			}

			if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
				// Try from the other direction.
				if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
					return;
				}
			}
			vec3 direction;

			// Scale along the current axis' line in local space.
			// This will be transformed to global later if need be.
			switch (data->current_axis_index) {
			case EDITOR_GIZMO_AXIS_X:
				direction = vec3_right();
				break;
			case EDITOR_GIZMO_AXIS_Y:
				direction = vec3_up();
				break;
			case EDITOR_GIZMO_AXIS_Z:
				direction = vec3_forward();
				break;
			case EDITOR_GIZMO_AXIS_XY:
				// Combine the 2 axes, scale along both.
				direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_right(), vec3_up()), 0.5f));
				break;
			case EDITOR_GIZMO_AXIS_XZ:
				// Combine the 2 axes, scale along both.
				direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_right(), vec3_backward()), 0.5f));
				break;
			case EDITOR_GIZMO_AXIS_YZ:
				// Combine the 2 axes, scale along both.
				direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_backward(), vec3_up()), 0.5f));
				break;
			case EDITOR_GIZMO_AXIS_XYZ:
				direction = vec3_normalized(vec3_one());
				break;
			default:
				return;
			}
			// The distance from the origin ultimately determines scale magnitude.
			f32 dist = vec3_distance(origin, intersection);

			// Get the direction of the intersection from the origin.
			vec3 dir_from_origin = vec3_normalized(vec3_sub(intersection, origin));

			// Get the transformed direction.
			vec3 direction_t;
			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL) {
				if (data->current_axis_index < EDITOR_GIZMO_AXIS_XYZ) {
					direction_t = vec3_transform(direction, 0.0f, gizmo_local);
				} else {
					// NOTE: In the case of uniform scale, base on the local up vector.
					direction_t = vec3_transform(vec3_up(), 0.0f, gizmo_local);
				}
			} else if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
				// Use the direction as-is.
				direction_t = direction;
			} else {
				// TODO: Other orientations.
				KASSERT_MSG(false, "Other gizmo orientations not supported.");
				return;
			}

			// Determine the sign of the magnitude by taking the dot
			// product between the direction toward the intersection from the
			// origin, then taking its sign.
			f32 d = ksign(vec3_dot(direction_t, dir_from_origin));

			// Calculate the scale difference by taking the
			// signed magnitude and scaling the untransformed directon by it.
			vec3 mod_scale = vec3_mul_scalar(direction, d * dist);

			// For global transforms, get the inverse of the rotation and apply that
			// to the scale to scale on absolute (global) axes instead of local.
			if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
				if (gizmo->selected_transform != KTRANSFORM_INVALID) {
					quat q = quat_inverse(ktransform_rotation_get(gizmo->selected_transform));
					mod_scale = vec3_rotate(mod_scale, q);
				}
			}

			/* KTRACE("scale (diff): [%.4f,%.4f,%.4f]", mod_scale.x, mod_scale.y, mod_scale.z); */
			// Apply scale to selected object.
			if (gizmo->selected_transform != KTRANSFORM_INVALID) {
				vec3 current_scale = gizmo->initial_scale;

				// Apply scale, but only on axes that have changed.
				for (u8 i = 0; i < 3; ++i) {
					if (mod_scale.elements[i] != 0.0f) {
						current_scale.elements[i] += mod_scale.elements[i];
					}
				}
				/* KTRACE("Applying scale: [%.4f,%.4f,%.4f]", current_scale.x, current_scale.y, current_scale.z); */
				ktransform_scale_set(gizmo->selected_transform, current_scale);
			}
			data->last_interaction_pos = intersection;
		} else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
			ktransform_calculate_local(gizmo->ktransform_handle);
			u8 hit_axis = INVALID_ID_U8;

			mat4 inv = mat4_inverse(kmatrix_system_get_by_id(engine_systems_get()->matrix_system, gizmo->render_model_id));

			ray transformed_ray = {
				.origin = vec3_transform(r->origin, 1.0f, inv),
				.direction = vec3_transform(r->direction, 0.0f, inv),
				.max_distance = r->max_distance,
				.flags = r->flags};

			// Loop through each axis/axis combo. Loop backwards to give priority to combos since
			// those hit boxes are much smaller.
			for (i32 i = EDITOR_GIZMO_AXIS_XYZ; i > -1; --i) {
				f32 min, max;
				if (ray_intersects_aabb(data->mode_extents[i], transformed_ray.origin, transformed_ray.direction, transformed_ray.max_distance, &min, &max)) {
					hit_axis = i;
					break;
				}
			}

			handle_highlighting(gizmo, data, hit_axis);
		}
	} else if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
		if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
			// NOTE: No interaction needed if no current axis.
			if (data->current_axis_index == INVALID_ID_U8) {
				return;
			}

			if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
				// Try from the other direction.
				if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
					return;
				}
			}
			vec3 direction;

			// Get the difference in angle between this interaction and the last and use that as the
			// axis angle for rotation.
			vec3 v_0 = vec3_sub(data->last_interaction_pos, origin);
			vec3 v_1 = vec3_sub(intersection, origin);
			f32 angle = kacos(vec3_dot(vec3_normalized(v_0), vec3_normalized(v_1)));
			// No angle means no change, so boot out.
			// NOTE: Also check for NaN, which can be done because floats have a unique property
			// that (x != x) detects NaN.
			if (angle == 0 || angle != angle) {
				return;
			}
			vec3 cross = vec3_cross(v_0, v_1);
			if (vec3_dot(data->interaction_plane.normal, cross) < 0) {
				angle = -angle;
			}

			quat rr = gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL ? inv_world_rotation : quat_identity();

			// Create the interaction plane.
			switch (data->current_axis_index) {
			case EDITOR_GIZMO_AXIS_X:
				direction = vec3_rotate(vec3_left(), rr);
				break;
			case EDITOR_GIZMO_AXIS_Y:
				direction = vec3_rotate(vec3_up(), rr);
				break;
			case EDITOR_GIZMO_AXIS_Z:
				direction = vec3_rotate(vec3_forward(), rr);
				break;
			}

			quat rotation = quat_from_axis_angle(direction, angle, true);
			// Apply rotation to gizmo here so it's visible.
			ktransform_rotate(gizmo->ktransform_handle, rotation);
			data->last_interaction_pos = intersection;

			// Apply rotation.
			if (gizmo->selected_transform != KTRANSFORM_INVALID) {
				ktransform_rotate(gizmo->selected_transform, (rotation));
			}

		} else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
			f32 dist;
			vec3 point;
			u8 hit_axis = INVALID_ID_U8;

			mat4 transform = kmatrix_system_get_by_id(engine_systems_get()->matrix_system, gizmo->render_model_id);
			vec3 center = mat4_position(transform);
			quat q = ktransform_rotation_get(gizmo->selected_transform);
			q = quat_normalize(q);
			f32 scale = gizmo->world_scale;

			// Loop through each axis.
			for (u32 i = 0; i < 3; ++i) {
				// Oriented disc.
				vec3 aa_normal = vec3_zero();
				aa_normal.elements[i] = 1.0f;
				aa_normal = vec3_transform(aa_normal, 0.0f, gizmo_local);
				f32 scaled_rad = radius * scale;
				f32 inner = scaled_rad - (scale * 0.05f);
				f32 outer = scaled_rad + (scale * 0.05f);
				if (raycast_disc_3d(r, center, aa_normal, outer, inner, &point, &dist)) {
					hit_axis = i;
					break;
				} else {
					// If not, try from the other way.
					aa_normal = vec3_mul_scalar(aa_normal, -1.0f);
					if (raycast_disc_3d(r, center, aa_normal, outer, inner, &point, &dist)) {
						hit_axis = i;
						break;
					}
				}
			}

			handle_highlighting(gizmo, data, hit_axis);
		}
	}

	ktransform_calculate_local(gizmo->ktransform_handle);
}

mat4 editor_gizmo_model_get (editor_gizmo *gizmo) {
	if (gizmo) {
		// NOTE: Using the local matrix since the gizmo will never be parented to anything.
		return ktransform_local_get(gizmo->ktransform_handle);
	}
	// Return identity in the case of the gizmo not existing for some reason.
	return mat4_identity();
}
