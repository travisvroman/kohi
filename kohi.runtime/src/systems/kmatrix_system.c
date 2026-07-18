#include "kmatrix_system.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "systems/ktransform_system.h"

struct renderer_system_state;

typedef enum matrix_table_flag_bits {
	KMATRIX_TABLE_FLAG_NONE = 0,
	// Slot is occupied. If not set, considered free.
	KMATRIX_TABLE_FLAG_OCCUPIED_BIT = 1 << 0
} matrix_table_flag_bits;

typedef u8 matrix_table_flags;

typedef struct typed_matrix_table {
	u16 capacity;
	u16 count;
	matrix_table_flags *flags;
	mat4 *matrices;
} typed_matrix_table;

typedef struct kmatrix_system_state {
	struct renderer_system_state *renderer;
	krenderbuffer matrix_ssbo;
	typed_matrix_table tables[KMATRIX_TYPE_COUNT];
} kmatrix_system_state;

static void ensure_type_allocated (typed_matrix_table *table, u16 count);

b8 kmatrix_system_initialize (u64 *memory_requirement, struct kmatrix_system_state *state, kmatrix_system_config *config) {
	*memory_requirement = sizeof(kmatrix_system_state);

	if (!state) {
		return true;
	}

	kzero_memory(state, sizeof(kmatrix_system_state));

	state->renderer = engine_systems_get()->renderer_system;

	// Global matrix storage buffer
	state->matrix_ssbo = renderer_renderbuffer_create(
		state->renderer,
		kname_create(KRENDERBUFFER_NAME_MATRIX_GLOBAL),
		RENDERBUFFER_TYPE_STORAGE,
		config->max_matrix_count * sizeof(mat4),
		RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT | RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT);
	KASSERT(state->matrix_ssbo != KRENDERBUFFER_INVALID);
	KDEBUG("Created matrix global storage buffer.");

	return true;
}
void kmatrix_system_shutdown (struct kmatrix_system_state *state) {
	renderer_renderbuffer_destroy(state->renderer, state->matrix_ssbo);
	for (u8 i = 0; i < KMATRIX_TYPE_COUNT; ++i) {
		kfree(state->tables[i].flags);
		kfree(state->tables[i].matrices);
	}
}

void kmatrix_system_update (struct kmatrix_system_state *state, struct frame_data *p_frame_data) {

	// Update the data in the SSBO.
	void *mapped_memory = renderer_renderbuffer_get_mapped_memory(engine_systems_get()->renderer_system, state->matrix_ssbo);
	mat4 *mapped_transforms = (mat4 *)mapped_memory;

	// Copy the table datas in order of type.
	u16 total_offset = 0;
	for (u8 i = 0; i < KMATRIX_TYPE_COUNT; ++i) {
		typed_matrix_table *table = &state->tables[i];
		kcopy_memory(mapped_transforms + total_offset, table->matrices, sizeof(mat4) * table->capacity);
		total_offset += table->capacity;
	}
}

u16 kmatrix_system_get_offset_by_type (struct kmatrix_system_state *state, kmatrix_type type) {
	u16 total_offset = 0;
	for (u8 i = 0; i < KMATRIX_TYPE_COUNT; ++i) {
		if (i == type) {
			return total_offset;
		}
		total_offset += state->tables[i].capacity;
	}

	return total_offset;
}

kmatrix_id kmatrix_system_add (struct kmatrix_system_state *state, kmatrix_type type, mat4 m) {
	kmatrix_id new_id = KMATRIX_INVALID;
	typed_matrix_table *table = &state->tables[type];
	ensure_type_allocated(table, table->count + 1);

	// Search for the empty slot.
	for (u16 i = 0; i < table->capacity; ++i) {
		if (!FLAG_GET(table->flags[i], KMATRIX_TABLE_FLAG_OCCUPIED_BIT)) {
			// found one.
			FLAG_SET(table->flags[i], KMATRIX_TABLE_FLAG_OCCUPIED_BIT, true);
			new_id = PACK_U32_U16S((u16)type, i);
			table->count++;
			break;
		}
	}

	// If there is not one, even after the ensure_allocate, then it's an error.
	if (new_id == KMATRIX_INVALID) {
		KFATAL("%s() - Failed to add new matrix.", __FUNCTION__);
	}

	return new_id;
}
void kmatrix_system_remove (struct kmatrix_system_state *state, kmatrix_id *id) {
	if (*id != KMATRIX_INVALID) {
		kmatrix_type type;
		u16 index;

		UNPACK_U32_U16S(*id, type, index);

		typed_matrix_table *table = &state->tables[type];
		FLAG_SET(table->flags[index], KMATRIX_TABLE_FLAG_OCCUPIED_BIT, false);
		table->count--;

		*id = KMATRIX_INVALID;
	}
}

b8 kmatrix_system_update_by_id (struct kmatrix_system_state *state, kmatrix_id id, mat4 m) {
	if (id != KMATRIX_INVALID) {
		kmatrix_type type;
		u16 index;

		UNPACK_U32_U16S(id, type, index);

		typed_matrix_table *table = &state->tables[type];
		if (FLAG_GET(table->flags[index], KMATRIX_TABLE_FLAG_OCCUPIED_BIT)) {
			table->matrices[index] = m;
			return true;
		} else {
			KWARN("%s() - Attempted to update a matrix in a slot not marked as occupied. This likely means the handle is stale. Nothing to do.", __FUNCTION__);
			return false;
		}
	}

	KWARN("%s() - Attempted to update a matrix with an invalid id. Nothing to do.", __FUNCTION__);
	return false;
}

void kmatrix_system_bulk_update_transforms (struct kmatrix_system_state *state, mat4 *transforms) {
	typed_matrix_table *table = &state->tables[KMATRIX_TYPE_TRANSFORM];
	u64 update_size = table->capacity * sizeof(mat4);
	kcopy_memory(table->matrices, transforms, update_size);
}

static void realloc_table (typed_matrix_table *table, u16 new_count) {
	mat4 *new_mats = KALLOC_TYPE_CARRAY(mat4, new_count);
	KCOPY_TYPE_CARRAY(new_mats, table->matrices, mat4, table->capacity);
	kfree(table->matrices);
	table->matrices = new_mats;

	matrix_table_flags *new_flags = KALLOC_TYPE_CARRAY(matrix_table_flags, new_count);
	KCOPY_TYPE_CARRAY(new_flags, table->flags, matrix_table_flags, table->capacity);
	kfree(table->flags);
	table->flags = new_flags;

	table->capacity = new_count;
}

static void ensure_type_allocated (typed_matrix_table *table, u16 count) {
	if (count > table->capacity) {
		realloc_table(table, count);
	} else {
		u16 diff = table->capacity - table->count;
		if (diff < count) {
			realloc_table(table, table->capacity + diff);
		}
	}
}
