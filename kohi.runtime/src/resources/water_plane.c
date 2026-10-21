#include "water_plane.h"

#include "core/engine.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "strings/kname.h"
#include "systems/kmaterial_system.h"

b8 water_plane_create (water_plane *out_plane) {
	if (!out_plane) {
		return false;
	}

	kzero_memory(out_plane, sizeof(water_plane));

	out_plane->model = mat4_identity();

	return true;
}
void water_plane_destroy (water_plane *plane) {
	if (plane) {

		kzero_memory(plane, sizeof(water_plane));
	}
}

b8 water_plane_initialize (water_plane *plane) {
	if (plane) {

		// Create the geometry, but don't load it yet.
		// TODO: should probably be based on some size.
		f32 size = 256.0f;
		plane->vertices[0] = (vertex_3d){-size, 0, -size, 0, 0, 1, 0, 0};
		plane->vertices[1] = (vertex_3d){-size, 0, +size, 0, 0, 1, 0, 1};
		plane->vertices[2] = (vertex_3d){+size, 0, +size, 0, 0, 1, 1, 1};
		plane->vertices[3] = (vertex_3d){+size, 0, -size, 0, 0, 1, 1, 0};
		for (u8 i = 0; i < 4; ++i) {
			plane->vertices[i].normal = (vec3){0, 0, 1};
			plane->vertices[i].colour = vec4_one();
			plane->vertices[i].tangent = (vec4){1, 0, 0, 1};
		}

		plane->indices[0] = 0;
		plane->indices[1] = 1;
		plane->indices[2] = 2;
		plane->indices[3] = 2;
		plane->indices[4] = 3;
		plane->indices[5] = 0;

		/* geometry_generate_normals(4, plane->vertices, 6, plane->indices);
		geometry_generate_tangents(4, plane->vertices, 6, plane->indices); */

		return true;
	}
	return false;
}

b8 water_plane_load (water_plane *plane) {
	if (plane) {

		// Get water material.
		// FIXME: Make this configurable.
		plane->material = kmaterial_system_get_default_water(engine_systems_get()->material_system);

		struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;

		krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
		krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
		// Allocate space
		if (!renderer_renderbuffer_allocate(renderer_system, vertex_buffer, sizeof(vertex_3d) * 4, &plane->vertex_buffer_offset)) {
			KERROR("Failed to allocate space in vertex buffer.");
			return false;
		}
		if (!renderer_renderbuffer_allocate(renderer_system, index_buffer, sizeof(u32) * 6, &plane->index_buffer_offset)) {
			KERROR("Failed to allocate space in index buffer.");
			return false;
		}

		// Load data
		if (!renderer_renderbuffer_load_range(renderer_system, vertex_buffer, plane->vertex_buffer_offset, sizeof(vertex_3d) * 4, plane->vertices, false)) {
			KERROR("Failed to load data into vertex buffer.");
			return false;
		}
		if (!renderer_renderbuffer_load_range(renderer_system, index_buffer, plane->index_buffer_offset, sizeof(u32) * 6, plane->indices, false)) {
			KERROR("Failed to load data into index buffer.");
			return false;
		}

		return true;
	}
	return false;
}

b8 water_plane_unload (water_plane *plane) {
	if (plane) {

		struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;

		krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
		krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
		// Free space
		if (!renderer_renderbuffer_free(renderer_system, vertex_buffer, sizeof(vertex_3d) * 4, plane->vertex_buffer_offset)) {
			KERROR("Failed to free space in vertex buffer.");
			return false;
		}
		if (!renderer_renderbuffer_free(renderer_system, index_buffer, sizeof(u32) * 6, plane->index_buffer_offset)) {
			KERROR("Failed to allfreeocate space in index buffer.");
			return false;
		}

		// Release material instance resources for this plane.
		kmaterial_system_release(engine_systems_get()->material_system, &plane->material);
		return true;
	}
	return false;
}

b8 water_plane_update (water_plane *plane) {
	if (plane) {
		//
		return true;
	}
	return false;
}
