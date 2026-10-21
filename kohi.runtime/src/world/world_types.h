
#pragma once

#include <defines.h>
#include <math/math_types.h>

typedef enum kshape_type {
	KSHAPE_TYPE_SPHERE,
	KSHAPE_TYPE_RECTANGLE
} kshape_type;

typedef enum kscene_volume_type {
	KSCENE_VOLUME_TYPE_TRIGGER
} kscene_volume_type;

typedef struct kcollision_shape {
	kshape_type shape_type;

	union {
		f32 radius;
		vec3 extents;
	};
} kcollision_shape;

/**
 * An identifier for an entity within a scene.
 *
 * @description Memory layout:
 * u16 entity type. This could be reduced to a u8 if other data is needed in here.
 * u16 entity type index (or index into the type-specific array).
 * u16 unused/reserved - index of the internal hierarchy node array.
 * u16 unused/reserved for the future.
 */
typedef u64 kentity;
#define KENTITY_INVALID INVALID_ID_U64

typedef enum kentity_flag_bits {
	KENTITY_FLAG_NONE = 0,
	// This entity slot is free for use.
	KENTITY_FLAG_FREE_BIT = 1 << 0,

	KENTITY_FLAG_SERIALIZABLE_BIT = 1 << 1,
} kentity_flag_bits;

typedef u32 kentity_flags;

typedef enum kentity_type {
	KENTITY_TYPE_NONE,
	KENTITY_TYPE_MODEL,
	KENTITY_TYPE_HEIGHTMAP_TERRAIN,
	KENTITY_TYPE_WATER_PLANE,
	KENTITY_TYPE_AUDIO_EMITTER,
	KENTITY_TYPE_VOLUME,
	KENTITY_TYPE_HIT_SHAPE,
	KENTITY_TYPE_POINT_LIGHT,
	KENTITY_TYPE_SPAWN_POINT,

	// The number of types of entities. Not a valid entity type.
	KENTITY_TYPE_COUNT,

	// Also not a valid entity type. Used to identigy invalid entities (i.e. from data/config issues)
	KENTITY_TYPE_INVALID = INVALID_ID_U16

} kentity_type;
