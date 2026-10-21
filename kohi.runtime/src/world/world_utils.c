#include "world/world_utils.h"

#include "world/world_types.h"

#include <logger.h>
#include <strings/kstring.h>

kentity_type kentity_type_from_string (const char *str) {
	if (!str || strings_equali(str, "none") || string_length(str) < 1) {
		return KENTITY_TYPE_NONE;
	} else if (strings_equali(str, "model")) {
		return KENTITY_TYPE_MODEL;
	} else if (strings_equali(str, "heightmap_terrain")) {
		return KENTITY_TYPE_HEIGHTMAP_TERRAIN;
	} else if (strings_equali(str, "water_plane")) {
		return KENTITY_TYPE_WATER_PLANE;
	} else if (strings_equali(str, "audio_emitter")) {
		return KENTITY_TYPE_AUDIO_EMITTER;
	} else if (strings_equali(str, "volume")) {
		return KENTITY_TYPE_VOLUME;
	} else if (strings_equali(str, "hit_shape")) {
		return KENTITY_TYPE_HIT_SHAPE;
	} else if (strings_equali(str, "point_light")) {
		return KENTITY_TYPE_POINT_LIGHT;
	} else if (strings_equali(str, "spawn_point")) {
		return KENTITY_TYPE_SPAWN_POINT;
	} else {
		return KENTITY_TYPE_NONE;
	}
}

const char *kentity_type_to_string (kentity_type type) {
	switch (type) {
	case KENTITY_TYPE_NONE:
		return "none";
	case KENTITY_TYPE_MODEL:
		return "model";
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
		return "heightmap_terrain";
	case KENTITY_TYPE_WATER_PLANE:
		return "water_plane";
	case KENTITY_TYPE_AUDIO_EMITTER:
		return "audio_emitter";
	case KENTITY_TYPE_VOLUME:
		return "volume";
	case KENTITY_TYPE_HIT_SHAPE:
		return "hit_shape";
	case KENTITY_TYPE_POINT_LIGHT:
		return "point_light";
	case KENTITY_TYPE_SPAWN_POINT:
		return "spawn_point";
	case KENTITY_TYPE_COUNT:
	case KENTITY_TYPE_INVALID:
		KERROR("%s - tried to convert invalid or count to string, ya dingus!");
		return "none";
	}
}

b8 kentity_type_ignores_scale (kentity_type type) {
	switch (type) {
	default:
	case KENTITY_TYPE_NONE:
	case KENTITY_TYPE_MODEL:
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
	case KENTITY_TYPE_WATER_PLANE:
		return false;
	case KENTITY_TYPE_AUDIO_EMITTER:
	case KENTITY_TYPE_VOLUME:
	case KENTITY_TYPE_HIT_SHAPE:
	case KENTITY_TYPE_POINT_LIGHT:
	case KENTITY_TYPE_SPAWN_POINT:
		return true;
	}
}

kshape_type kshape_type_from_string (const char *str) {
	if (strings_equali(str, "sphere")) {
		return KSHAPE_TYPE_SPHERE;
	} else if (strings_equali(str, "rectangle")) {
		return KSHAPE_TYPE_RECTANGLE;
	} else {
		KERROR("Unknown shape_type of '%s' was provided, defaulting to sphere.", str);
		return KSHAPE_TYPE_SPHERE;
	}
}

const char *kshape_type_to_string (kshape_type type) {
	switch (type) {
	case KSHAPE_TYPE_SPHERE:
		return "sphere";
	case KSHAPE_TYPE_RECTANGLE:
		return "rectangle";
	}
}

kscene_volume_type scene_volume_type_from_string (const char *str) {
	if (str) {
		if (strings_equali(str, "trigger")) {
			return KSCENE_VOLUME_TYPE_TRIGGER;
		}
	}

	KWARN("%s - unknown scene volume type '%s'. Defaulting to 'trigger'.", __FUNCTION__, str);
	return KSCENE_VOLUME_TYPE_TRIGGER;
}

const char *scene_volume_type_to_string (kscene_volume_type type) {
	switch (type) {
	case KSCENE_VOLUME_TYPE_TRIGGER:
		return "trigger";
	}
}
