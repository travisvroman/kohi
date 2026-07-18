#pragma once

#include <math/math_types.h>

struct frame_data;
struct kmatrix_system_state;

typedef enum kmatrix_type {
	KMATRIX_TYPE_VIEW,
	KMATRIX_TYPE_PROJECTION,
	KMATRIX_TYPE_TRANSFORM,
	KMATRIX_TYPE_GENERAL,
	KMATRIX_TYPE_COUNT
} kmatrix_type;

typedef struct kmatrix_system_config {
	// This directly impacts how much memory is reserved for the SSBO, so don't crank this up really high.
	u16 max_matrix_count;
} kmatrix_system_config;

// Stores u16 kmatrix_type and u16 id (offset into the matrix type array).
typedef u32 kmatrix_id;

#define KMATRIX_INVALID U32_MAX

/*
 * Matrices are all stored in a global SSBO together, grouped by type, and look something like this
 * in the overall block of memory:
 * [views...][projections...][transforms...][generals/none types...]
 * Note that these are in matching order to the kmatrix_type enum.
 *
 * When a matrix of a given type is added, its array size _can_ be expanded if no slots are left, leading
 * to the others being essentially "pushed out" in the buffer. These arrays never shrink because the kmatrix_id
 * is built with a direct index into the type's array. Removing a matrix of a given type simply marks that "slot"
 * as "free", allowing it to be used the next time a matrix is added.
 */

KAPI b8 kmatrix_system_initialize (u64 *memory_requirement, struct kmatrix_system_state *state, kmatrix_system_config *config);
KAPI void kmatrix_system_shutdown (struct kmatrix_system_state *state);

KAPI void kmatrix_system_update (struct kmatrix_system_state *state, struct frame_data *p_frame_data);

KAPI u16 kmatrix_system_get_offset_by_type (struct kmatrix_system_state *state, kmatrix_type type);

KAPI kmatrix_id kmatrix_system_add (struct kmatrix_system_state *state, kmatrix_type type, mat4 m);
// Invalidates the provided id.
KAPI void kmatrix_system_remove (struct kmatrix_system_state *state, kmatrix_id *id);

// Update the value of a single matrix.
KAPI b8 kmatrix_system_update_by_id (struct kmatrix_system_state *state, kmatrix_id id, mat4 m);

// Updates all transforms at once. This is the only type that needs to be (or can be) updated this way.
KAPI void kmatrix_system_bulk_update_transforms (struct kmatrix_system_state *state, mat4 *transforms);
