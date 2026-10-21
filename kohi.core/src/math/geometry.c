#include "geometry.h"

#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "strings/kname.h"

void geometry_generate_normals (u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
		vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

		vec3 normal = vec3_normalized(vec3_cross(edge1, edge2));

		// NOTE: This just generates a face normal. Smoothing out should be done in
		// a separate pass if desired.
		vertices[i0].normal = normal;
		vertices[i1].normal = normal;
		vertices[i2].normal = normal;
	}
}

void geometry_generate_tangents (u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
		vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

		f32 deltaU1 = vertices[i1].texcoord.x - vertices[i0].texcoord.x;
		f32 deltaV1 = vertices[i1].texcoord.y - vertices[i0].texcoord.y;

		f32 deltaU2 = vertices[i2].texcoord.x - vertices[i0].texcoord.x;
		f32 deltaV2 = vertices[i2].texcoord.y - vertices[i0].texcoord.y;

		f32 dividend = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
		f32 fc = 1.0f / dividend;

		vec3 tangent = (vec3){(fc * (deltaV2 * edge1.x - deltaV1 * edge2.x)),
							  (fc * (deltaV2 * edge1.y - deltaV1 * edge2.y)),
							  (fc * (deltaV2 * edge1.z - deltaV1 * edge2.z))};

		tangent = vec3_normalized(tangent);

		f32 sx = deltaU1, sy = deltaU2;
		f32 tx = deltaV1, ty = deltaV2;
		f32 handedness = ((tx * sy - ty * sx) < 0.0f) ? -1.0f : 1.0f;

		vec3 t4 = vec3_mul_scalar(tangent, handedness);
		// Encode handedness into w.
		vertices[i0].tangent = vec4_from_vec3(t4, handedness);
		vertices[i1].tangent = vec4_from_vec3(t4, handedness);
		vertices[i2].tangent = vec4_from_vec3(t4, handedness);
	}
}

b8 vertex3d_equal (vertex_3d vert_0, vertex_3d vert_1) {
	return vec3_compare(vert_0.position, vert_1.position, K_FLOAT_EPSILON) &&
		   vec3_compare(vert_0.normal, vert_1.normal, K_FLOAT_EPSILON) &&
		   vec2_compare(vert_0.texcoord, vert_1.texcoord, K_FLOAT_EPSILON) &&
		   vec4_compare(vert_0.colour, vert_1.colour, K_FLOAT_EPSILON) &&
		   vec4_compare(vert_0.tangent, vert_1.tangent, K_FLOAT_EPSILON);
}

void reassign_index (u32 index_count, u32 *indices, u32 from, u32 to) {
	for (u32 i = 0; i < index_count; ++i) {
		if (indices[i] == from) {
			indices[i] = to;
		} else if (indices[i] > from) {
			// Pull in all indicies higher than 'from' by 1.
			indices[i]--;
		}
	}
}

void geometry_deduplicate_vertices (u32 vertex_count, vertex_3d *vertices,
									u32 index_count, u32 *indices,
									u32 *out_vertex_count,
									vertex_3d **out_vertices) {
	// Create new arrays for the collection to sit in.
	vertex_3d *unique_verts =
		kallocate(sizeof(vertex_3d) * vertex_count, MEMORY_TAG_ARRAY);
	*out_vertex_count = 0;

	u32 found_count = 0;
	for (u32 v = 0; v < vertex_count; ++v) {
		b8 found = false;
		for (u32 u = 0; u < *out_vertex_count; ++u) {
			if (vertex3d_equal(vertices[v], unique_verts[u])) {
				// Reassign indices, do _not_ copy
				reassign_index(index_count, indices, v - found_count, u);
				found = true;
				found_count++;
				break;
			}
		}

		if (!found) {
			// Copy over to unique.
			unique_verts[*out_vertex_count] = vertices[v];
			(*out_vertex_count)++;
		}
	}

	// Allocate new vertices array
	*out_vertices =
		kallocate(sizeof(vertex_3d) * (*out_vertex_count), MEMORY_TAG_ARRAY);
	// Copy over unique
	kcopy_memory(*out_vertices, unique_verts,
				 sizeof(vertex_3d) * (*out_vertex_count));
	// Destroy temp array
	kfree(unique_verts);

	KDEBUG("geometry_deduplicate_vertices: removed %d vertices, orig/now %d/%d.",
		   vertex_count - *out_vertex_count, vertex_count, *out_vertex_count);
}

void generate_uvs_from_image_coords (u32 img_width, u32 img_height, u32 px_x, u32 px_y, f32 *out_tx, f32 *out_ty) {
	KASSERT_DEBUG(out_tx);
	KASSERT_DEBUG(out_ty);
	*out_tx = (f32)px_x / img_width;
	*out_ty = (f32)px_y / img_height;
}

kgeometry geometry_generate_quad (f32 width, f32 height, f32 tx_min, f32 tx_max, f32 ty_min, f32 ty_max, kname name) {
	kgeometry out_geometry = {0};

	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_2D_STATIC;
	out_geometry.generation = INVALID_ID_U16;
	out_geometry.extents.min = (vec3){-width * 0.5f, -height * 0.5f, 0.0f};
	out_geometry.extents.max = (vec3){width * 0.5f, height * 0.5f, 0.0f};
	// Always half width/height since upper left is 0,0 and lower right is width/height
	out_geometry.center = vec3_zero();
	out_geometry.vertex_element_size = sizeof(vertex_2d);
	out_geometry.vertex_count = 4;
	out_geometry.vertices = KALLOC_TYPE_CARRAY(vertex_2d, out_geometry.vertex_count);
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.index_count = 6;
	out_geometry.indices = KALLOC_TYPE_CARRAY(u32, out_geometry.index_count);
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	vertex_2d vertices[4];
	// FIXME: Extents above are based on half width/height, but this is zero based. Pick one, ya dingus!
	vertices[0].position.x = 0.0f;	 // 0    3
	vertices[0].position.y = 0.0f;	 //
	vertices[0].texcoord.x = tx_min; //
	vertices[0].texcoord.y = ty_min; // 2    1

	vertices[1].position.y = height;
	vertices[1].position.x = width;
	vertices[1].texcoord.x = tx_max;
	vertices[1].texcoord.y = ty_max;

	vertices[2].position.x = 0.0f;
	vertices[2].position.y = height;
	vertices[2].texcoord.x = tx_min;
	vertices[2].texcoord.y = ty_max;

	vertices[3].position.x = width;
	vertices[3].position.y = 0.0;
	vertices[3].texcoord.x = tx_max;
	vertices[3].texcoord.y = ty_min;
	KCOPY_TYPE_CARRAY(out_geometry.vertices, vertices, vertex_2d, out_geometry.vertex_count);

	// Indices - counter-clockwise
	u32 indices[6] = {2, 1, 0, 3, 0, 1};
	KCOPY_TYPE_CARRAY(out_geometry.indices, indices, u32, out_geometry.index_count);

	return out_geometry;
}

kgeometry geometry_generate_line2d (vec2 point_0, vec2 point_1, kname name) {
	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_2D_STATIC;
	out_geometry.generation = INVALID_ID_U16;
	out_geometry.center = vec3_from_vec2(vec2_mid(point_0, point_1), 0.0f);
	out_geometry.extents.min = (vec3){
		KMIN(point_0.x, point_1.x),
		KMIN(point_0.y, point_1.y),
		0.0f};
	out_geometry.extents.max = (vec3){
		KMAX(point_0.x, point_1.x),
		KMAX(point_0.y, point_1.y),
		0.0f};
	out_geometry.vertex_count = 2;
	out_geometry.vertex_element_size = sizeof(vertex_2d);
	out_geometry.vertices = KALLOC_TYPE_CARRAY(vertex_2d, out_geometry.vertex_count);
	((vertex_2d *)out_geometry.vertices)[0].position = point_0;
	((vertex_2d *)out_geometry.vertices)[1].position = point_1;
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	// NOTE: lines do not have indices.
	out_geometry.index_count = 0;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.indices = 0;
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	return out_geometry;
}

kgeometry geometry_generate_line3d (vec3 point_0, vec3 point_1, kname name) {
	return geometry_generate_line3d_typed(point_0, point_1, name, KGEOMETRY_TYPE_3D_STATIC_COLOUR);
}

kgeometry geometry_generate_line3d_typed (vec3 point_0, vec3 point_1, kname name, kgeometry_type type) {
	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = type;
	out_geometry.generation = INVALID_ID_U16;
	out_geometry.center = vec3_mid(point_0, point_1);
	out_geometry.extents.min = (vec3){
		KMIN(point_0.x, point_1.x),
		KMIN(point_0.y, point_1.y),
		KMIN(point_0.z, point_1.z)};
	out_geometry.extents.max = (vec3){
		KMAX(point_0.x, point_1.x),
		KMAX(point_0.y, point_1.y),
		KMAX(point_0.z, point_1.z)};
	out_geometry.vertex_count = 2;

	switch (type) {
	default:
		KASSERT("Only types of colour and positon_only are supported.");
		return (kgeometry){0};
	case KGEOMETRY_TYPE_3D_STATIC_COLOUR:
		out_geometry.vertex_element_size = sizeof(colour_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, out_geometry.vertex_count);
		((colour_vertex_3d *)out_geometry.vertices)[0].position = vec4_from_vec3(point_0, 1.0f);
		((colour_vertex_3d *)out_geometry.vertices)[1].position = vec4_from_vec3(point_1, 1.0f);
		break;
	case KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY:
		out_geometry.vertex_element_size = sizeof(position_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(position_vertex_3d, out_geometry.vertex_count);
		((position_vertex_3d *)out_geometry.vertices)[0].position = vec4_from_vec3(point_0, 1.0f);
		((position_vertex_3d *)out_geometry.vertices)[1].position = vec4_from_vec3(point_1, 1.0f);
		break;
	}

	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	// NOTE: lines do not have indices.
	out_geometry.index_count = 0;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.indices = 0;
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	return out_geometry;
}

kgeometry geometry_generate_line_sphere3d (f32 radius, u32 segment_count, kname name) {
	return geometry_generate_line_sphere3d_typed(radius, segment_count, name, KGEOMETRY_TYPE_3D_STATIC_COLOUR);
}

kgeometry geometry_generate_line_sphere3d_typed (f32 radius, u32 segment_count, kname name, kgeometry_type type) {
	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = type;
	out_geometry.generation = INVALID_ID_U16;
	out_geometry.center = vec3_zero();

	out_geometry.extents.min = (vec3){-radius, -radius, -radius};
	out_geometry.extents.max = (vec3){radius, radius, radius};

	// 2 per line, 3 lines + 3 lines
	out_geometry.vertex_count = 12 + (segment_count * 2 * 3);

	switch (type) {
	default:
		KASSERT("Only types of colour and positon_only are supported.");
		return (kgeometry){0};
	case KGEOMETRY_TYPE_3D_STATIC_COLOUR: {
		out_geometry.vertex_element_size = sizeof(colour_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, out_geometry.vertex_count);

		// Start with the center, draw small axes.
		// x
		colour_vertex_3d *verts = out_geometry.vertices;
		verts[1].position.x = 0.2f;
		verts[3].position.y = 0.2f;
		verts[5].position.z = 0.2f;

		// For each axis, generate points in a circle.
		u32 j = 6;

		// x
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.y = radius * kcos(theta);
			verts[j].position.z = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.y = radius * kcos(theta);
			verts[j + 1].position.z = radius * ksin(theta);
		}

		// y
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.x = radius * kcos(theta);
			verts[j].position.z = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.x = radius * kcos(theta);
			verts[j + 1].position.z = radius * ksin(theta);
		}

		// z
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.x = radius * kcos(theta);
			verts[j].position.y = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.x = radius * kcos(theta);
			verts[j + 1].position.y = radius * ksin(theta);
		}

	} break;
	case KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY: {
		out_geometry.vertex_element_size = sizeof(position_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(position_vertex_3d, out_geometry.vertex_count);

		// Start with the center, draw small axes.
		// x
		position_vertex_3d *verts = out_geometry.vertices;
		verts[1].position.x = 0.2f;

		// y
		verts[3].position.y = 0.2f;

		// z
		verts[5].position.z = 0.2f;

		// For each axis, generate points in a circle.
		u32 j = 6;

		// x
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.y = radius * kcos(theta);
			verts[j].position.z = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.y = radius * kcos(theta);
			verts[j + 1].position.z = radius * ksin(theta);
		}

		// y
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.x = radius * kcos(theta);
			verts[j].position.z = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.x = radius * kcos(theta);
			verts[j + 1].position.z = radius * ksin(theta);
		}

		// z
		for (u32 i = 0; i < segment_count; ++i, j += 2) {
			// 2 at a time to form a line.
			f32 theta = (f32)i / segment_count * K_2PI;
			verts[j].position.x = radius * kcos(theta);
			verts[j].position.y = radius * ksin(theta);

			theta = (f32)((i + 1) % segment_count) / segment_count * K_2PI;
			verts[j + 1].position.x = radius * kcos(theta);
			verts[j + 1].position.y = radius * ksin(theta);
		}

	} break;
	}

	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	// NOTE: lines do not have indices.
	out_geometry.index_count = 0;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.indices = 0;
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	return out_geometry;
}

kgeometry geometry_generate_plane (f32 width, f32 height, u32 x_segment_count, u32 y_segment_count, f32 tile_x, f32 tile_y, kname name) {
	if (width == 0) {
		KWARN("Width must be nonzero. Defaulting to one.");
		width = 1.0f;
	}
	if (height == 0) {
		KWARN("Height must be nonzero. Defaulting to one.");
		height = 1.0f;
	}
	if (x_segment_count < 1) {
		KWARN("x_segment_count must be a positive number. Defaulting to one.");
		x_segment_count = 1;
	}
	if (y_segment_count < 1) {
		KWARN("y_segment_count must be a positive number. Defaulting to one.");
		y_segment_count = 1;
	}

	if (tile_x == 0) {
		KWARN("tile_x must be nonzero. Defaulting to one.");
		tile_x = 1.0f;
	}
	if (tile_y == 0) {
		KWARN("tile_y must be nonzero. Defaulting to one.");
		tile_y = 1.0f;
	}

	f32 half_width = width * 0.5f;
	f32 half_height = height * 0.5f;

	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_3D_STATIC;
	out_geometry.generation = INVALID_ID_U16; // NOTE: generation is 0 because this is technically the first "update"
	out_geometry.extents.min = (vec3){-half_width, -half_height, 0.0f};
	out_geometry.extents.max = (vec3){half_width, half_height, 0.0f};
	// Always 0 since min/max of each axis are -/+ half of the size.
	out_geometry.center = vec3_zero();
	out_geometry.vertex_element_size = sizeof(vertex_3d);
	out_geometry.vertex_count = x_segment_count * y_segment_count * 4; // 4 verts per segment
	out_geometry.vertices = KALLOC_TYPE_CARRAY(vertex_3d, out_geometry.vertex_count);
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.index_count = x_segment_count * y_segment_count * 6; // 6 indices per segment
	out_geometry.indices = KALLOC_TYPE_CARRAY(u32, out_geometry.index_count);
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	// TODO: This generates extra vertices, but we can always deduplicate them later.
	f32 seg_width = width / x_segment_count;
	f32 seg_height = height / y_segment_count;
	for (u32 y = 0; y < y_segment_count; ++y) {
		for (u32 x = 0; x < x_segment_count; ++x) {
			// Generate vertices
			f32 min_x = (x * seg_width) - half_width;
			f32 min_y = (y * seg_height) - half_height;
			f32 max_x = min_x + seg_width;
			f32 max_y = min_y + seg_height;
			f32 min_uvx = (x / (f32)x_segment_count) * tile_x;
			f32 min_uvy = (y / (f32)y_segment_count) * tile_y;
			f32 max_uvx = ((x + 1) / (f32)x_segment_count) * tile_x;
			f32 max_uvy = ((y + 1) / (f32)y_segment_count) * tile_y;

			u32 v_offset = ((y * x_segment_count) + x) * 4;
			vertex_3d *v0 = &((vertex_3d *)out_geometry.vertices)[v_offset + 0];
			vertex_3d *v1 = &((vertex_3d *)out_geometry.vertices)[v_offset + 1];
			vertex_3d *v2 = &((vertex_3d *)out_geometry.vertices)[v_offset + 2];
			vertex_3d *v3 = &((vertex_3d *)out_geometry.vertices)[v_offset + 3];

			v0->position.x = min_x;
			v0->position.y = min_y;
			v0->texcoord.x = min_uvx;
			v0->texcoord.y = min_uvy;

			v1->position.x = max_x;
			v1->position.y = max_y;
			v1->texcoord.x = max_uvx;
			v1->texcoord.y = max_uvy;

			v2->position.x = min_x;
			v2->position.y = max_y;
			v2->texcoord.x = min_uvx;
			v2->texcoord.y = max_uvy;

			v3->position.x = max_x;
			v3->position.y = min_y;
			v3->texcoord.x = max_uvx;
			v3->texcoord.y = min_uvy;

			// Generate indices
			u32 i_offset = ((y * x_segment_count) + x) * 6;
			((u32 *)out_geometry.indices)[i_offset + 0] = v_offset + 0;
			((u32 *)out_geometry.indices)[i_offset + 1] = v_offset + 1;
			((u32 *)out_geometry.indices)[i_offset + 2] = v_offset + 2;
			((u32 *)out_geometry.indices)[i_offset + 3] = v_offset + 0;
			((u32 *)out_geometry.indices)[i_offset + 4] = v_offset + 3;
			((u32 *)out_geometry.indices)[i_offset + 5] = v_offset + 1;
		}
	}

	return out_geometry;
}

kgeometry geometry_generate_plane_2d (f32 width, f32 height, u32 x_segment_count, u32 y_segment_count, f32 tile_x, f32 tile_y, kname name, b8 centered) {
	if (width == 0) {
		KWARN("Width must be nonzero. Defaulting to one.");
		width = 1.0f;
	}
	if (height == 0) {
		KWARN("Height must be nonzero. Defaulting to one.");
		height = 1.0f;
	}
	if (x_segment_count < 1) {
		KWARN("x_segment_count must be a positive number. Defaulting to one.");
		x_segment_count = 1;
	}
	if (y_segment_count < 1) {
		KWARN("y_segment_count must be a positive number. Defaulting to one.");
		y_segment_count = 1;
	}

	if (tile_x == 0) {
		KWARN("tile_x must be nonzero. Defaulting to one.");
		tile_x = 1.0f;
	}
	if (tile_y == 0) {
		KWARN("tile_y must be nonzero. Defaulting to one.");
		tile_y = 1.0f;
	}

	f32 half_width = width * 0.5f;
	f32 half_height = height * 0.5f;

	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_2D_STATIC;
	out_geometry.generation = INVALID_ID_U16; // NOTE: generation is 0 because this is technically the first "update"
	out_geometry.extents.min = centered ? (vec3){-half_width, -half_height, 0.0f} : vec3_zero();
	out_geometry.extents.max = centered ? (vec3){half_width, half_height, 0.0f} : (vec3){width, height, 0.0f};
	// Always 0 since min/max of each axis are -/+ half of the size.
	out_geometry.center = vec3_zero();
	out_geometry.vertex_element_size = sizeof(vertex_2d);
	out_geometry.vertex_count = x_segment_count * y_segment_count * 4; // 4 verts per segment
	out_geometry.vertices = KALLOC_TYPE_CARRAY(vertex_2d, out_geometry.vertex_count);
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.index_count = x_segment_count * y_segment_count * 6; // 6 indices per segment
	out_geometry.indices = KALLOC_TYPE_CARRAY(u32, out_geometry.index_count);
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	// TODO: This generates extra vertices, but we can always deduplicate them later.
	f32 seg_width = width / x_segment_count;
	f32 seg_height = height / y_segment_count;
	for (u32 y = 0; y < y_segment_count; ++y) {
		for (u32 x = 0; x < x_segment_count; ++x) {
			// Generate vertices
			f32 min_x = (x * seg_width) - (centered ? half_width : 0);
			f32 min_y = (y * seg_height) - (centered ? half_height : 0);
			f32 max_x = min_x + seg_width;
			f32 max_y = min_y + seg_height;
			f32 min_uvx = (x / (f32)x_segment_count) * tile_x;
			f32 min_uvy = (y / (f32)y_segment_count) * tile_y;
			f32 max_uvx = ((x + 1) / (f32)x_segment_count) * tile_x;
			f32 max_uvy = ((y + 1) / (f32)y_segment_count) * tile_y;

			u32 v_offset = ((y * x_segment_count) + x) * 4;
			vertex_2d *v0 = &((vertex_2d *)out_geometry.vertices)[v_offset + 0];
			vertex_2d *v1 = &((vertex_2d *)out_geometry.vertices)[v_offset + 1];
			vertex_2d *v2 = &((vertex_2d *)out_geometry.vertices)[v_offset + 2];
			vertex_2d *v3 = &((vertex_2d *)out_geometry.vertices)[v_offset + 3];

			v0->position.x = min_x;
			v0->position.y = min_y;
			v0->texcoord.x = min_uvx;
			v0->texcoord.y = min_uvy;

			v1->position.x = max_x;
			v1->position.y = max_y;
			v1->texcoord.x = max_uvx;
			v1->texcoord.y = max_uvy;

			v2->position.x = min_x;
			v2->position.y = max_y;
			v2->texcoord.x = min_uvx;
			v2->texcoord.y = max_uvy;

			v3->position.x = max_x;
			v3->position.y = min_y;
			v3->texcoord.x = max_uvx;
			v3->texcoord.y = min_uvy;

			// Generate indices
			u32 i_offset = ((y * x_segment_count) + x) * 6;
			((u32 *)out_geometry.indices)[i_offset + 0] = v_offset + 0;
			((u32 *)out_geometry.indices)[i_offset + 1] = v_offset + 1;
			((u32 *)out_geometry.indices)[i_offset + 2] = v_offset + 2;
			((u32 *)out_geometry.indices)[i_offset + 3] = v_offset + 0;
			((u32 *)out_geometry.indices)[i_offset + 4] = v_offset + 3;
			((u32 *)out_geometry.indices)[i_offset + 5] = v_offset + 1;
		}
	}

	return out_geometry;
}

void geometry_recalculate_line_box3d_by_points (kgeometry *geometry, vec3 points[8]) {
	if (geometry->type == KGEOMETRY_TYPE_3D_STATIC_COLOUR) {
		// Front lines
		{
			// top
			((colour_vertex_3d *)geometry->vertices)[0].position = vec4_from_vec3(points[2], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[1].position = vec4_from_vec3(points[3], 1.0f);
			// right
			((colour_vertex_3d *)geometry->vertices)[2].position = vec4_from_vec3(points[1], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[3].position = vec4_from_vec3(points[2], 1.0f);
			// bottom
			((colour_vertex_3d *)geometry->vertices)[4].position = vec4_from_vec3(points[0], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[5].position = vec4_from_vec3(points[1], 1.0f);
			// left
			((colour_vertex_3d *)geometry->vertices)[6].position = vec4_from_vec3(points[3], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[7].position = vec4_from_vec3(points[0], 1.0f);
		}
		// back lines
		{
			// top
			((colour_vertex_3d *)geometry->vertices)[8].position = vec4_from_vec3(points[6], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[9].position = vec4_from_vec3(points[7], 1.0f);
			// right
			((colour_vertex_3d *)geometry->vertices)[10].position = vec4_from_vec3(points[5], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[11].position = vec4_from_vec3(points[6], 1.0f);
			// bottom
			((colour_vertex_3d *)geometry->vertices)[12].position = vec4_from_vec3(points[4], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[13].position = vec4_from_vec3(points[5], 1.0f);
			// left
			((colour_vertex_3d *)geometry->vertices)[14].position = vec4_from_vec3(points[7], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[15].position = vec4_from_vec3(points[4], 1.0f);
		}

		// top connecting lines
		{
			// left
			((colour_vertex_3d *)geometry->vertices)[16].position = vec4_from_vec3(points[3], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[17].position = vec4_from_vec3(points[7], 1.0f);
			// right
			((colour_vertex_3d *)geometry->vertices)[18].position = vec4_from_vec3(points[2], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[19].position = vec4_from_vec3(points[6], 1.0f);
		}
		// bottom connecting lines
		{
			// left
			((colour_vertex_3d *)geometry->vertices)[20].position = vec4_from_vec3(points[0], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[21].position = vec4_from_vec3(points[4], 1.0f);
			// right
			((colour_vertex_3d *)geometry->vertices)[22].position = vec4_from_vec3(points[1], 1.0f);
			((colour_vertex_3d *)geometry->vertices)[23].position = vec4_from_vec3(points[5], 1.0f);
		}
	} else if (geometry->type == KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY) {
		// Front lines
		{
			// top
			((position_vertex_3d *)geometry->vertices)[0].position = vec4_from_vec3(points[2], 1.0f);
			((position_vertex_3d *)geometry->vertices)[1].position = vec4_from_vec3(points[3], 1.0f);
			// right
			((position_vertex_3d *)geometry->vertices)[2].position = vec4_from_vec3(points[1], 1.0f);
			((position_vertex_3d *)geometry->vertices)[3].position = vec4_from_vec3(points[2], 1.0f);
			// bottom
			((position_vertex_3d *)geometry->vertices)[4].position = vec4_from_vec3(points[0], 1.0f);
			((position_vertex_3d *)geometry->vertices)[5].position = vec4_from_vec3(points[1], 1.0f);
			// left
			((position_vertex_3d *)geometry->vertices)[6].position = vec4_from_vec3(points[3], 1.0f);
			((position_vertex_3d *)geometry->vertices)[7].position = vec4_from_vec3(points[0], 1.0f);
		}
		// back lines
		{
			// top
			((position_vertex_3d *)geometry->vertices)[8].position = vec4_from_vec3(points[6], 1.0f);
			((position_vertex_3d *)geometry->vertices)[9].position = vec4_from_vec3(points[7], 1.0f);
			// right
			((position_vertex_3d *)geometry->vertices)[10].position = vec4_from_vec3(points[5], 1.0f);
			((position_vertex_3d *)geometry->vertices)[11].position = vec4_from_vec3(points[6], 1.0f);
			// bottom
			((position_vertex_3d *)geometry->vertices)[12].position = vec4_from_vec3(points[4], 1.0f);
			((position_vertex_3d *)geometry->vertices)[13].position = vec4_from_vec3(points[5], 1.0f);
			// left
			((position_vertex_3d *)geometry->vertices)[14].position = vec4_from_vec3(points[7], 1.0f);
			((position_vertex_3d *)geometry->vertices)[15].position = vec4_from_vec3(points[4], 1.0f);
		}

		// top connecting lines
		{
			// left
			((position_vertex_3d *)geometry->vertices)[16].position = vec4_from_vec3(points[3], 1.0f);
			((position_vertex_3d *)geometry->vertices)[17].position = vec4_from_vec3(points[7], 1.0f);
			// right
			((position_vertex_3d *)geometry->vertices)[18].position = vec4_from_vec3(points[2], 1.0f);
			((position_vertex_3d *)geometry->vertices)[19].position = vec4_from_vec3(points[6], 1.0f);
		}
		// bottom connecting lines
		{
			// left
			((position_vertex_3d *)geometry->vertices)[20].position = vec4_from_vec3(points[0], 1.0f);
			((position_vertex_3d *)geometry->vertices)[21].position = vec4_from_vec3(points[4], 1.0f);
			// right
			((position_vertex_3d *)geometry->vertices)[22].position = vec4_from_vec3(points[1], 1.0f);
			((position_vertex_3d *)geometry->vertices)[23].position = vec4_from_vec3(points[5], 1.0f);
		}
	}
}

void geometry_recalculate_line_box3d_by_extents (kgeometry *geometry, extents_3d extents, vec3 offset) {
	if (geometry->type == KGEOMETRY_TYPE_3D_STATIC_COLOUR) {
		// Front lines
		{
			// top
			((colour_vertex_3d *)geometry->vertices)[0].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[1].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			// right
			((colour_vertex_3d *)geometry->vertices)[2].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[3].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			// bottom
			((colour_vertex_3d *)geometry->vertices)[4].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[5].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			// left
			((colour_vertex_3d *)geometry->vertices)[6].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[7].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
		}
		// back lines
		{
			// top
			((colour_vertex_3d *)geometry->vertices)[8].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[9].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((colour_vertex_3d *)geometry->vertices)[10].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[11].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// bottom
			((colour_vertex_3d *)geometry->vertices)[12].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[13].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// left
			((colour_vertex_3d *)geometry->vertices)[14].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[15].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
		}

		// top connecting lines
		{
			// left
			((colour_vertex_3d *)geometry->vertices)[16].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[17].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((colour_vertex_3d *)geometry->vertices)[18].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[19].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
		}
		// bottom connecting lines
		{
			// left
			((colour_vertex_3d *)geometry->vertices)[20].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[21].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((colour_vertex_3d *)geometry->vertices)[22].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((colour_vertex_3d *)geometry->vertices)[23].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
		}
	} else if (geometry->type == KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY) {
		// Front lines
		{
			// top
			((position_vertex_3d *)geometry->vertices)[0].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[1].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			// right
			((position_vertex_3d *)geometry->vertices)[2].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[3].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			// bottom
			((position_vertex_3d *)geometry->vertices)[4].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[5].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			// left
			((position_vertex_3d *)geometry->vertices)[6].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[7].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
		}
		// back lines
		{
			// top
			((position_vertex_3d *)geometry->vertices)[8].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[9].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((position_vertex_3d *)geometry->vertices)[10].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[11].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// bottom
			((position_vertex_3d *)geometry->vertices)[12].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[13].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// left
			((position_vertex_3d *)geometry->vertices)[14].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[15].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
		}

		// top connecting lines
		{
			// left
			((position_vertex_3d *)geometry->vertices)[16].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[17].position = (vec4){extents.min.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((position_vertex_3d *)geometry->vertices)[18].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[19].position = (vec4){extents.max.x + offset.x, extents.min.y + offset.y, extents.max.z + offset.z, 1.0f};
		}
		// bottom connecting lines
		{
			// left
			((position_vertex_3d *)geometry->vertices)[20].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[21].position = (vec4){extents.min.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
			// right
			((position_vertex_3d *)geometry->vertices)[22].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.min.z + offset.z, 1.0f};
			((position_vertex_3d *)geometry->vertices)[23].position = (vec4){extents.max.x + offset.x, extents.max.y + offset.y, extents.max.z + offset.z, 1.0f};
		}
	}
}

kgeometry geometry_generate_line_box3d (vec3 size, kname name, vec3 offset) {
	return geometry_generate_line_box3d_typed(size, name, KGEOMETRY_TYPE_3D_STATIC_COLOUR, offset);
}

kgeometry geometry_generate_line_box3d_typed (vec3 size, kname name, kgeometry_type type, vec3 offset) {

	f32 half_width = size.x * 0.5f;
	f32 half_height = size.y * 0.5f;
	f32 half_depth = size.z * 0.5f;

	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = type;
	out_geometry.generation = INVALID_ID_U16; // NOTE: generation is 0 because this is technically the first "update"
	out_geometry.extents.min = (vec3){-half_width, -half_height, -half_depth};
	out_geometry.extents.max = (vec3){half_width, half_height, half_depth};
	// Always 0 since min/max of each axis are -/+ half of the size.
	out_geometry.center = vec3_zero();
	out_geometry.vertex_count = 2 * 12; // 12 lines to make a cube.
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;

	switch (type) {
	default:
		KASSERT("Only types of colour and positon_only are supported.");
		return (kgeometry){0};
	case KGEOMETRY_TYPE_3D_STATIC_COLOUR:
		out_geometry.vertex_element_size = sizeof(colour_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, out_geometry.vertex_count);
		break;
	case KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY:
		out_geometry.vertex_element_size = sizeof(position_vertex_3d);
		out_geometry.vertices = KALLOC_TYPE_CARRAY(position_vertex_3d, out_geometry.vertex_count);
		break;
	}

	// NOTE: line-based boxes do not have/need indices.
	out_geometry.index_count = 0;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.indices = 0;
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	extents_3d extents = {0};
	extents.min.x = -half_width;
	extents.min.y = -half_height;
	extents.min.z = -half_depth;
	extents.max.x = half_width;
	extents.max.y = half_height;
	extents.max.z = half_depth;

	geometry_recalculate_line_box3d_by_extents(&out_geometry, extents, offset);

	return out_geometry;
}

kgeometry geometry_generate_cube (f32 width, f32 height, f32 depth, f32 tile_x, f32 tile_y, kname name) {
	if (width == 0) {
		KWARN("Width must be nonzero. Defaulting to one.");
		width = 1.0f;
	}
	if (height == 0) {
		KWARN("Height must be nonzero. Defaulting to one.");
		height = 1.0f;
	}
	if (depth == 0) {
		KWARN("Depth must be nonzero. Defaulting to one.");
		depth = 1;
	}
	if (tile_x == 0) {
		KWARN("tile_x must be nonzero. Defaulting to one.");
		tile_x = 1.0f;
	}
	if (tile_y == 0) {
		KWARN("tile_y must be nonzero. Defaulting to one.");
		tile_y = 1.0f;
	}

	f32 half_width = width * 0.5f;
	f32 half_height = height * 0.5f;
	f32 half_depth = depth * 0.5f;

	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_3D_STATIC;
	out_geometry.generation = INVALID_ID_U16;
	out_geometry.extents.min = (vec3){-half_width, -half_height, -half_depth};
	out_geometry.extents.max = (vec3){half_width, half_height, half_depth};
	// Always 0 since min/max of each axis are -/+ half of the size.
	out_geometry.center = vec3_zero();
	out_geometry.vertex_element_size = sizeof(vertex_3d);
	out_geometry.vertex_count = 4 * 6; // 4 verts per side, 6 sides
	out_geometry.vertices = KALLOC_TYPE_CARRAY(vertex_3d, out_geometry.vertex_count);
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	out_geometry.index_element_size = sizeof(u32);
	out_geometry.index_count = 6 * 6; // 6 indices per side, 6 sides
	out_geometry.indices = KALLOC_TYPE_CARRAY(u32, out_geometry.index_count);
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	f32 min_x = -half_width;
	f32 min_y = -half_height;
	f32 min_z = -half_depth;
	f32 max_x = half_width;
	f32 max_y = half_height;
	f32 max_z = half_depth;
	f32 min_uvx = 0.0f;
	f32 min_uvy = 0.0f;
	f32 max_uvx = tile_x;
	f32 max_uvy = tile_y;

	vertex_3d verts[24];

	u32 f = 0;

	// Front face
	verts[(f * 4) + 0].position = (vec3){min_x, min_y, max_z};
	verts[(f * 4) + 1].position = (vec3){max_x, max_y, max_z};
	verts[(f * 4) + 2].position = (vec3){min_x, max_y, max_z};
	verts[(f * 4) + 3].position = (vec3){max_x, min_y, max_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){0.0f, 0.0f, 1.0f};
	verts[(f * 4) + 1].normal = (vec3){0.0f, 0.0f, 1.0f};
	verts[(f * 4) + 2].normal = (vec3){0.0f, 0.0f, 1.0f};
	verts[(f * 4) + 3].normal = (vec3){0.0f, 0.0f, 1.0f};
	++f;

	// Back face
	verts[(f * 4) + 0].position = (vec3){max_x, min_y, min_z};
	verts[(f * 4) + 1].position = (vec3){min_x, max_y, min_z};
	verts[(f * 4) + 2].position = (vec3){max_x, max_y, min_z};
	verts[(f * 4) + 3].position = (vec3){min_x, min_y, min_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){0.0f, 0.0f, -1.0f};
	verts[(f * 4) + 1].normal = (vec3){0.0f, 0.0f, -1.0f};
	verts[(f * 4) + 2].normal = (vec3){0.0f, 0.0f, -1.0f};
	verts[(f * 4) + 3].normal = (vec3){0.0f, 0.0f, -1.0f};
	++f;

	// Left
	verts[(f * 4) + 0].position = (vec3){min_x, min_y, min_z};
	verts[(f * 4) + 1].position = (vec3){min_x, max_y, max_z};
	verts[(f * 4) + 2].position = (vec3){min_x, max_y, min_z};
	verts[(f * 4) + 3].position = (vec3){min_x, min_y, max_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){-1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 1].normal = (vec3){-1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 2].normal = (vec3){-1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 3].normal = (vec3){-1.0f, 0.0f, 0.0f};
	++f;

	// Right face
	verts[(f * 4) + 0].position = (vec3){max_x, min_y, max_z};
	verts[(f * 4) + 1].position = (vec3){max_x, max_y, min_z};
	verts[(f * 4) + 2].position = (vec3){max_x, max_y, max_z};
	verts[(f * 4) + 3].position = (vec3){max_x, min_y, min_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 1].normal = (vec3){1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 2].normal = (vec3){1.0f, 0.0f, 0.0f};
	verts[(f * 4) + 3].normal = (vec3){1.0f, 0.0f, 0.0f};
	++f;

	// Bottom face
	verts[(f * 4) + 0].position = (vec3){max_x, min_y, max_z};
	verts[(f * 4) + 1].position = (vec3){min_x, min_y, min_z};
	verts[(f * 4) + 2].position = (vec3){max_x, min_y, min_z};
	verts[(f * 4) + 3].position = (vec3){min_x, min_y, max_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){0.0f, -1.0f, 0.0f};
	verts[(f * 4) + 1].normal = (vec3){0.0f, -1.0f, 0.0f};
	verts[(f * 4) + 2].normal = (vec3){0.0f, -1.0f, 0.0f};
	verts[(f * 4) + 3].normal = (vec3){0.0f, -1.0f, 0.0f};
	++f;

	// Top face
	verts[(f * 4) + 0].position = (vec3){min_x, max_y, max_z};
	verts[(f * 4) + 1].position = (vec3){max_x, max_y, min_z};
	verts[(f * 4) + 2].position = (vec3){min_x, max_y, min_z};
	verts[(f * 4) + 3].position = (vec3){max_x, max_y, max_z};
	verts[(f * 4) + 0].texcoord = (vec2){min_uvx, min_uvy};
	verts[(f * 4) + 1].texcoord = (vec2){max_uvx, max_uvy};
	verts[(f * 4) + 2].texcoord = (vec2){min_uvx, max_uvy};
	verts[(f * 4) + 3].texcoord = (vec2){max_uvx, min_uvy};
	verts[(f * 4) + 0].normal = (vec3){0.0f, 1.0f, 0.0f};
	verts[(f * 4) + 1].normal = (vec3){0.0f, 1.0f, 0.0f};
	verts[(f * 4) + 2].normal = (vec3){0.0f, 1.0f, 0.0f};
	verts[(f * 4) + 3].normal = (vec3){0.0f, 1.0f, 0.0f};
	++f;

	for (u32 i = 0; i < 24; ++i) {
		verts[i].colour = vec4_one();
	}

	kcopy_memory(out_geometry.vertices, verts, out_geometry.vertex_element_size * out_geometry.vertex_count);

	for (u32 i = 0; i < 6; ++i) {
		u32 v_offset = i * 4;
		u32 i_offset = i * 6;
		((u32 *)out_geometry.indices)[i_offset + 0] = v_offset + 0;
		((u32 *)out_geometry.indices)[i_offset + 1] = v_offset + 1;
		((u32 *)out_geometry.indices)[i_offset + 2] = v_offset + 2;
		((u32 *)out_geometry.indices)[i_offset + 3] = v_offset + 0;
		((u32 *)out_geometry.indices)[i_offset + 4] = v_offset + 3;
		((u32 *)out_geometry.indices)[i_offset + 5] = v_offset + 1;
	}

	geometry_generate_tangents(out_geometry.vertex_count, out_geometry.vertices, out_geometry.index_count, out_geometry.indices);

	return out_geometry;
}

kgeometry geometry_generate_grid (grid_orientation orientation, u32 segment_count_dim_0, u32 segment_count_dim_1, f32 segment_scale, b8 use_third_axis, kname name) {

	kgeometry out_geometry = {0};
	out_geometry.name = name;
	out_geometry.type = KGEOMETRY_TYPE_3D_STATIC_COLOUR;
	out_geometry.generation = INVALID_ID_U16;
	//
	f32 max_0 = segment_count_dim_0 * segment_scale;
	f32 min_0 = -max_0;
	f32 max_1 = segment_count_dim_1 * segment_scale;
	f32 min_1 = -max_1;
	switch (orientation) {
	default:
	case GRID_ORIENTATION_XZ:
		out_geometry.extents.min.x = min_0;
		out_geometry.extents.max.x = max_0;
		out_geometry.extents.min.z = min_1;
		out_geometry.extents.max.z = max_1;
		break;
	case GRID_ORIENTATION_XY:
		out_geometry.extents.min.x = min_0;
		out_geometry.extents.max.x = max_0;
		out_geometry.extents.min.y = min_1;
		out_geometry.extents.max.y = max_1;
		break;
	case GRID_ORIENTATION_YZ:
		out_geometry.extents.min.y = min_0;
		out_geometry.extents.max.y = max_0;
		out_geometry.extents.min.z = min_1;
		out_geometry.extents.max.z = max_1;
		break;
	}
	// Always 0 since min/max of each axis are -/+ half of the size.
	out_geometry.center = vec3_zero();
	out_geometry.vertex_element_size = sizeof(colour_vertex_3d);
	// Generated from the center out. Start with 2 or 3 axis lines. Then min/max lines in each direction for each cell.
	out_geometry.vertex_count = (use_third_axis ? 6 : 4) + (segment_count_dim_0 * 4) + (segment_count_dim_1 * 4);
	out_geometry.vertices = KALLOC_TYPE_CARRAY(colour_vertex_3d, out_geometry.vertex_count);
	out_geometry.vertex_buffer_offset = INVALID_ID_U64;
	out_geometry.index_element_size = 0; // sizeof(u32);
	out_geometry.index_count = 0;		 // no indices
	out_geometry.indices = 0;
	out_geometry.index_buffer_offset = INVALID_ID_U64;

	// Generate vertex data

	// Grid line lengths are the amount of spaces in the opposite direction.
	i32 line_length_0 = segment_count_dim_1 * segment_scale;
	i32 line_length_1 = segment_count_dim_0 * segment_scale;
	i32 line_length_2 = line_length_0 > line_length_1 ? line_length_0 : line_length_1;

	// f32 max_0 = segment_count_dim_0 * segment_scale;
	// f32 min_0 = -max_0;
	// f32 max_1 = segment_count_dim_1 * segment_scale;
	// f32 min_1 = -max_1;

	u32 element_index_0, element_index_1, element_index_2;

	switch (orientation) {
	default:
	case GRID_ORIENTATION_XZ:
		element_index_0 = 0; // x
		element_index_1 = 2; // z
		element_index_2 = 1; // y
		break;
	case GRID_ORIENTATION_XY:
		element_index_0 = 0; // x
		element_index_1 = 1; // y
		element_index_2 = 2; // z
		break;
	case GRID_ORIENTATION_YZ:
		element_index_0 = 1; // y
		element_index_1 = 2; // z
		element_index_2 = 0; // x
		break;
	}

	// First axis line
	((colour_vertex_3d *)out_geometry.vertices)[0].position.elements[element_index_0] = -line_length_1;
	((colour_vertex_3d *)out_geometry.vertices)[0].position.elements[element_index_1] = 0;
	((colour_vertex_3d *)out_geometry.vertices)[1].position.elements[element_index_0] = line_length_1;
	((colour_vertex_3d *)out_geometry.vertices)[1].position.elements[element_index_1] = 0;
	((colour_vertex_3d *)out_geometry.vertices)[0].colour.elements[element_index_0] = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[0].colour.a = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[1].colour.elements[element_index_0] = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[1].colour.a = 1.0f;

	// Second axis line
	((colour_vertex_3d *)out_geometry.vertices)[2].position.elements[element_index_0] = 0;
	((colour_vertex_3d *)out_geometry.vertices)[2].position.elements[element_index_1] = -line_length_0;
	((colour_vertex_3d *)out_geometry.vertices)[3].position.elements[element_index_0] = 0;
	((colour_vertex_3d *)out_geometry.vertices)[3].position.elements[element_index_1] = line_length_0;
	((colour_vertex_3d *)out_geometry.vertices)[2].colour.elements[element_index_1] = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[2].colour.a = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[3].colour.elements[element_index_1] = 1.0f;
	((colour_vertex_3d *)out_geometry.vertices)[3].colour.a = 1.0f;

	if (use_third_axis) {
		// Third axis line
		((colour_vertex_3d *)out_geometry.vertices)[4].position.elements[element_index_0] = 0;
		((colour_vertex_3d *)out_geometry.vertices)[4].position.elements[element_index_2] = -line_length_2;
		((colour_vertex_3d *)out_geometry.vertices)[5].position.elements[element_index_0] = 0;
		((colour_vertex_3d *)out_geometry.vertices)[5].position.elements[element_index_2] = line_length_2;
		((colour_vertex_3d *)out_geometry.vertices)[4].colour.elements[element_index_2] = 1.0f;
		((colour_vertex_3d *)out_geometry.vertices)[4].colour.a = 1.0f;
		((colour_vertex_3d *)out_geometry.vertices)[5].colour.elements[element_index_2] = 1.0f;
		((colour_vertex_3d *)out_geometry.vertices)[5].colour.a = 1.0f;
	}

	vec4 alt_line_colour = (vec4){1.0f, 1.0f, 1.0f, 0.5f};
	// calculate 4 lines at a time, 2 in each direction, min/max.
	i32 j = 1;

	u32 start_index = use_third_axis ? 6 : 4;

	for (u32 i = start_index; i < out_geometry.vertex_count; i += 8) {
		// First line (max)
		((colour_vertex_3d *)out_geometry.vertices)[i + 0].position.elements[element_index_0] = j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 0].position.elements[element_index_1] = line_length_0;
		((colour_vertex_3d *)out_geometry.vertices)[i + 0].colour = alt_line_colour;
		((colour_vertex_3d *)out_geometry.vertices)[i + 1].position.elements[element_index_0] = j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 1].position.elements[element_index_1] = -line_length_0;
		((colour_vertex_3d *)out_geometry.vertices)[i + 1].colour = alt_line_colour;

		// Second line (min)
		((colour_vertex_3d *)out_geometry.vertices)[i + 2].position.elements[element_index_0] = -j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 2].position.elements[element_index_1] = line_length_0;
		((colour_vertex_3d *)out_geometry.vertices)[i + 2].colour = alt_line_colour;
		((colour_vertex_3d *)out_geometry.vertices)[i + 3].position.elements[element_index_0] = -j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 3].position.elements[element_index_1] = -line_length_0;
		((colour_vertex_3d *)out_geometry.vertices)[i + 3].colour = alt_line_colour;

		// Third line (max)
		((colour_vertex_3d *)out_geometry.vertices)[i + 4].position.elements[element_index_0] = -line_length_1;
		((colour_vertex_3d *)out_geometry.vertices)[i + 4].position.elements[element_index_1] = -j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 4].colour = alt_line_colour;
		((colour_vertex_3d *)out_geometry.vertices)[i + 5].position.elements[element_index_0] = line_length_1;
		((colour_vertex_3d *)out_geometry.vertices)[i + 5].position.elements[element_index_1] = -j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 5].colour = alt_line_colour;

		// Fourth line (min)
		((colour_vertex_3d *)out_geometry.vertices)[i + 6].position.elements[element_index_0] = -line_length_1;
		((colour_vertex_3d *)out_geometry.vertices)[i + 6].position.elements[element_index_1] = j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 6].colour = alt_line_colour;
		((colour_vertex_3d *)out_geometry.vertices)[i + 7].position.elements[element_index_0] = line_length_1;
		((colour_vertex_3d *)out_geometry.vertices)[i + 7].position.elements[element_index_1] = j * segment_scale;
		((colour_vertex_3d *)out_geometry.vertices)[i + 7].colour = alt_line_colour;

		j++;
	}

	return out_geometry;
}

void geometry_destroy (kgeometry *geometry) {
	if (geometry) {
		if (geometry->vertices) {
			kfree(geometry->vertices);
		}
		if (geometry->indices) {
			kfree(geometry->indices);
		}
		kzero_memory(geometry, sizeof(kgeometry));

		// Setting this to invalidid effectively marks the geometry as "not setup".
		geometry->generation = INVALID_ID_U16;
		geometry->vertex_buffer_offset = INVALID_ID_U64;
		geometry->index_buffer_offset = INVALID_ID_U64;
	}
}

static inline vec4 axis_remap (axis_3d axis, f32 radial_x, f32 radial_y, f32 axis_pos) {
	switch (axis) {
	case AXIS_X:
		return (vec4){axis_pos, radial_x, radial_y, 1.0f};
	case AXIS_Y:
		return (vec4){radial_x, axis_pos, radial_y, 1.0f};
	default:
	case AXIS_Z:
		return (vec4){radial_x, radial_y, axis_pos, 1.0f};
	}
}

void generate_axis_geometry (
	axis_3d axis,
	f32 base_offset,
	f32 length,
	colour4 colour,
	f32 shaft_radius,
	f32 arrowhead_radius,
	f32 arrowhead_length,
	u32 segment_count,
	b8 include_arrowhead,
	u32 *out_vertex_count,
	u32 *out_index_count,
	colour_vertex_3d *vertices,
	u32 *indices,
	u32 vertex_offset) {

	const u32 segments = segment_count < 3 ? 3 : segment_count;

	const f32 arrow_len = include_arrowhead ? arrowhead_length : 0.0f;
	const f32 shaft_len = length - arrow_len;

	// Vertex count
	u32 vertex_count = 0;

	// Shaft rings
	const u32 shaft_bottom = vertex_count;
	vertex_count += segments;
	const u32 shaft_top = vertex_count;
	vertex_count += segments;

	// Arrowhead or cap
	u32 cone_base = 0;
	u32 cone_tip = 0;
	u32 cap_center = 0;

	if (include_arrowhead) {
		cone_base = vertex_count;
		vertex_count += segments;
		cone_tip = vertex_count;
		vertex_count += 1;
	} else {
		cap_center = vertex_count;
		vertex_count += 1;
	}

	// Index count
	u32 index_count = 0;

	// Shaft sides
	index_count += segments * 6;

	if (include_arrowhead) {
		// cone sides
		index_count += segments * 3;
	} else {
		// cap disc
		index_count += segments * 3;
	}

	if (out_vertex_count) {
		*out_vertex_count = vertex_count;
	}

	if (out_index_count) {
		*out_index_count = index_count;
	}

	if (!vertices || !indices) {
		return;
	}

	// Vertex generation
	u32 v = 0;

	axis_3d base_axis = axis;
	f32 offset = base_offset;
	vec4 shift_vector = vec4_zero();
	switch (axis) {
	default:
		break;
	case AXIS_XY:
		base_axis = AXIS_Y;
		offset = 0.0f;
		shift_vector = vec4_create(base_offset, 0, 0, 0);
		break;
	case AXIS_XZ:
		base_axis = AXIS_Z;
		offset = 0.0f;
		shift_vector = vec4_create(base_offset, 0, 0, 0);
		break;
	case AXIS_YX:
		base_axis = AXIS_X;
		offset = 0.0f;
		shift_vector = vec4_create(0, base_offset, 0, 0);
		break;
	case AXIS_YZ:
		base_axis = AXIS_Z;
		offset = 0.0f;
		shift_vector = vec4_create(0, base_offset, 0, 0);
		break;
	case AXIS_ZX:
		base_axis = AXIS_X;
		offset = 0.0f;
		shift_vector = vec4_create(0, 0, base_offset, 0);
		break;
	case AXIS_ZY:
		base_axis = AXIS_Y;
		offset = 0.0f;
		shift_vector = vec4_create(0, 0, base_offset, 0);
		break;
	}

	// Shaft bottom ring
	for (u32 i = 0; i < segments; ++i) {
		f32 a = (f32)i / (f32)segments * 2.0f * K_PI;
		f32 x = kcos(a) * shaft_radius;
		f32 y = ksin(a) * shaft_radius;

		vertices[v] = (colour_vertex_3d){
			.position = axis_remap(base_axis, x, y, offset),
			.colour = colour};

		v++;
	}

	// Shaft top ring
	for (u32 i = 0; i < segments; ++i) {
		f32 a = (f32)i / (f32)segments * 2.0f * K_PI;
		f32 x = kcos(a) * shaft_radius;
		f32 y = ksin(a) * shaft_radius;

		vertices[v] = (colour_vertex_3d){
			.position = axis_remap(base_axis, x, y, shaft_len),
			.colour = colour};
		v++;
	}

	// Arrowhead base or cap center
	if (include_arrowhead) {
		for (u32 i = 0; i < segments; ++i) {
			f32 a = (f32)i / (f32)segments * 2.0f * K_PI;
			f32 x = kcos(a) * arrowhead_radius;
			f32 y = ksin(a) * arrowhead_radius;

			vertices[v] = (colour_vertex_3d){
				.position = axis_remap(base_axis, x, y, shaft_len),
				.colour = colour};
			v++;
		}

		vertices[v] = (colour_vertex_3d){
			.position = axis_remap(base_axis, 0.0f, 0.0f, length),
			.colour = colour};
		v++;
	} else {
		vertices[v] = (colour_vertex_3d){
			.position = axis_remap(base_axis, 0.0f, 0.0f, shaft_len),
			.colour = colour};
		v++;
	}

	// Shift the vertices if needed.
	for (u32 i = 0; i < vertex_count; ++i) {
		vertices[i].position = vec4_add(vertices[i].position, shift_vector);
	}

	// Index generation
	u32 idx = 0;

	// Shaft sides
	for (u32 i = 0; i < segments; ++i) {
		u32 i0 = shaft_bottom + i;
		u32 i1 = shaft_bottom + (i + 1) % segments;
		u32 i2 = shaft_top + i;
		u32 i3 = shaft_top + (i + 1) % segments;

		indices[idx++] = vertex_offset + i0;
		indices[idx++] = vertex_offset + i2;
		indices[idx++] = vertex_offset + i1;

		indices[idx++] = vertex_offset + i1;
		indices[idx++] = vertex_offset + i2;
		indices[idx++] = vertex_offset + i3;
	}
	if (include_arrowhead) {
		// Cone sides
		for (u32 i = 0; i < segments; ++i) {
			u32 i0 = cone_base + i;
			u32 i1 = cone_base + (i + 1) % segments;

			indices[idx++] = vertex_offset + i0;
			indices[idx++] = vertex_offset + i1;
			indices[idx++] = vertex_offset + cone_tip;
		}
	} else {
		// flat cap
		for (u32 i = 0; i < segments; ++i) {
			u32 i0 = shaft_top + i;
			u32 i1 = shaft_top + (i + 1) % segments;

			indices[idx++] = vertex_offset + cap_center;
			indices[idx++] = vertex_offset + i1;
			indices[idx++] = vertex_offset + i0;
		}
	}
}

void generate_axis_ring_geometry (
	axis_3d axis,
	f32 radius,
	f32 thickness,
	colour4 colour,
	u32 ring_segments,
	u32 tube_segments,
	u32 *out_vertex_count,
	u32 *out_index_count,
	colour_vertex_3d *vertices,
	u32 *indices,
	u32 vertex_offset) {

	const u32 rings = ring_segments < 3 ? 3 : ring_segments;
	const u32 tubes = tube_segments < 3 ? 3 : tube_segments;

	const f32 minor_r = thickness * 0.5f;

	const u32 vertex_count = rings * tubes;
	const u32 index_count = rings * tubes * 6;

	if (out_vertex_count) {
		*out_vertex_count = vertex_count;
	}
	if (out_index_count) {
		*out_index_count = index_count;
	}
	if (!vertices || !indices) {
		return;
	}

	// Vertex generation
	u32 v = 0;

	for (u32 i = 0; i < rings; ++i) {
		f32 u = (f32)i / (f32)rings * 2.0f * K_PI;
		f32 cu = kcos(u);
		f32 su = ksin(u);

		for (u32 j = 0; j < tubes; ++j) {
			f32 v_ang = (f32)j / (f32)tubes * 2.0f * K_PI;
			f32 cv = kcos(v_ang);
			f32 sv = ksin(v_ang);

			/* Torus parametric equation (Z axis) */
			f32 x = (radius + minor_r * cv) * cu;
			f32 y = (radius + minor_r * cv) * su;
			f32 z = minor_r * sv;

			vertices[v++] = (colour_vertex_3d){
				.position = axis_remap(axis, x, y, z),
				.colour = colour};
		}
	}

	// Index generation
	u32 idx = 0;

	for (u32 i = 0; i < rings; ++i) {
		u32 ni = (i + 1) % rings;

		for (u32 j = 0; j < tubes; ++j) {
			u32 nj = (j + 1) % tubes;

			u32 i0 = i * tubes + j;
			u32 i1 = ni * tubes + j;
			u32 i2 = ni * tubes + nj;
			u32 i3 = i * tubes + nj;

			indices[idx++] = vertex_offset + i0;
			indices[idx++] = vertex_offset + i1;
			indices[idx++] = vertex_offset + i2;

			indices[idx++] = vertex_offset + i0;
			indices[idx++] = vertex_offset + i2;
			indices[idx++] = vertex_offset + i3;
		}
	}
}
