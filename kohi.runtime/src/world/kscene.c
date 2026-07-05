#include "kscene.h"

#include <audio/audio_frontend.h>
#include <containers/bvh.h>
#include <containers/darray.h>
#include <containers/u64_bst.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/frame_data.h>
#include <core_render_types.h>
#include <core_resource_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/platform.h>
#include <renderer/kforward_renderer.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/debug/debug_grid.h>
#include <resources/skybox.h>
#include <resources/water_plane.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <strings/kstring_id.h>
#include <systems/kcamera_system.h>
#include <systems/kmaterial_system.h>
#include <systems/kmodel_system.h>
#include <systems/ktransform_system.h>
#include <systems/light_system.h>
#include <systems/texture_system.h>
#include <utils/kcolour.h>
#include <world/world_types.h>

#include "assets/kasset_types.h"
#include "serializers/kasset_hf_terrain_serializer.h"
#include "systems/asset_system.h"
#include "world/heightfield_terrain.h"
#include "world/world_utils.h"

#define kSCENE_CURRENT_VERSION 1

#define ENTITY_VOLUME_DEBUG_COLOUR \
	(colour4){1, 1, 0, 1}
#define ENTITY_AUDIO_EMITTER_DEBUG_COLOUR \
	(colour4){1, 0.5f, 0, 1}
#define ENTITY_MODEL_STATIC_DEBUG_COLOUR \
	(colour4){0, 1, 0, 1}
#define ENTITY_MODEL_ANIMATED_DEBUG_COLOUR \
	(colour4){0, 1, 1, 1}

/**
 * A base entity with no type. Used for grouping other entities together, for example
 */
typedef struct base_entity {
	kentity_type type;
	// Keep the name here for reverse lookup.
	kname name;
	ktransform transform;
	kentity_flags flags;
	// Case-sensitive tags used to determine what volumes this hit shape interacts with.
	u8 tag_count;
	kstring_id* tags;

	// An darray of child entity handles.
	kentity* children;
	kentity parent;

	// The extents for the entity.
	extents_3d extents;

	bvh_id bvh_id;

#if KOHI_DEBUG
	// Index into debug data array, unique across all types.
	u32 debug_data_index;
#endif
} base_entity;

/**
 * A model specialized entity.
 */
typedef struct model_entity {
	base_entity base;
	kmodel_instance model;

	// Metadata for serialization later
	kname asset_name;
	kname package_name;
} model_entity;

/**
 * A point light type entity.
 */
typedef struct point_light_entity {
	base_entity base;

	/** @brief The light colour. This is the base version that gets (de)serialized. */
	colour3 colour;
	/** @brief Reduces light intensity linearly. This is the base version that gets (de)serialized. */
	f32 linear;
	/** @brief Makes the light fall off slower at longer distances. This is the base version that gets (de)serialized. */
	f32 quadratic;

	// A handle into the light system, that contains the data.
	klight handle;
} point_light_entity;

typedef struct spawn_point_entity {
	base_entity base;
	f32 radius;
} spawn_point_entity;

typedef struct volume_entity {
	base_entity base;

	kscene_volume_type type;

	kcollision_shape shape;

	// Case-sensitive tags used to determine if what hit shapes qualify to trigger commands in this volume.
	u8 hit_shape_tag_count;
	kstring_id* hit_shape_tags;

	// Called when something enters the volume.
	const char* on_enter_command;
	// Called when something leaves the volume.
	const char* on_leave_command;
	// Called every update tick.
	const char* on_tick_command;

} volume_entity;

typedef struct hit_shape_entity {
	base_entity base;

	kcollision_shape shape;
} hit_shape_entity;

typedef struct kgeometry_ref {
	// The entity the geometry belongs to.
	kentity entity;
	// Index into the static_geometry_datas array.
	u16 geometry_index;
} kgeometry_ref;

typedef struct water_plane_entity {
	base_entity base;
	kmaterial base_material;
	kgeometry_ref ref;
	u32 size;
	kgeometry geo;
} water_plane_entity;

typedef enum audio_emitter_entity_flag_bits {
	AUDIO_EMITTER_ENTITY_FLAG_NONE = 0,
	// Used for longer audio assets such as songs that should stream from the source instead of loading the entire thing.
	AUDIO_EMITTER_ENTITY_FLAG_STREAMING = 1 << 0,
} audio_emitter_entity_flag_bits;

typedef u32 audio_emitter_entity_flags;

typedef struct audio_emitter_entity {
	base_entity base;

	// Handle to the emitter within the audio system.
	kaudio_emitter emitter;

	audio_emitter_entity_flags flags;

	// For serialization
	kname asset_name;
	kname package_name;
	f32 inner_radius;
	f32 outer_radius;
	f32 falloff;
	f32 volume;
	b8 is_streaming;
	b8 is_looping;
} audio_emitter_entity;

// Map material id to geometry references.
typedef struct kmaterial_geometry_list {
	kmaterial base_material;
	u16 count;
	u16 capacity;
	kgeometry_ref* geometries;
} kmaterial_geometry_list;

typedef struct kmaterial_to_geometry_map {
	u16 count;
	u16 capacity;
	kmaterial_geometry_list* lists;
} kmaterial_to_geometry_map;

typedef enum kgeometry_data_flag_bits {
	KGEOMETRY_DATA_FLAG_NONE = 0,
	// This geometry data is free for use in the array.
	KGEOMETRY_DATA_FLAG_FREE_BIT = 1 << 0,
	KGEOMETRY_DATA_FLAG_WINDING_INVERTED_BIT = 1 << 1,
} kgeometry_data_flag_bits;

typedef u32 kgeometry_data_flags;

// Holds geometry data required for rendering later on
typedef struct kgeometry_data {
	u64 vertex_offset;
	u32 vertex_count;
	u64 index_offset;
	u32 index_count;
	kgeometry_data_flags flags;

	// The material instance for this geometry.
	u16 material_instance_id;
} kgeometry_data;

#if KOHI_DEBUG
typedef enum kscene_debug_data_type {
	KSCENE_DEBUG_DATA_TYPE_NONE,
	KSCENE_DEBUG_DATA_TYPE_RECTANGLE,
	KSCENE_DEBUG_DATA_TYPE_SPHERE,
} kscene_debug_data_type;

typedef struct kscene_debug_data {
	kscene_debug_data_type type;
	kgeometry geometry;
	kentity owner;
	mat4 model;
	colour4 colour;
	b8 ignore_scale;
} kscene_debug_data;
#endif

#if KOHI_DEBUG
typedef struct scene_bvh_debug_data {
	kgeometry geo;
	mat4 model;
} scene_bvh_debug_data;
#endif

// Entry is considered "free" if both a and b are set to kENTITY_INVALID
typedef struct collision_shape_state {
	// Reference to the first entity. Not owned by this state.
	kentity a;
	// Reference to the second entity. Not owned by this state.
	kentity b;
} collision_shape_state;

/**
 * The internal representation of a scene that holds state, entity data, etc.
 */
typedef struct kscene {
	kscene_state state;

	i32 queued_initial_asset_loads;

	kscene_flags flags;

	// Invoked when the initial load of the scene is complete.
	PFN_scene_loaded loaded_callback;
	void* load_context;

	u8 version;
	const char* name;
	const char* description;

	// BST name lookup (key=name, value=kentity)
	bt_node* name_lookup;

	kname skybox_asset_name;
	kname skybox_asset_package_name;
	skybox sb;

	ktexture default_irradiance_texture;

	klight directional_light;

	f32 shadow_dist;
	f32 shadow_fade_dist;
	f32 shadow_split_mult;
	f32 shadow_bias;

	// Fog settings
	colour4 fog_colour;
	f32 fog_near;
	f32 fog_far;

	// Used for rendering reflections in the world.
	kcamera world_inv_camera;

	bvh bvh_tree;
	ktransform bvh_transform;
#if KOHI_DEBUG
	// A pool of bvh debug datas that hold render representation.
	scene_bvh_debug_data* bvh_debug_pool;
	// A pool holding all vertices for all BVH render boxes.
	position_vertex_3d* bvh_debug_vertex_pool;
	// Count of elements in the pool.
	u16 bvh_debug_pool_size;

	// Debug grid
	debug_grid grid;
#endif

	// darray of 'parentless' entities
	kentity* root_entities;

	// Base entities with no type
	base_entity* bases;

	// darray of model type entities
	model_entity* models;
	// Mapping of geometry datas by opaque material.
	kmaterial_to_geometry_map opaque_static_model_material_map;
	// Mapping of geometry datas by transparent material.
	kmaterial_to_geometry_map transparent_static_model_material_map;
	// Mapping of geometry datas by opaque material.
	kmaterial_to_geometry_map opaque_animated_model_material_map;
	// Mapping of geometry datas by transparent material.
	kmaterial_to_geometry_map transparent_animated_model_material_map;
	// Data required to render animated geometry datas
	kgeometry_data* model_geometry_datas;
	extents_3d* model_geometry_extents;

	// darray of point light type entities.
	point_light_entity* point_lights;

	spawn_point_entity* spawn_points;

	// darray of volume type entities.
	volume_entity* volumes;

	// darray of hit shape type entities.
	hit_shape_entity* hit_shapes;

	// darray of water plane type entities.
	water_plane_entity* water_planes;

	// darray of audio emitter type entities.
	audio_emitter_entity* audio_emitters;

	// darray of active collision shape states.
	collision_shape_state* col_shape_states;

	kname scene_asset_name;
	const char* hf_terrain_asset_name;
	hf_terrain hf;

#if KOHI_DEBUG
	// Darray of debug render data.
	kscene_debug_data* debug_datas;
#endif
} kscene;

static kentity init_base_entity_with_extents(kscene* scene, base_entity* base, u16 entity_index, kname name, kentity_type type, ktransform transform, kentity parent, extents_3d extents);
static kentity init_base_entity(kscene* scene, base_entity* base, u16 entity_index, kname name, kentity_type type, ktransform transform, kentity parent);
static void kmaterial_list_ensure_allocated(kmaterial_geometry_list* list);
static void kmaterial_map_ensure_allocated(kmaterial_to_geometry_map* map);
static kmaterial_geometry_list* get_or_create_material_geo_list(kmaterial_to_geometry_map* map, kmaterial material);

static void on_model_loaded(kmodel_instance instance, void* context);
static void map_model_submesh_geometries(kscene* scene, kentity entity, u16 submesh_index, b8 winding_inverted, const kmaterial_instance* mat_inst);
// maps entity geometries by material. Should only be used for loaded entities.
static void map_model_entity_geometries(kscene* scene, kentity entity);
static void unmap_model_entity_geometries(kscene* scene, kentity entity);

// Handles notifications that an inital load entity type that has async asset load started.
static void notify_initial_load_entity_started(kscene* scene, kentity entity);
// Handles notifications of initial asset load completion and updates counts.
static void notify_initial_load_entity_complete(kscene* scene, kentity entity);

static b8 deserialize(const char* file_content, kscene* out_scene);
// Returns KNULL if not found
static base_entity* get_entity_base(kscene* scene, kentity entity);

static void base_entity_destroy(kscene* scene, base_entity* base, kentity entity_handle);
static void model_entity_destroy(kscene* scene, model_entity* typed_entity, kentity entity_handle);
static void point_light_entity_destroy(kscene* scene, point_light_entity* typed_entity, kentity entity_handle);
static void spawn_point_entity_destroy(kscene* scene, spawn_point_entity* typed_entity, kentity entity_handle);
static void volume_entity_destroy(kscene* scene, volume_entity* typed_entity, kentity entity_handle);
static void hit_shape_entity_destroy(kscene* scene, hit_shape_entity* typed_entity, kentity entity_handle);
static void water_plane_entity_destroy(kscene* scene, water_plane_entity* typed_entity, kentity entity_handle);
static void audio_emitter_entity_destroy(kscene* scene, audio_emitter_entity* typed_entity, kentity entity_handle);

#if KOHI_DEBUG
static void create_debug_data(kscene* scene, vec3 size, vec3 center, kentity entity, kscene_debug_data_type type, colour4 colour, b8 ignore_scale, u32* out_debug_data_index);
static kscene_debug_data_type debug_type_from_shape_type(kshape_type type);
#else
// Intentional no-op in release
#	define create_debug_data(scene, size, center, entity, type, colour, ignore_scale, out_debug_data_index)
#	define debug_type_from_shape_type(type)
#endif

struct kscene* kscene_create(kname scene_asset_name, const char* config, PFN_scene_loaded loaded_callback, void* load_context, b8 is_editor) {

	kscene* scene = KALLOC_TYPE(kscene, MEMORY_TAG_SCENE);
	scene->state = KSCENE_STATE_UNINITIALIZED;

	scene->directional_light = KLIGHT_INVALID;
	scene->loaded_callback = loaded_callback;
	scene->load_context = load_context;
	scene->scene_asset_name = scene_asset_name;

	if (!bvh_create(0, scene, &scene->bvh_tree)) {
		KERROR("Failed to create BVH");
		return KNULL;
	}
	scene->bvh_transform = ktransform_create(0);

#if KOHI_DEBUG
	scene->bvh_debug_pool_size = 256;
	scene->bvh_debug_pool = KALLOC_TYPE_CARRAY(scene_bvh_debug_data, scene->bvh_debug_pool_size);
	scene->bvh_debug_vertex_pool = KALLOC_TYPE_CARRAY(position_vertex_3d, 24 * scene->bvh_debug_pool_size);

	struct renderer_system_state* renderer = engine_systems_get()->renderer_system;

	// Allocate space in the vertex buffer for the entire pool.
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	// Vertex size * number of verts per box * number of boxes
	u64 total_size = sizeof(position_vertex_3d) * 24 * scene->bvh_debug_pool_size;
	u64 start_offset = 0;
	if (!renderer_renderbuffer_allocate(renderer, vertex_buffer, total_size, &start_offset)) {
		KERROR("Failed to create pool for BVH debug data.");
		return KNULL;
	}
	// Iterate all the debug datas in the pool and set their geometry offsets, one
	// right after the other.
	for (u16 i = 0; i < scene->bvh_debug_pool_size; ++i) {
		scene_bvh_debug_data* d = &scene->bvh_debug_pool[i];
		d->geo.type = KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY;
		d->geo.vertex_element_size = sizeof(position_vertex_3d);
		d->geo.vertex_count = 24;
		// NOTE: use from the giant vertex pool.
		d->geo.vertices = scene->bvh_debug_vertex_pool + (24 * i);
		d->geo.vertex_buffer_offset = start_offset + (sizeof(position_vertex_3d) * 24 * i);

		d->geo.index_count = 0;
		d->geo.index_element_size = 0;
		d->geo.indices = 0;
		d->geo.index_buffer_offset = 0;

		d->model = mat4_identity();
	}

	// Create/load debug grid.
	debug_grid_config grid_config = {
		.name = kname_create("__debug_grid__"),
		.orientation = GRID_ORIENTATION_XZ,
		.segment_count_dim_0 = 100,
		.segment_count_dim_1 = 100,
		.segment_size = 1};
	debug_grid_create(&grid_config, &scene->grid);
	debug_grid_initialize(&scene->grid);
	debug_grid_load(&scene->grid);

	// Enable general debug if editor mode
	if (is_editor) {
		FLAG_SET(scene->flags, KSCENE_FLAG_DEBUG_ENABLED_BIT, true);
		FLAG_SET(scene->flags, KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT, false);
		FLAG_SET(scene->flags, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT, true);
	}
#endif

	// Enable rendering everything by default.
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_SKYBOX_BIT, true);
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_STATIC_MODELS_BIT, true);
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_ANIMATED_MODELS_BIT, true);
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_HF_TERRAIN_BIT, true);
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_WATER_PLANES_BIT, true);
	FLAG_SET(scene->flags, KSCENE_FLAG_RENDER_FOG_BIT, true);

	scene->root_entities = darray_create(kentity);

	// "typeless" entities
	scene->bases = darray_create(base_entity);

	// Model entities
	scene->models = darray_create(model_entity);
	scene->model_geometry_datas = darray_create(kgeometry_data);
	scene->model_geometry_extents = darray_create(extents_3d);

	scene->point_lights = darray_create(point_light_entity);
	scene->spawn_points = darray_create(spawn_point_entity);

	scene->volumes = darray_create(volume_entity);
	scene->hit_shapes = darray_create(hit_shape_entity);

	scene->water_planes = darray_create(water_plane_entity);

	scene->audio_emitters = darray_create(audio_emitter_entity);

	scene->col_shape_states = darray_create(collision_shape_state);

#if KOHI_DEBUG
	// Holds debug geometry data.
	scene->debug_datas = darray_reserve(kscene_debug_data, 64);
#endif

	scene->name_lookup = KNULL;

	// Create a camera to be used for reflections. Its properties don't matter much for now.
	kwindow* win = engine_active_window_get();
	rect_2di world_vp_rect = {0, 0, win->width, win->height};
	scene->world_inv_camera = kcamera_create(KCAMERA_TYPE_3D, world_vp_rect, vec3_zero(), vec3_zero(), deg_to_rad(45.0f), 0.1f, 1000.0f);

	// Flip state to loading until all is done.
	scene->state = KSCENE_STATE_PARSING_CONFIG;
	if (!deserialize(config, scene)) {
		KERROR("Scene deserialization failed. See logs for details.");
		return KNULL;
	}

	scene->hf_terrain_asset_name = string_format("%k_terrain", scene->scene_asset_name);

	// FIXME: use async instead
	kasset_hf_terrain* terrain_asset = asset_system_request_hf_terrain_sync(engine_systems_get()->asset_state, scene->hf_terrain_asset_name);
	if (!terrain_asset) {
		scene->hf = hf_terrain_generate(2, 2);
	} else {
		scene->hf = hf_terrain_create_from_asset(terrain_asset);
		asset_system_release_hf_terrain(engine_systems_get()->asset_state, terrain_asset);
	}

	return scene;
}

#define CLEANUP_ENTITY_TYPE(type)                                              \
	if (scene->type##s) {                                                      \
		u32 count = darray_length(scene->type##s);                             \
		for (u32 i = 0; i < count; ++i) {                                      \
			type##_entity_destroy(scene, &scene->type##s[i], KENTITY_INVALID); \
		}                                                                      \
		darray_destroy(scene->type##s);                                        \
		scene->type##s = KNULL;                                                \
	}

static void cleanup_map(kmaterial_to_geometry_map* map) {
	if (map->capacity && map->lists) {
		for (u32 i = 0; i < map->capacity; ++i) {
			kmaterial_geometry_list* list = &map->lists[i];
			if (list->capacity && list->geometries) {
				KFREE_TYPE_CARRAY(list->geometries, kgeometry_ref, list->capacity);
			}
		}
		KFREE_TYPE_CARRAY(map->lists, kmaterial_geometry_list, map->capacity);
		map->lists = KNULL;
		map->count = 0;
		map->capacity = 0;
	}
}

void kscene_destroy(struct kscene* scene) {
	if (!scene) {
		return;
	}

	scene->state = KSCENE_STATE_UNINITIALIZED;

	// Let any work the renderer is doing finish first.
	renderer_wait_for_idle();

	if (scene->description) {
		string_free(scene->description);
		scene->description = KNULL;
	}
	if (scene->name) {
		string_free(scene->name);
		scene->name = KNULL;
	}

	u64_bst_cleanup(scene->name_lookup);
	scene->name_lookup = KNULL;

	scene->shadow_bias = 0;
	scene->shadow_fade_dist = 0;
	scene->shadow_split_mult = 0;
	scene->shadow_bias = 0;
	scene->flags = 0;
	scene->queued_initial_asset_loads = 0;

	if (scene->directional_light != KLIGHT_INVALID) {
		light_destroy(engine_systems_get()->light_system, scene->directional_light);
		scene->directional_light = KLIGHT_INVALID;
	}

	skybox_unload(&scene->sb);
	skybox_destroy(&scene->sb);
	scene->skybox_asset_name = INVALID_KNAME;
	scene->skybox_asset_package_name = INVALID_KNAME;

	texture_release(scene->default_irradiance_texture);
	scene->default_irradiance_texture = INVALID_KTEXTURE;

	bvh_destroy(&scene->bvh_tree);
	ktransform_destroy(&scene->bvh_transform);

	CLEANUP_ENTITY_TYPE(base);

	CLEANUP_ENTITY_TYPE(water_plane);
	CLEANUP_ENTITY_TYPE(model);
	darray_destroy(scene->model_geometry_datas);
	scene->model_geometry_datas = KNULL;
	darray_destroy(scene->model_geometry_extents);
	scene->model_geometry_extents = KNULL;
	cleanup_map(&scene->opaque_static_model_material_map);
	cleanup_map(&scene->transparent_static_model_material_map);
	cleanup_map(&scene->opaque_animated_model_material_map);
	cleanup_map(&scene->transparent_static_model_material_map);

	CLEANUP_ENTITY_TYPE(point_light);
	CLEANUP_ENTITY_TYPE(spawn_point);
	CLEANUP_ENTITY_TYPE(volume);
	CLEANUP_ENTITY_TYPE(hit_shape);
	CLEANUP_ENTITY_TYPE(audio_emitter);

	// TODO: heightmap terrain entities
	/* CLEANUP_ENTITY_TYPE(heightmap_terrain); */

	if (scene->col_shape_states) {
		darray_destroy(scene->col_shape_states);
		scene->col_shape_states = KNULL;
	}

	if (scene->root_entities) {
		darray_destroy(scene->root_entities);
		scene->root_entities = KNULL;
	}

	scene->loaded_callback = KNULL;
	scene->load_context = KNULL;
	scene->name = INVALID_KNAME;

#if KOHI_DEBUG
	if (scene->debug_datas) {
		u32 count = darray_length(scene->debug_datas);
		for (u32 i = 0; i < count; ++i) {
			renderer_geometry_destroy(&scene->debug_datas[i].geometry);
			geometry_destroy(&scene->debug_datas[i].geometry);
			// NOTE: Don't destroy the transform here since it is also the transform of its owner.
			/* ktransform_destroy(&scene->debug_datas[i].transform); */
		}
		darray_destroy(scene->debug_datas);
		scene->debug_datas = KNULL;
	}

	debug_grid_destroy(&scene->grid);

	if (scene->bvh_debug_pool && scene->bvh_debug_pool_size) {
		// Cleanup debug BVH data.
		KFREE_TYPE_CARRAY(scene->bvh_debug_pool, scene_bvh_debug_data, scene->bvh_debug_pool_size);
		KFREE_TYPE_CARRAY(scene->bvh_debug_vertex_pool, position_vertex_3d, 24 * scene->bvh_debug_pool_size);
	}
	scene->bvh_debug_pool = KNULL;
	scene->bvh_debug_vertex_pool = KNULL;
	scene->bvh_debug_pool_size = 0;

	// HACK: move this to a proper location.
	hf_terrain_destroy(&scene->hf);

	string_free(scene->hf_terrain_asset_name);

#endif

	KFREE_TYPE(scene, kscene, MEMORY_TAG_SCENE);
}

static void recalculate_transforms(kscene* scene, base_entity* parent, kentity child_handle) {
	KASSERT(child_handle != KENTITY_INVALID);
	base_entity* child = get_entity_base(scene, child_handle);
	mat4 bvh_extents_transform = ktransform_world_get(child->transform); // child_world;

	aabb box = aabb_from_mat4_extents(child->extents.min, child->extents.max, bvh_extents_transform);

	bvh_update(&scene->bvh_tree, child->bvh_id, box);

	u16 count = child->children ? darray_length(child->children) : 0;
	for (u16 i = 0; i < count; ++i) {
		recalculate_transforms(scene, child, child->children[i]);
	}
}

#if KOHI_DEBUG
// Recalculate transforms for debug datas.
static void recalculate_debug_transforms(kscene* scene) {
	// TODO: optimization - cache these and only change if the parent transform changes.
	u16 count = scene->debug_datas ? darray_length(scene->debug_datas) : 0;
	for (u16 i = 0; i < count; ++i) {
		kscene_debug_data* data = &scene->debug_datas[i];

		if (data->owner != KENTITY_INVALID) {
			base_entity* owner_base = get_entity_base(scene, data->owner);
			if (data->ignore_scale) {
				// If ignoring scale (think point lights, audio emitters, etc.) then a new matrix
				// must be composed containing only position and rotation updates.
				quat world_rot = ktransform_world_rotation_get(owner_base->transform);
				vec3 world_pos = ktransform_world_position_get(owner_base->transform);
				data->model = mat4_from_translation_rotation_scale(world_pos, world_rot, vec3_one());
			} else {
				// If no adjustments are needed, just use the parent transform's world matrix as this
				// debug data's world matrix.
				data->model = ktransform_world_get(owner_base->transform);
			}
		} else {
			// If there's no parent, just use the local matrix as the world matrix.
			data->model = mat4_identity();
		}
	}
}
#endif

b8 collision_shapes_intersect(const kcollision_shape* a, ktransform ta, const kcollision_shape* b, ktransform tb) {
	switch (a->shape_type) {
	case KSHAPE_TYPE_SPHERE: {
		vec3 pos_a = ktransform_world_position_get(ta);
		ksphere ks_a = {.radius = a->radius, .position = pos_a};

		switch (b->shape_type) {
		case KSHAPE_TYPE_SPHERE: {
			vec3 pos_b = ktransform_world_position_get(tb);
			ksphere ks_b = {.radius = b->radius, .position = pos_b};
			return sphere_intersects_sphere(ks_a, ks_b);
		} break;
		case KSHAPE_TYPE_RECTANGLE: {
			mat4 mat_b = ktransform_world_get(tb);
			vec3 ext_b = b->extents;
			extents_3d extents_b = {
				.min = (vec3){kabs(ext_b.x) * -0.5f, kabs(ext_b.y) * -0.5f, kabs(ext_b.z) * -0.5f},
				.max = (vec3){kabs(ext_b.x) * 0.5f, kabs(ext_b.y) * 0.5f, kabs(ext_b.z) * 0.5f},
			};
			obb obb_b = aabb_to_obb(extents_b, mat_b);
			return obb_intersects_sphere(&obb_b, &ks_a);
		} break;
		}
	} break;
	case KSHAPE_TYPE_RECTANGLE: {
		mat4 mat_a = ktransform_world_get(ta);
		vec3 ext_a = a->extents;
		extents_3d extents_a = {
			.min = (vec3){kabs(ext_a.x) * -0.5f, kabs(ext_a.y) * -0.5f, kabs(ext_a.z) * -0.5f},
			.max = (vec3){kabs(ext_a.x) * 0.5f, kabs(ext_a.y) * 0.5f, kabs(ext_a.z) * 0.5f},
		};
		obb obb_a = aabb_to_obb(extents_a, mat_a);

		switch (b->shape_type) {
		case KSHAPE_TYPE_SPHERE: {
			vec3 pos_b = ktransform_world_position_get(tb);
			ksphere ks_b = {.radius = b->radius, .position = pos_b};
			return obb_intersects_sphere(&obb_a, &ks_b);
		} break;
		case KSHAPE_TYPE_RECTANGLE: {
			mat4 mat_b = ktransform_world_get(tb);
			vec3 ext_b = b->extents;
			extents_3d extents_b = {
				.min = (vec3){kabs(ext_b.x) * -0.5f, kabs(ext_b.y) * -0.5f, kabs(ext_b.z) * -0.5f},
				.max = (vec3){kabs(ext_b.x) * 0.5f, kabs(ext_b.y) * 0.5f, kabs(ext_b.z) * 0.5f},
			};
			obb obb_b = aabb_to_obb(extents_b, mat_b);
			return obb_intersects_obb(&obb_a, &obb_b, 0);
		} break;
		}
	} break;
	}
}

// returns INVALID_ID_U32 if not found.
u32 shape_state_indexof(const kscene* scene, kentity a, kentity b) {
	u32 len = darray_length(scene->col_shape_states);
	for (u32 i = 0; i < len; ++i) {
		collision_shape_state* s = &scene->col_shape_states[i];
		if ((a == s->a && b == s->b) || (b == s->a && a == s->b)) {
			return i;
		}
	}

	return INVALID_ID_U32;
}

void shape_state_create(kscene* scene, kentity a, kentity b) {
	if (shape_state_indexof(scene, a, b) == INVALID_ID_U32) {
		u32 len = darray_length(scene->col_shape_states);
		for (u32 i = 0; i < len; ++i) {
			collision_shape_state* s = &scene->col_shape_states[i];
			if (s->a == KENTITY_INVALID && s->b == KENTITY_INVALID) {
				// Free entry, use it.
				s->a = a;
				s->b = b;
				return;
			}
		}

		collision_shape_state new_state = {.a = a, .b = b};
		darray_push(scene->col_shape_states, new_state);
	}
}

void shape_state_remove(kscene* scene, kentity a, kentity b) {
	u32 index = shape_state_indexof(scene, a, b);
	if (index != INVALID_ID_U32) {
		scene->col_shape_states[index].a = KENTITY_INVALID;
		scene->col_shape_states[index].b = KENTITY_INVALID;
	}
}

void kscene_on_window_resize(struct kscene* scene, const struct kwindow* window) {
	if (!window->width || !window->height || !scene) {
		return;
	}

	// Resize cameras.
	rect_2di world_vp_rect = {0, 0, window->width, window->height};
	// Set the vp_rect on all relevant cameras based on the new window size.
	kcamera_set_vp_rect(scene->world_inv_camera, world_vp_rect);
}

b8 kscene_update(struct kscene* scene, struct frame_data* p_frame_data) {
	if (scene) {
		// If parsing is complete, then check if the state can be flipped to loaded.
		if (scene->state == KSCENE_STATE_LOADING && scene->queued_initial_asset_loads < 1) {
			scene->queued_initial_asset_loads = 0;
			KINFO("All initial entity asset loads are complete. Scene is now loaded.");
			scene->state = KSCENE_STATE_PRE_LOADED;
			return true;
		}

		if (scene->state == KSCENE_STATE_PRE_LOADED) {
			if (scene->loaded_callback) {
				scene->loaded_callback(scene, scene->load_context);
			}
			scene->state = KSCENE_STATE_LOADED;
		}

		if (scene->state == KSCENE_STATE_LOADED) {

			// Update all transforms from the top (roots) down.
			u16 root_count = darray_length(scene->root_entities);
			for (u16 i = 0; i < root_count; ++i) {
				recalculate_transforms(scene, 0, scene->root_entities[i]);
			}

#if KOHI_DEBUG
			recalculate_debug_transforms(scene);
#endif

			// Sync audio emitter positions.
			u16 audio_emitter_count = darray_length(scene->audio_emitters);
			for (u16 i = 0; i < audio_emitter_count; ++i) {
				audio_emitter_entity* audio_entity = &scene->audio_emitters[i];
				mat4 world = ktransform_world_get(audio_entity->base.transform);
				// Get world position for the audio emitter based on it's owning node's ktransform.
				vec3 emitter_world_pos = mat4_position(world);
				kaudio_emitter_world_position_set(engine_systems_get()->audio_system, audio_entity->emitter, emitter_world_pos);
			}

			// Sync point light positions and other data.
			u16 point_light_count = darray_length(scene->point_lights);
			for (u16 i = 0; i < point_light_count; ++i) {
				point_light_entity* light_entity = &scene->point_lights[i];

				vec3 pos = ktransform_world_position_get(light_entity->base.transform);

				point_light_set_position(engine_systems_get()->light_system, light_entity->handle, pos);
				// TODO: sync other properties (colour, etc.)
			}

			// Check all hit shapes against all volumes.
			// TODO: optimization: use the BVH to check these if the number of them gets high.
			u16 hit_shape_count = darray_length(scene->hit_shapes);
			for (u16 i = 0; i < hit_shape_count; ++i) {
				hit_shape_entity* hit_entity = &scene->hit_shapes[i];
				kentity a = kentity_pack(hit_entity->base.type, (u16)i, 0, 0);

				u16 vol_count = darray_length(scene->volumes);
				for (u16 v = 0; v < vol_count; ++v) {
					volume_entity* vol = &scene->volumes[v];

					b8 has_collision = collision_shapes_intersect(
						&hit_entity->shape, hit_entity->base.transform,
						&vol->shape, vol->base.transform);

					kentity b = kentity_pack(vol->base.type, (u16)v, 0, 0);
					u32 index = shape_state_indexof(scene, a, b);
					if (has_collision) {
						if (index == INVALID_ID_U32) {
							// new collision
							shape_state_create(scene, a, b);
							KDEBUG("on enter");
							if (vol->on_enter_command) {
								console_command_execute(vol->on_enter_command);
							}
						} else {
							// existing
							if (vol->on_tick_command) {
								console_command_execute(vol->on_tick_command);
							}
						}
					} else {
						// Existing, no longer colliding.
						if (index != INVALID_ID_U32) {
							shape_state_remove(scene, a, b);
							KDEBUG("on leave");
							if (vol->on_leave_command) {
								console_command_execute(vol->on_leave_command);
							}
						}
					}
				}
			}

			// Update BVH debug geometry
#if KOHI_DEBUG

			// Recalculate boxes for every BVH node
			for (u16 i = 0; i < scene->bvh_tree.count; ++i) {
				bvh_node* n = &scene->bvh_tree.nodes[i];
				if (n->height >= 0) {
					scene_bvh_debug_data* dd = &scene->bvh_debug_pool[i];
					geometry_recalculate_line_box3d_by_extents(&dd->geo, n->aabb, dd->geo.center);
					dd->model = mat4_identity();
				}
			}
#endif
		} // end loaded
	}
	return true;
}

b8 kscene_frame_prepare(struct kscene* scene, struct frame_data* p_frame_data, u32 render_mode, kcamera current_camera) {
	if (scene && scene->state == KSCENE_STATE_LOADED) {

		frame_allocator_int* frame_allocator = &p_frame_data->allocator;
		kforward_renderer_render_data* render_data = p_frame_data->render_data;

		if (FLAG_GET(scene->flags, KSCENE_FLAG_RENDER_FOG_BIT)) {
			render_data->forward_data.fog_colour = scene->fog_colour;
			render_data->forward_data.fog_near = scene->fog_near;
			render_data->forward_data.fog_far = scene->fog_far;
		} else {
			render_data->forward_data.fog_colour = vec4_zero();
			render_data->forward_data.fog_near = 0.0f;
			render_data->forward_data.fog_far = 99999999.9f;
		}

		// "Global" items used by multiple passes.
		mat4 view = kcamera_get_view(current_camera);
		mat4 projection = kcamera_get_projection(current_camera);
		vec3 view_position = kcamera_get_position(current_camera);
		vec3 view_euler = kcamera_get_euler_rotation(current_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(current_camera);
		f32 fov = kcamera_get_fov(current_camera);

		f32 near_clip = kcamera_get_near_clip(current_camera);
		f32 far_clip = kcamera_get_far_clip(current_camera);
		// Ensure the shadow map isn't including things the camera can't see.
		f32 shadow_far = KMIN(scene->shadow_dist + scene->shadow_fade_dist, far_clip);
		f32 clip_range = shadow_far - near_clip;

		f32 min_z = near_clip;
		f32 max_z = near_clip + clip_range;
		f32 range = max_z - min_z;
		f32 ratio = max_z / min_z;
		// Calculate cascade splits based on view camera frustum.
		vec4 splits;
		for (u32 c = 0; c < KMATERIAL_MAX_SHADOW_CASCADES; c++) {
			f32 p = (c + 1) / (f32)KMATERIAL_MAX_SHADOW_CASCADES;
			f32 log = min_z * kpow(ratio, p);
			f32 uniform = min_z + range * p;
			f32 d = scene->shadow_split_mult * (log - uniform) + uniform;
			splits.elements[c] = (d - near_clip) / clip_range;
		}
		// Default values to use in the event there is no directional light.
		// These are required because the scene pass needs them.
		mat4 shadow_camera_view_projections[KMATERIAL_MAX_SHADOW_CASCADES];
		for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
			shadow_camera_view_projections[i] = mat4_identity();
		}

		kdirectional_light_data dir_light = (kdirectional_light_data){
			.light = scene->directional_light,
			.direction = directional_light_get_direction(engine_systems_get()->light_system, scene->directional_light)};

		// Shadow pass data
		{
			// Shadowmap pass - only runs if there is a directional light.
			// TODO: Will also need to run for point lights when implemented.
			render_data->shadow_data.do_pass = true;
			// TODO: this should be configurable.
			render_data->shadow_data.cascade_count = KMATERIAL_MAX_SHADOW_CASCADES;
			render_data->shadow_data.cascades = frame_allocator->allocate(sizeof(kshadow_pass_cascade_render_data) * render_data->shadow_data.cascade_count);

			f32 last_split_dist = 0.0f;

			// Obtain the light direction.
			vec3 light_dir = vec3_normalized(dir_light.direction);

			// Get the view-projection matrix
			mat4 shadow_dist_projection = mat4_perspective(fov, (f32)vp_rect.width / vp_rect.height, near_clip, shadow_far);
			mat4 cam_view_proj = mat4_transposed(mat4_mul(view, shadow_dist_projection));
			mat4 inv_view_proj = mat4_inverse(cam_view_proj);

			// Get the world-space corners of the view frustum.
			vec4 global_corners[8] = {
				{-1.0f, +1.0f, 0.0f, 1.0f},
				{+1.0f, +1.0f, 0.0f, 1.0f},
				{+1.0f, -1.0f, 0.0f, 1.0f},
				{-1.0f, -1.0f, 0.0f, 1.0f},

				{-1.0f, +1.0f, 1.0f, 1.0f},
				{+1.0f, +1.0f, 1.0f, 1.0f},
				{+1.0f, -1.0f, 1.0f, 1.0f},
				{-1.0f, -1.0f, 1.0f, 1.0f}};

			for (u32 j = 0; j < 8; ++j) {
				vec4 inv_corner = mat4_mul_vec4(inv_view_proj, global_corners[j]);
				global_corners[j] = (vec4_div_scalar(inv_corner, inv_corner.w));
			}

			// Pass over shadow map "camera" view and projection matrices (one per cascade).
			for (u32 c = 0; c < render_data->shadow_data.cascade_count; c++) {
				kshadow_pass_cascade_render_data* cascade = &render_data->shadow_data.cascades[c];

				vec4 corners[8];
				kcopy_memory(corners, global_corners, sizeof(corners));

				// Adjust the corners by pulling/pushing the near/far according to the current split.
				f32 split_dist = splits.elements[c];
				for (u32 i = 0; i < 4; ++i) {
					// far - near
					vec4 dist = vec4_sub(corners[i + 4], corners[i]);
					corners[i + 4] = vec4_add(corners[i], vec4_mul_scalar(dist, split_dist));
					corners[i] = vec4_add(corners[i], vec4_mul_scalar(dist, last_split_dist));
				}

				// Calculate the center of the camera's frustum by averaging the points.
				// This is also used as the lookat point for the shadow "camera".
				vec3 center = vec3_zero();
				for (u32 i = 0; i < 8; ++i) {
					center = vec3_add(center, vec3_from_vec4(corners[i]));
				}
				center = vec3_div_scalar(center, 8.0f); // size

				// Get the furthest-out point from the center and use that as the extents.
				f32 radius = 0.0f;
				for (u32 i = 0; i < 8; ++i) {
					f32 distance = vec3_distance(vec3_from_vec4(corners[i]), center);
					radius = KMAX(radius, distance);
				}
				radius = kceil(radius * 16.0f) / 16.0f;

				// Calculate the extents by using the radius from above.
				extents_3d extents;
				extents.max = vec3_create(radius, radius, radius);
				extents.min = vec3_mul_scalar(extents.max, -1.0f);

				// "Pull" the min inward and "push" the max outward on the z axis to make sure
				// shadow casters outside the view are captured as well (think trees above the player).
				// TODO: This should be adjustable/tuned per scene.
				f32 z_multiplier = 10.0f;
				if (extents.min.z < 0) {
					extents.min.z *= z_multiplier;
				} else {
					extents.min.z /= z_multiplier;
				}

				if (extents.max.z < 0) {
					extents.max.z /= z_multiplier;
				} else {
					extents.max.z *= z_multiplier;
				}

				// Generate lookat by moving along the opposite direction of the directional light by the
				// minimum extents. This is negated because the directional light points "down" and the camera
				// needs to be "up".
				vec3 shadow_camera_position = vec3_sub(center, vec3_mul_scalar(light_dir, -extents.min.z));
				mat4 light_view = mat4_look_at(shadow_camera_position, center, vec3_up());

				// Generate ortho projection based on extents.
				mat4 light_ortho = mat4_orthographic(extents.min.x, extents.max.x, extents.min.y, extents.max.y, 0.0f, extents.max.z - extents.min.z);

				// combined view/projection
				shadow_camera_view_projections[c] = (mat4_mul(light_view, light_ortho));

				cascade->view_projection = shadow_camera_view_projections[c];

				last_split_dist = split_dist;
			}

			render_data->shadow_data.hf_terrain_data = kscene_get_hf_terrain_render_data(
				scene,
				p_frame_data,
				KNULL, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_NONE);

			// Gather the geometries to be rendered.
			// Note that this only needs to happen once, since all geometries visible by the furthest-out cascase
			// must also be drawn on the nearest cascade to ensure objects outside the view cast shadows into the
			// view properly.
			//
			// Meshes with opaque materials first.
			u16 opaque_material_count = 0;
			kmaterial_render_data* opaque_material_render_data = kscene_get_static_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_NONE,
				&opaque_material_count);

			u16 animated_opaque_material_count = 0;
			kmaterial_render_data* animated_opaque_material_render_data = kscene_get_animated_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_NONE,
				&animated_opaque_material_count);

			u32 animated_count = darray_length(animated_opaque_material_render_data);
			for (u32 i = 0; i < animated_count; ++i) {
				darray_push(opaque_material_render_data, animated_opaque_material_render_data[i]);
				opaque_material_count++;
			}

			// Opaque-material geometries can be grouped together for the shadow pass.
			render_data->shadow_data.opaque_geometry_count = 0;
			render_data->shadow_data.opaque_geometries = darray_create_with_allocator(kgeometry_render_data, frame_allocator);
			for (u16 i = 0; i < opaque_material_count; ++i) {
				for (u16 j = 0; j < opaque_material_render_data[i].geometry_count; ++j) {
					darray_push(render_data->shadow_data.opaque_geometries, opaque_material_render_data[i].geometries[j]);
				}
			}
			render_data->shadow_data.opaque_geometry_count = darray_length(render_data->shadow_data.opaque_geometries);

			// Track the number of meshes drawn in the shadow pass.
			p_frame_data->drawn_shadow_mesh_count = render_data->shadow_data.opaque_geometry_count;

			// Meshes with transparent materials next. Can just use these as they come organized from the scene.
			render_data->shadow_data.transparent_geometries_by_material = kscene_get_static_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
				&render_data->shadow_data.transparent_geometries_by_material_count);
			// Get a count of all the geometries
			for (u16 i = 0; i < render_data->shadow_data.transparent_geometries_by_material_count; ++i) {
				p_frame_data->drawn_shadow_mesh_count += (u16)render_data->shadow_data.transparent_geometries_by_material[i].geometry_count;
			}

			// Gather animated geometries as well.
			// FIXME: animated and static model data should be combined into a single call from the scene since the
			// shaders are no longer separate. When this is done, this code won't be required.
			u16 animated_transparent_count = 0;
			kmaterial_render_data* animated_transparent_geometries_by_material = kscene_get_animated_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
				&animated_transparent_count);
			// Get a count of all the geometries
			for (u16 i = 0; i < animated_transparent_count; ++i) {
				p_frame_data->drawn_shadow_mesh_count += (u16)animated_transparent_geometries_by_material[i].geometry_count;
				darray_push(render_data->shadow_data.transparent_geometries_by_material, animated_transparent_geometries_by_material[i]);
			}

			// opaque and transparent animated geometries
			{
				// Meshes with opaque materials first.
				u16 animated_opaque_material_count = 0;
				kmaterial_render_data* animated_opaque_material_render_data = kscene_get_animated_model_render_data(
					scene,
					p_frame_data,
					0, // FIXME: frustum culling disabled for now.
					KSCENE_RENDER_DATA_FLAG_NONE,
					&animated_opaque_material_count);

				// Opaque-material geometries can be grouped together for the shadow pass.
				render_data->shadow_data.animated_opaque_geometry_count = 0;
				render_data->shadow_data.animated_opaque_geometries = darray_create_with_allocator(kgeometry_render_data, frame_allocator);
				for (u16 i = 0; i < animated_opaque_material_count; ++i) {
					for (u16 j = 0; j < animated_opaque_material_render_data[i].geometry_count; ++j) {
						darray_push(render_data->shadow_data.animated_opaque_geometries, animated_opaque_material_render_data[i].geometries[j]);
					}
				}
				render_data->shadow_data.animated_opaque_geometry_count = darray_length(render_data->shadow_data.animated_opaque_geometries);

				// Track the number of meshes drawn in the shadow pass.
				p_frame_data->drawn_shadow_mesh_count = render_data->shadow_data.animated_opaque_geometry_count;

				// Meshes with transparent materials next. Can just use these as they come organized from the scene.
				render_data->shadow_data.animated_transparent_geometries_by_material = kscene_get_static_model_render_data(
					scene,
					p_frame_data,
					0, // FIXME: frustum culling disabled for now.
					KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
					&render_data->shadow_data.animated_transparent_geometries_by_material_count);
				// Get a count of all the geometries
				for (u16 i = 0; i < render_data->shadow_data.animated_transparent_geometries_by_material_count; ++i) {
					p_frame_data->drawn_shadow_mesh_count += (u16)render_data->shadow_data.animated_transparent_geometries_by_material[i].geometry_count;
				}
			}

			// Gather terrain geometries.
			render_data->shadow_data.terrains = kscene_get_hm_terrain_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				0,
				&render_data->shadow_data.terrain_count);
			// Get terrain geometry count (i.e. number of chunks)
			for (u16 i = 0; i < render_data->shadow_data.terrain_count; ++i) {
				// TODO: Counter for terrain geometries.
				p_frame_data->drawn_shadow_mesh_count += render_data->shadow_data.terrains[i].chunk_count;
			}
		} // end shadow pass

		// Forward pass data
		{
			render_data->forward_data.do_pass = true;

			render_data->forward_data.projection = projection;
			render_data->forward_data.view_matrix = view;
			render_data->forward_data.view_position = vec4_from_vec3(view_position, 1.0);

			render_data->forward_data.render_mode = render_mode;
			render_data->forward_data.shadow_bias = scene->shadow_bias;
			// Ensure the shadow doesn't exceed the camera.
			render_data->forward_data.shadow_distance = shadow_far; // scene->shadow_dist;
			render_data->forward_data.shadow_fade_distance = scene->shadow_fade_dist;
			render_data->forward_data.shadow_split_mult = scene->shadow_split_mult;
			render_data->forward_data.near_clip = near_clip;
			render_data->forward_data.far_clip = far_clip;

			// SKYBOX
			render_data->forward_data.skybox = kscene_get_skybox_render_data(scene);
			render_data->forward_data.skybox.do_pass = FLAG_GET(scene->flags, KSCENE_FLAG_RENDER_SKYBOX_BIT);

			// Pass over shadow map "camera" view and projection matrices (one per cascade).
			for (u32 c = 0; c < render_data->shadow_data.cascade_count; c++) {
				render_data->forward_data.cascade_splits[c] = (near_clip + splits.elements[c] * clip_range) * 1.0f;
				render_data->forward_data.directional_light_spaces[c] = shadow_camera_view_projections[c];
			}

			for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
				render_data->forward_data.irradiance_cubemap_textures[i] = INVALID_KTEXTURE;
			}
			// HACK: use the skybox cubemap as the irradiance texture for now.
			ktexture sb_texture = render_data->forward_data.skybox.skybox_texture;
			render_data->forward_data.irradiance_cubemap_texture_count = 1;
			render_data->forward_data.irradiance_cubemap_textures[0] = sb_texture != INVALID_KTEXTURE ? sb_texture : scene->default_irradiance_texture;

			// Lighting
			render_data->forward_data.dir_light = dir_light;

			// Get a list of geometries from the "standard" camera perspective.
			// These get reused for the water planes' refraction passes.
			render_data->forward_data.standard_pass.view_position = view_position;
			render_data->forward_data.standard_pass.view_matrix = render_data->forward_data.view_matrix;

			// Meshes with opaque materials first.
			render_data->forward_data.standard_pass.opaque_meshes_by_material = kscene_get_static_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_NONE,
				&render_data->forward_data.standard_pass.opaque_meshes_by_material_count);

			// Get geometry count.
			for (u16 i = 0; i < render_data->forward_data.standard_pass.opaque_meshes_by_material_count; ++i) {
				p_frame_data->drawn_mesh_count += render_data->forward_data.standard_pass.opaque_meshes_by_material[i].geometry_count;
			}

			// Animated meshes with opaque materials.
			render_data->forward_data.standard_pass.animated_opaque_meshes_by_material = kscene_get_animated_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_NONE,
				&render_data->forward_data.standard_pass.animated_opaque_meshes_by_material_count);

			// Get geometry count.
			for (u16 i = 0; i < render_data->forward_data.standard_pass.animated_opaque_meshes_by_material_count; ++i) {
				p_frame_data->drawn_mesh_count += render_data->forward_data.standard_pass.animated_opaque_meshes_by_material[i].geometry_count;
			}

			// Meshes with transparent materials next. Can just use these as they come organized from the scene.
			render_data->forward_data.standard_pass.transparent_meshes_by_material = kscene_get_static_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
				&render_data->forward_data.standard_pass.transparent_meshes_by_material_count);
			// Get a count of all the geometries
			for (u16 i = 0; i < render_data->forward_data.standard_pass.transparent_meshes_by_material_count; ++i) {
				p_frame_data->drawn_mesh_count += render_data->forward_data.standard_pass.transparent_meshes_by_material[i].geometry_count;
			}

			// Animated meshes with transparent materials next. Can just use these as they come organized from the scene.
			render_data->forward_data.standard_pass.animated_transparent_meshes_by_material = kscene_get_animated_model_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
				&render_data->forward_data.standard_pass.animated_transparent_meshes_by_material_count);
			// Get a count of all the geometries
			for (u16 i = 0; i < render_data->forward_data.standard_pass.animated_transparent_meshes_by_material_count; ++i) {
				p_frame_data->drawn_mesh_count += render_data->forward_data.standard_pass.animated_transparent_meshes_by_material[i].geometry_count;
			}

			// Gather terrain geometries.
			render_data->forward_data.standard_pass.terrains = kscene_get_hm_terrain_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				0,
				&render_data->forward_data.standard_pass.terrain_count);

			// Get terrain geometry count (i.e. number of chunks)
			for (u16 i = 0; i < render_data->forward_data.standard_pass.terrain_count; ++i) {
				// TODO: Counter for terrain geometries.
				p_frame_data->drawn_mesh_count += render_data->forward_data.standard_pass.terrains[i].chunk_count;
			}

			// Heightfield Terrain
			render_data->forward_data.standard_pass.hf_terrain_data = kscene_get_hf_terrain_render_data(
				scene,
				p_frame_data,
				KNULL, // FIXME: frustum culling disabled for now
				KSCENE_RENDER_DATA_FLAG_NONE);

			// Obtain the water plane render datas and setup pass data for each.
			kwater_plane_render_data* water_planes = kscene_get_water_plane_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				0,
				&render_data->forward_data.water_plane_count);

			if (render_data->forward_data.water_plane_count) {
				render_data->forward_data.water_planes = frame_allocator->allocate(sizeof(kforward_pass_water_plane_render_data) * render_data->forward_data.water_plane_count);
				for (u32 w = 0; w < render_data->forward_data.water_plane_count; ++w) {
					kforward_pass_water_plane_render_data* wp_data = &render_data->forward_data.water_planes[w];

					// Take a copy of the water plane's render data.
					wp_data->plane_render_data = water_planes[w];

					// refraction pass data
					{
						// NOTE: The refraction pass can literally just use the same data as the standard pass. No need to re-query for it.
						wp_data->refraction_pass.view_position = render_data->forward_data.standard_pass.view_position;
						wp_data->refraction_pass.view_matrix = render_data->forward_data.standard_pass.view_matrix;

						wp_data->refraction_pass.transparent_meshes_by_material_count = render_data->forward_data.standard_pass.transparent_meshes_by_material_count;
						wp_data->refraction_pass.transparent_meshes_by_material = render_data->forward_data.standard_pass.transparent_meshes_by_material;
						wp_data->refraction_pass.opaque_meshes_by_material_count = render_data->forward_data.standard_pass.opaque_meshes_by_material_count;
						wp_data->refraction_pass.opaque_meshes_by_material = render_data->forward_data.standard_pass.opaque_meshes_by_material;

						wp_data->refraction_pass.animated_transparent_meshes_by_material_count = render_data->forward_data.standard_pass.animated_transparent_meshes_by_material_count;
						wp_data->refraction_pass.animated_transparent_meshes_by_material = render_data->forward_data.standard_pass.animated_transparent_meshes_by_material;
						wp_data->refraction_pass.animated_opaque_meshes_by_material_count = render_data->forward_data.standard_pass.animated_opaque_meshes_by_material_count;
						wp_data->refraction_pass.animated_opaque_meshes_by_material = render_data->forward_data.standard_pass.animated_opaque_meshes_by_material;

						// Heightmap terrain.
						wp_data->refraction_pass.terrain_count = render_data->forward_data.standard_pass.terrain_count;
						wp_data->refraction_pass.terrains = render_data->forward_data.standard_pass.terrains;

						// Heightfield terrain
						wp_data->refraction_pass.hf_terrain_data = render_data->forward_data.standard_pass.hf_terrain_data;
					}

					// reflection pass data
					{
						// Use the inverted camera for the reflection render.
						// Invert position across plane.
						f32 double_distance = 2.0f * (view_position.y - 0); // TODO: water plane position, distance along plane normal.
						vec3 inv_cam_pos = view_position;
						inv_cam_pos.y -= double_distance; // TODO: invert along water plane normal axis.

						kcamera_set_position(scene->world_inv_camera, inv_cam_pos);
						vec3 inv_cam_rot = view_euler;
						inv_cam_rot.x *= -1.0f; // Invert the pitch.
						kcamera_set_euler_rotation_radians(scene->world_inv_camera, inv_cam_rot);

						wp_data->reflection_pass.view_position = inv_cam_pos;
						wp_data->reflection_pass.view_matrix = kcamera_get_view(scene->world_inv_camera);

						// Get a list of opaque geometries from the "reflection" camera perspective.
						wp_data->reflection_pass.opaque_meshes_by_material = kscene_get_static_model_render_data(
							scene,
							p_frame_data,
							0, // FIXME: frustum culling disabled for now.
							KSCENE_RENDER_DATA_FLAG_NONE,
							&wp_data->reflection_pass.opaque_meshes_by_material_count);

						// Get a list of animated opaque geometries from the "reflection" camera perspective.
						wp_data->reflection_pass.animated_opaque_meshes_by_material = kscene_get_animated_model_render_data(
							scene,
							p_frame_data,
							0, // FIXME: frustum culling disabled for now.
							KSCENE_RENDER_DATA_FLAG_NONE,
							&wp_data->reflection_pass.animated_opaque_meshes_by_material_count);

						// Get a list of transparent geometries from the "reflection" camera perspective.
						wp_data->reflection_pass.transparent_meshes_by_material = kscene_get_static_model_render_data(
							scene,
							p_frame_data,
							0, // FIXME: frustum culling disabled for now.
							KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
							&wp_data->reflection_pass.transparent_meshes_by_material_count);

						// Get a list of animated transparent geometries from the "reflection" camera perspective.
						wp_data->reflection_pass.animated_transparent_meshes_by_material = kscene_get_animated_model_render_data(
							scene,
							p_frame_data,
							0, // FIXME: frustum culling disabled for now.
							KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT,
							&wp_data->reflection_pass.animated_transparent_meshes_by_material_count);

						// Get terrains/chunk data
						wp_data->reflection_pass.terrains = kscene_get_hm_terrain_render_data(
							scene,
							p_frame_data,
							0, // FIXME: frustum culling disabled for now.
							0,
							&wp_data->reflection_pass.terrain_count);

						// Heightfield Terrain
						wp_data->reflection_pass.hf_terrain_data = kscene_get_hf_terrain_render_data(
							scene,
							p_frame_data,
							KNULL, // FIXME: frustum culling disabled for now
							KSCENE_RENDER_DATA_FLAG_NONE);
					}
				}
			} // end water planes
		} // end forward pass

#if KOHI_DEBUG

		// World debug pass (debug only)
		{
			render_data->world_debug_data.do_pass = true;

			render_data->world_debug_data.projection = projection;
			render_data->world_debug_data.view = view;

			// Get world debug geometries.
			render_data->world_debug_data.geometries = kscene_get_debug_render_data(
				scene,
				p_frame_data,
				0, // FIXME: frustum culling disabled for now.
				0,
				&render_data->world_debug_data.geometry_count);

			if (!render_data->world_debug_data.geometry_count) {
				render_data->world_debug_data.geometries = darray_create_with_allocator(kdebug_geometry_render_data, &p_frame_data->allocator);
			}

			// Add grid geometry.
			kgeometry* gg = &scene->grid.geometry;
			render_data->world_debug_data.draw_grid = FLAG_GET(scene->flags, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT);
			render_data->world_debug_data.grid_geometry = (kdebug_geometry_render_data){
				.geo = {
					.animation_id = INVALID_ID_U16,
					.transform = 0,
					.bound_point_light_count = 0,
					.vertex_count = gg->vertex_count,
					.vertex_offset = gg->vertex_buffer_offset,
					.index_count = gg->index_count,
					.index_offset = gg->index_buffer_offset,
				}};

			/*
			// Additional debug geometries inserted via game logic
			// TODO: Move this to the scene.

			u32 line_count = darray_length(state->test_lines);
			for (u32 i = 0; i < line_count; ++i) {
				debug_line3d* line = &state->test_lines[i];
				debug_line3d_render_frame_prepare(line, p_frame_data);
				geometry_render_data rd = {0};
				rd.model = ktransform_world_get(line->ktransform);
				kgeometry* g = &line->geometry;
				rd.vertex_count = g->vertex_count;
				rd.vertex_buffer_offset = g->vertex_buffer_offset;
				rd.vertex_element_size = g->vertex_element_size;
				rd.index_count = g->index_count;
				rd.index_buffer_offset = g->index_buffer_offset;
				rd.index_element_size = g->index_element_size;
				rd.unique_id = INVALID_ID_U16;
				darray_push(debug_geometries, rd);
				debug_geometry_count++;
			}
			u32 box_count = darray_length(state->test_boxes);
			for (u32 i = 0; i < box_count; ++i) {
				debug_box3d* box = &state->test_boxes[i];
				debug_box3d_render_frame_prepare(box, p_frame_data);
				geometry_render_data rd = {0};
				rd.model = ktransform_world_get(box->ktransform);
				kgeometry* g = &box->geometry;
				rd.vertex_count = g->vertex_count;
				rd.vertex_buffer_offset = g->vertex_buffer_offset;
				rd.vertex_element_size = g->vertex_element_size;
				rd.index_count = g->index_count;
				rd.index_buffer_offset = g->index_buffer_offset;
				rd.index_element_size = g->index_element_size;
				rd.unique_id = INVALID_ID_U16;
				darray_push(debug_geometries, rd);
				debug_geometry_count++;
			} */
		}

		// Update BVH debug line data.
		struct renderer_system_state* renderer = engine_systems_get()->renderer_system;
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
		// Re-upload all the geometry in one shot.
		u64 offset = scene->bvh_debug_pool[0].geo.vertex_buffer_offset;
		// Vertex size * number of verts per box * number of boxes
		u64 total_size = sizeof(position_vertex_3d) * 24 * scene->bvh_debug_pool_size;
		if (!renderer_renderbuffer_load_range(renderer, vertex_buffer, offset, total_size, scene->bvh_debug_vertex_pool, true)) {
			KERROR("Failed to update scene BVH debug data.");
		}
#endif
	}
	return true;
}

kscene_state kscene_state_get(const struct kscene* scene) {
	KASSERT_DEBUG(scene);
	return scene->state;
}

kscene_flags kscene_get_flags(const struct kscene* scene) {
	return scene->flags;
}
b8 kscene_get_flag(const struct kscene* scene, kscene_flag_bits flag) {
	return FLAG_GET(scene->flags, (u32)flag);
}
void kscene_set_flags(struct kscene* scene, kscene_flags flags) {
	scene->flags = flags;
}
void kscene_set_flag(struct kscene* scene, kscene_flags flag, b8 enabled) {
	FLAG_SET(scene->flags, flag, enabled);
}

const char* kscene_get_name(const struct kscene* scene) {
	return scene->name;
}
void kscene_set_name(struct kscene* scene, const char* name) {
	if (scene->name) {
		string_free(scene->name);
	}
	scene->name = string_duplicate(name);
}

colour4 kscene_get_fog_colour(const struct kscene* scene) {
	return scene->fog_colour;
}
void kscene_set_fog_colour(struct kscene* scene, colour4 colour) {
	scene->fog_colour = colour;
}
f32 kscene_get_fog_near(const struct kscene* scene) {
	return scene->fog_near;
}
void kscene_set_fog_near(struct kscene* scene, f32 near) {
	scene->fog_near = near;
}
f32 kscene_get_fog_far(const struct kscene* scene) {
	return scene->fog_far;
}
void kscene_set_fog_far(struct kscene* scene, f32 far) {
	scene->fog_far = far;
}

void kscene_get_shadow_properties(
	struct kscene* scene,
	kcamera current_camera,
	f32* out_shadow_dist,
	f32* out_shadow_fade_distance,
	f32* out_shadow_split_mult,
	f32* out_shadow_bias) {

	f32 far_clip = kcamera_get_far_clip(current_camera);
	// Ensure the shadow map isn't including things the camera can't see.
	*out_shadow_dist = KMIN(scene->shadow_dist + scene->shadow_fade_dist, far_clip);
	*out_shadow_dist = scene->shadow_dist;
	*out_shadow_fade_distance = scene->shadow_fade_dist;
	*out_shadow_split_mult = scene->shadow_split_mult;
	*out_shadow_bias = scene->shadow_bias;
}

static b8 raycast_hits_sphere(const char* type_str, ktransform transform, f32 radius, const ray* r, raycast_hit* out_hit) {

	vec3 pos = ktransform_world_position_get(transform);

	vec3 point;
	f32 dist;
	/* KDEBUG("Ray hits sphere test. radius=%f", radius); */
	if (ray_intersects_sphere(r, pos, radius, &point, &dist)) {
		if (out_hit) {
			out_hit->type = RAYCAST_HIT_TYPE_SURFACE;
			out_hit->distance = dist;
			out_hit->position = point;
			out_hit->normal = vec3_normalized(vec3_sub(point, pos));
		}

		/* KDEBUG("More specific %s hit info acquired. Using it.", type_str); */
		return true;
	} else {
		// If it doesn't hit, disqualify it.
		/* KDEBUG("Hit the BVH node, but not the contained %s sphere. Hit does not count.", type_str); */
		return false;
	}
}

static b8 on_raycast_hit(bvh_userdata user, bvh_id id, const ray* r, f32 min, f32 max, f32 dist, vec3 pos, void* context, raycast_hit* out_hit) {
	kscene* scene = (kscene*)context;

	kentity entity = (kentity)user;
	base_entity* base = get_entity_base(scene, entity);
	kentity_type type = base->type;

	const char* name = kname_string_get(base->name);

	mat4 world = ktransform_world_get(base->transform);
	u16 type_index = kentity_unpack_type_index(entity);

	// Does it count as a hit?
	switch (type) {
	case KENTITY_TYPE_MODEL: {
		model_entity* typed = &scene->models[type_index];

		// Within the model, check to see if the raycast hits it as well.
		if (kmodel_ray_intersects(engine_systems_get()->model_system, typed->model, r, world, out_hit)) {
			/* KDEBUG("More specific model hit info acquired (name='%s'). Using it.", name); */
			return true;
		} else {
			// If it doesn't hit, disqualify it.
			/* KDEBUG("Hit the BVH node (name='%s'), but not the contained mesh. Hit does not count.", name); */
			return false;
		}
	} break;
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
		/* KINFO("Hit a heightmap terrain entity named '%s'", name); */

		return false;
	case KENTITY_TYPE_WATER_PLANE: {
		/* KINFO("Hit a water plane entity named '%s'", name); */

		mat4 world_inv = mat4_inverse(world);

		water_plane_entity* typed_entity = &scene->water_planes[type_index];
		kgeometry* g = &typed_entity->geo;

		triangle picked;
		vec3 pos;
		vec3 normal;

		// Transform ray by inverted world transform
		ray rt = ray_transformed(r, world_inv);

		if (ray_pick_triangle(&rt, true, g->vertex_count, g->vertex_element_size, g->vertices, g->index_count, g->indices, &picked, &pos, &normal)) {
			if (out_hit) {
				out_hit->type = RAYCAST_HIT_TYPE_SURFACE;
				// Transform position.
				pos = vec3_transform(pos, 1.0f, world);
				// Transform normal too.
				normal = vec3_transform(normal, 0.0f, world);

				out_hit->distance = vec3_distance(r->origin, pos);
				out_hit->position = pos;
				out_hit->normal = normal;
			}

			/* KDEBUG("More specific water plane hit info acquired. Using it."); */
			return true;
		}
	} break;
	case KENTITY_TYPE_AUDIO_EMITTER: {
		audio_emitter_entity* typed_entity = &scene->audio_emitters[type_index];

		return raycast_hits_sphere("audio emitter", base->transform, typed_entity->outer_radius, r, out_hit);
	} break;
	case KENTITY_TYPE_VOLUME: {
		volume_entity* typed_entity = &scene->volumes[type_index];

		if (typed_entity->shape.shape_type == KSHAPE_TYPE_SPHERE) {
			return raycast_hits_sphere("volume", base->transform, typed_entity->shape.radius, r, out_hit);
		} else if (typed_entity->shape.shape_type == KSHAPE_TYPE_RECTANGLE) {
			// TODO: OBB/ray check.
			return false;
		}

	} break;
	case KENTITY_TYPE_HIT_SHAPE: {
		hit_shape_entity* typed_entity = &scene->hit_shapes[type_index];

		if (typed_entity->shape.shape_type == KSHAPE_TYPE_SPHERE) {
			return raycast_hits_sphere("hit shape", base->transform, typed_entity->shape.radius, r, out_hit);
		} else if (typed_entity->shape.shape_type == KSHAPE_TYPE_RECTANGLE) {
			// TODO: OBB/ray check.
			return false;
		}

	} break;
	case KENTITY_TYPE_POINT_LIGHT: {
		point_light_entity* typed_entity = &scene->point_lights[type_index];

		f32 radius = point_light_radius_get(engine_systems_get()->light_system, typed_entity->handle);
		return raycast_hits_sphere("point light", base->transform, radius, r, out_hit);
	} break;
	case KENTITY_TYPE_SPAWN_POINT: {
		spawn_point_entity* typed_entity = &scene->spawn_points[type_index];

		return raycast_hits_sphere("spawn point", base->transform, typed_entity->radius, r, out_hit);
	} break;
	default:
	case KENTITY_TYPE_NONE:
		/* KINFO("Base node found. No further tests needed."); */
		// This will allow the hit to be counted.
		out_hit->type = RAYCAST_HIT_TYPE_BVH_AABB_BASE_NODE;
		return true;
	case KENTITY_TYPE_INVALID:
		KWARN("Hit a invalid entity named '%s'. Not counted.", name);
		return false;
	}

	// Count as a hit. NOTE: Can use this to mask what is selected, etc.
	return true;
}

b8 kscene_raycast(struct kscene* scene, const ray* r, struct raycast_result* out_result) {

	if (scene && out_result) {
		*out_result = bvh_raycast(&scene->bvh_tree, r, on_raycast_hit, scene);
		return true;
	}
	return false;
}

b8 kscene_hf_terrain_raycast(struct kscene* scene, const ray* r, b8 use_editor_aabb, hf_block* out_block, hf_chunk* out_chunk, vec3* out_pos, vec3* out_normal) {
	// FIXME: brute forcing this for now, will handle better in the future.

	if (!scene || !scene->hf.blocks) {
		return false;
	}

	u32 block_count = scene->hf.block_count_z * scene->hf.block_count_x;
	for (u32 b = 0; b < block_count; ++b) {
		hf_block* block = &scene->hf.blocks[b];

		f32 tmin = 0.0f;
		f32 tmaxi = r->max_distance;
		b8 block_hit = ray_intersects_aabb(use_editor_aabb ? block->editor_aabb : block->aabb, r->origin, r->direction, r->max_distance, &tmin, &tmaxi);
		if (block_hit) {
			// Iterate the chunks and check for a aabb hit there.
			for (u32 c = 0; c < HF_BLOCK_CHUNK_COUNT; ++c) {
				hf_chunk* chunk = &block->chunks[c];
				tmin = 0.0f;
				tmaxi = r->max_distance;
				b8 chunk_hit = ray_intersects_aabb(use_editor_aabb ? chunk->editor_aabb : chunk->aabb, r->origin, r->direction, r->max_distance, &tmin, &tmaxi);
				if (chunk_hit) {
					u64 vertex_offset = (chunk->vertex_buffer_offset - scene->hf.base_vertex_buffer_offset) / sizeof(hf_vertex_3d);
					triangle tri;
					vec3 hit_pos;
					vec3 hit_normal;
					b8 triangle_hit = ray_pick_triangle(r, false, HF_CHUNK_VERTEX_COUNT, sizeof(hf_vertex_3d), scene->hf.vertices + vertex_offset, HF_INDEX_COUNT, scene->hf.indices, &tri, &hit_pos, &hit_normal);
					if (triangle_hit) {
						// Collision! Yay
						/* KTRACE("Terrain hit: pos=%V3.3, normal=%V3.3", &hit_pos, &hit_normal); */
						*out_block = *block;
						*out_chunk = *chunk;
						*out_pos = hit_pos;
						*out_normal = hit_normal;
						return true;
					}
				}
			}
		}
	}

	/* KTRACE("Nothing hit."); */
	return false;
}

hf_terrain* kscene_hf_terrain_get(struct kscene* scene) {
	return &scene->hf;
}

b8 kscene_hf_terrain_get_height_at(struct kscene* scene, f32 world_x, f32 world_z, vec3* out_pos, vec3* out_normal) {
	return hf_terrain_get_height_at(&scene->hf, world_x, world_z, out_pos, out_normal);
}

kentity kscene_get_entity_by_name(struct kscene* scene, kname name) {
	const bt_node* node = u64_bst_find(scene->name_lookup, name);
	if (node) {
		return (kentity)node->value.u64;
	}
	return KENTITY_INVALID;
}

kentity_flags kscene_get_entity_flags(struct kscene* scene, kentity entity) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return base->flags;
	}
	return KENTITY_FLAG_NONE;
}
void kscene_set_entity_flags(struct kscene* scene, kentity entity, kentity_flags flags) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		base->flags = flags;
	}
}
void kscene_set_entity_flag(struct kscene* scene, kentity entity, kentity_flag_bits flag, b8 enabled) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		FLAG_SET(base->flags, flag, enabled);
	}
}

kname kscene_get_entity_name(struct kscene* scene, kentity entity) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return base->name;
	}
	return INVALID_KNAME;
}

void kscene_set_entity_name(struct kscene* scene, kentity entity, kname name) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		kname old_name = base->name;
		base->name = name;

		u64_bst_delete(scene->name_lookup, old_name);
		bt_node_value val = {.u64 = entity};
		bt_node* new_node = u64_bst_insert(scene->name_lookup, name, val);
		if (!scene->name_lookup) {
			scene->name_lookup = new_node;
		}
	}
}

kentity_type kscene_get_entity_type(struct kscene* scene, kentity entity) {
	return kentity_unpack_type(entity);
}

kentity* kscene_get_entity_children(struct kscene* scene, kentity entity, u16* out_count) {
	if (!scene || !out_count || entity == KENTITY_INVALID) {
		*out_count = 0;
		return 0;
	}

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		*out_count = (u16)darray_length(base->children);
		return base->children;
	}

	*out_count = 0;
	return 0;
}

kentity kscene_get_entity_parent(struct kscene* scene, kentity entity) {
	if (!scene || entity == KENTITY_INVALID) {
		return KENTITY_INVALID;
	}

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return base->parent;
	}

	return KENTITY_INVALID;
}

ktransform kscene_get_entity_transform(struct kscene* scene, kentity entity) {
	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return base->transform;
	}

	return KTRANSFORM_INVALID;
}

extents_3d kscene_get_aabb(struct kscene* scene, kentity entity) {

	base_entity* base = get_entity_base(scene, entity);
	mat4 bvh_extents_transform = ktransform_world_get(base->transform); // child_world;

	return aabb_from_mat4(extents_3d_half(base->extents), bvh_extents_transform);
}

vec3 kscene_get_entity_position(struct kscene* scene, kentity entity) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return ktransform_position_get(base->transform);
	}

	KWARN("Returning default position of zero");
	return vec3_zero();
}
void kscene_set_entity_position(struct kscene* scene, kentity entity, vec3 position) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		ktransform_position_set(base->transform, position);
	}
}

quat kscene_get_entity_rotation(struct kscene* scene, kentity entity) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return ktransform_rotation_get(base->transform);
	}

	KWARN("Returning default rotation of quat identity");
	return quat_identity();
}
void kscene_set_entity_rotation(struct kscene* scene, kentity entity, quat rotation) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		ktransform_rotation_set(base->transform, rotation);
	}
}

vec3 kscene_get_entity_scale(struct kscene* scene, kentity entity) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		return ktransform_scale_get(base->transform);
	}

	KWARN("Returning default scale of one");
	return vec3_one();
}
void kscene_set_entity_scale(struct kscene* scene, kentity entity, vec3 scale) {
	KASSERT(scene);

	base_entity* base = get_entity_base(scene, entity);
	if (base) {
		ktransform_scale_set(base->transform, scale);
	}
}

void kscene_remove_entity(struct kscene* scene, kentity* entity) {
	KASSERT_DEBUG(scene);

	if (*entity != KENTITY_INVALID) {
		kentity_type type = kentity_unpack_type(*entity);
		u16 typed_index = kentity_unpack_type_index(*entity);

		switch (type) {

		case KENTITY_TYPE_NONE: {
			KASSERT_DEBUG(typed_index < darray_length(scene->bases));
			base_entity_destroy(scene, &scene->bases[typed_index], *entity);
		} break;

		case KENTITY_TYPE_MODEL: {
			KASSERT_DEBUG(typed_index < darray_length(scene->models));
			model_entity_destroy(scene, &scene->models[typed_index], *entity);
		} break;

		case KENTITY_TYPE_POINT_LIGHT: {
			KASSERT_DEBUG(typed_index < darray_length(scene->point_lights));
			point_light_entity_destroy(scene, &scene->point_lights[typed_index], *entity);
		} break;

		case KENTITY_TYPE_SPAWN_POINT: {
			KASSERT_DEBUG(typed_index < darray_length(scene->spawn_points));
			spawn_point_entity_destroy(scene, &scene->spawn_points[typed_index], *entity);
		} break;

		case KENTITY_TYPE_VOLUME: {
			KASSERT_DEBUG(typed_index < darray_length(scene->volumes));
			volume_entity_destroy(scene, &scene->volumes[typed_index], *entity);
		} break;

		case KENTITY_TYPE_HIT_SHAPE: {
			KASSERT_DEBUG(typed_index < darray_length(scene->hit_shapes));
			hit_shape_entity_destroy(scene, &scene->hit_shapes[typed_index], *entity);
		} break;

		case KENTITY_TYPE_WATER_PLANE: {
			KASSERT_DEBUG(typed_index < darray_length(scene->water_planes));
			water_plane_entity_destroy(scene, &scene->water_planes[typed_index], *entity);
		} break;

		case KENTITY_TYPE_AUDIO_EMITTER: {
			KASSERT_DEBUG(typed_index < darray_length(scene->audio_emitters));
			audio_emitter_entity_destroy(scene, &scene->audio_emitters[typed_index], *entity);
		} break;

		default:
		case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
			// FIXME: heightmp_terrain_entity_destroy();
			KFATAL("Not yet implemented");
			return;
		}

		*entity = KENTITY_INVALID;
	}
}

static void entity_add_child(kscene* scene, kentity parent, kentity child) {
	base_entity* parent_base = get_entity_base(scene, parent);
	base_entity* child_base = get_entity_base(scene, child);
	if (parent_base) {
		if (!parent_base->children) {
			parent_base->children = darray_create(kentity);
		}
		darray_push(parent_base->children, child);

		child_base->parent = parent;
		// Also update the transform parent.
		ktransform_parent_set(child_base->transform, parent_base->transform);
	} else {
		// Add to the scene's root list.
		darray_push(scene->root_entities, child);
		// Also update the transform parent.
		ktransform_parent_set(child_base->transform, KTRANSFORM_INVALID);
	}
}

kentity kscene_add_entity(struct kscene* scene, kname name, ktransform transform, kentity parent) {
	KASSERT_DEBUG(scene);

	// Get an typed entity index
	u16 spawn_point_count = darray_length(scene->bases);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < spawn_point_count; ++i) {
		if (FLAG_GET(scene->bases[i].flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = spawn_point_count;
		darray_push(scene->bases, (base_entity){0});
	}

	base_entity* new_ent = &scene->bases[entity_index];

	kentity entity = init_base_entity(scene, new_ent, entity_index, name, KENTITY_TYPE_NONE, transform, parent);

	new_ent->extents.min = vec3_from_scalar(-0.1f);
	new_ent->extents.max = vec3_from_scalar(0.1f);

	return entity;
}

static void base_entity_destroy(kscene* scene, base_entity* base, kentity entity_handle) {
	// Don't bother with hierarchy if no valid entity handle is passed, since that means the entire
	// scene is being cleaned up.
	if (entity_handle != KENTITY_INVALID) {
		// Remove as a child from parent (if there is one) and reparent children of this node.
		kentity parent = base->parent;
		base_entity* parent_base = KNULL;
		if (parent != KENTITY_INVALID) {
			parent_base = get_entity_base(scene, parent);
			if (parent_base->children) {
				u16 count = darray_length(parent_base->children);
				for (u16 i = 0; i < count; ++i) {
					if (parent_base->children[i] == entity_handle) {
						// Match - remove it from parent.
						kentity popped_handle;
						darray_pop_at(parent_base->children, i, &popped_handle);

						break;
					}
				}
			}
		} else {
			// If it has no parent, it's a root. Remove from that list.
			u16 len = darray_length(scene->root_entities);
			for (u16 i = 0; i < len; ++i) {
				if (scene->root_entities[i] == entity_handle) {
					darray_pop_at(scene->root_entities, i, 0);
				}
			}
		}

		u16 child_count = base->children ? darray_length(base->children) : 0;
		for (u16 i = 0; i < child_count; ++i) {
			// Reassign its parent.
			kentity child_entity = base->children[i];
			base_entity* child = get_entity_base(scene, child_entity);
			child->parent = parent;
			if (parent_base) {
				// Add to parent's child list.
				if (!parent_base->children) {
					parent_base->children = darray_reserve(kentity, child_count);
				}
				darray_push(parent_base->children, child_entity);
				ktransform_parent_set(child->transform, parent_base->transform);
			} else {
				// It's now a root.
				darray_push(scene->root_entities, child_entity);
				ktransform_parent_set(child->transform, KTRANSFORM_INVALID);
			}
		}

		// Remove its name from the lookup table.
		u64_bst_delete(scene->name_lookup, base->name);
	}

	if (base->children) {
		darray_destroy(base->children);
		base->children = 0;
	}

	if (base->tag_count && base->tags) {
		KFREE_TYPE_CARRAY(base->tags, kstring_id, base->tag_count);
	}
	base->tags = 0;
	base->tag_count = 0;

	ktransform_destroy(&base->transform);

	// Cleaup debug data.
#if KOHI_DEBUG
	if (base->debug_data_index != INVALID_ID_U32) {
		renderer_geometry_destroy(&scene->debug_datas[base->debug_data_index].geometry);
		base->debug_data_index = INVALID_ID_U32;
	}
#endif

	// Flag as free
	FLAG_SET(base->flags, KENTITY_FLAG_FREE_BIT, true);
}

kentity kscene_add_model_pos_rot_scale(struct kscene* scene, kname name, kentity parent, kname asset_name, kname package_name, vec3 pos, quat rot, vec3 scale) {
	ktransform transform = ktransform_from_position_rotation_scale(pos, rot, scale, KENTITY_INVALID);
	return kscene_add_model(scene, name, transform, parent, asset_name, package_name, 0, 0);
}

typedef struct kscene_model_load_context {
	kscene* scene;
	kentity entity;
	PFN_model_loaded on_loaded_callback;
	void* model_loaded_context;
} kscene_model_load_context;

kentity kscene_add_model(struct kscene* scene, kname name, ktransform transform, kentity parent, kname asset_name, kname package_name, PFN_model_loaded on_loaded_callback, void* load_context) {
	KASSERT_DEBUG(scene);

	if (asset_name == INVALID_KNAME) {
		KERROR("%s - invalid asset_name was provided. Nothing to be done.", __FUNCTION__);
		return KENTITY_INVALID;
	}

	// Get an typed entity index
	u16 model_count = darray_length(scene->models);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < model_count; ++i) {
		if (FLAG_GET(scene->models[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = model_count;
		// Push an empty entry, to be filled out in a moment.
		darray_push(scene->models, (model_entity){0});
	}

	model_entity* new_ent = &scene->models[entity_index];

	kentity entity = init_base_entity(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_MODEL, transform, parent);

	/* aabb b = aabb_create((vec3){-1, -1, -1}, vec3_one());
	new_ent->base.bvh_id = bvh_insert(&scene->bvh_tree, b, entity); */

	new_ent->model = (kmodel_instance){.base_mesh = INVALID_ID_U16, .instance = INVALID_ID_U16};
	new_ent->package_name = package_name;
	new_ent->asset_name = asset_name;

	kscene_model_load_context* context = kallocate(sizeof(kscene_model_load_context), MEMORY_TAG_SCENE);
	context->entity = entity;
	context->scene = scene;
	context->on_loaded_callback = on_loaded_callback;
	context->model_loaded_context = load_context;

	kmodel_system_state* model_state = engine_systems_get()->model_system;

	// Kick off async asset load
	if (package_name == INVALID_KNAME) {
		new_ent->model = kmodel_instance_acquire_from_package(model_state, asset_name, package_name, on_model_loaded, context);
	} else {
		new_ent->model = kmodel_instance_acquire(model_state, asset_name, on_model_loaded, context);
	}

	return entity;
}

static void model_entity_destroy(kscene* scene, model_entity* typed_entity, kentity entity_handle) {

	// Unmap from internal material->geometry maps. Also frees geometry references.
	// Don't bother if cleaning up the entire scene, though.
	if (entity_handle != KENTITY_INVALID) {
		unmap_model_entity_geometries(scene, entity_handle);
	}

	kmodel_system_state* model_state = engine_systems_get()->model_system;
	// Release the model instance from the entity, which also releases held material instances.
	kmodel_instance_release(model_state, &typed_entity->model);

	typed_entity->asset_name = INVALID_KNAME;
	typed_entity->package_name = INVALID_KNAME;

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_point_light(struct kscene* scene, kname name, ktransform transform, kentity parent, vec3 colour, f32 linear, f32 quadratic) {
	KASSERT_DEBUG(scene);

	// Get an typed entity index
	u16 point_light_count = darray_length(scene->point_lights);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < point_light_count; ++i) {
		if (FLAG_GET(scene->point_lights[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = point_light_count;
		darray_push(scene->point_lights, (point_light_entity){0});
	}

	point_light_entity* new_ent = &scene->point_lights[entity_index];

	kentity entity = init_base_entity(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_POINT_LIGHT, transform, parent);
	vec3 pos = ktransform_world_position_get(new_ent->base.transform);

	new_ent->colour = colour;
	new_ent->linear = linear;
	new_ent->quadratic = quadratic;
	new_ent->handle = point_light_create(engine_systems_get()->light_system, pos, colour, 1.0f, linear, quadratic);

	f32 r = point_light_radius_get(engine_systems_get()->light_system, new_ent->handle);
	new_ent->base.extents.min = (vec3){-r, -r, -r};
	new_ent->base.extents.max = (vec3){r, r, r};

	create_debug_data(
		scene,
		extents_3d_half(new_ent->base.extents),
		vec3_zero(),
		entity,
		KSCENE_DEBUG_DATA_TYPE_SPHERE,
		((colour4){new_ent->colour.r, new_ent->colour.g, new_ent->colour.b, 1}),
		true,
		&new_ent->base.debug_data_index);

	return entity;
}

static void point_light_entity_destroy(kscene* scene, point_light_entity* typed_entity, kentity entity_handle) {
	light_destroy(engine_systems_get()->light_system, typed_entity->handle);

	typed_entity->linear = 0;
	typed_entity->quadratic = 0;
	typed_entity->colour = vec3_zero();

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_spawn_point(struct kscene* scene, kname name, ktransform transform, kentity parent, f32 radius) {
	KASSERT_DEBUG(scene);

	// Get an typed entity index
	u16 spawn_point_count = darray_length(scene->spawn_points);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < spawn_point_count; ++i) {
		if (FLAG_GET(scene->spawn_points[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = spawn_point_count;
		darray_push(scene->spawn_points, (spawn_point_entity){0});
	}

	radius = radius ? radius : 1.0f;
	spawn_point_entity* new_ent = &scene->spawn_points[entity_index];

	extents_3d ex = extents_3d_from_scalar(radius);
	kentity entity = init_base_entity_with_extents(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_SPAWN_POINT, transform, parent, ex);

	new_ent->radius = radius;

	create_debug_data(scene, vec3_from_scalar(radius), vec3_zero(), entity, KSCENE_DEBUG_DATA_TYPE_SPHERE, ((colour4){0, 0, 1, 1}), true, &new_ent->base.debug_data_index);

	return entity;
}

static void spawn_point_entity_destroy(kscene* scene, spawn_point_entity* typed_entity, kentity entity_handle) {
	// NOTE: Nothing here needing destruction aside from the base.

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_volume(
	struct kscene* scene,
	kname name,
	ktransform transform,
	kentity parent,
	kscene_volume_type type,
	kcollision_shape shape,
	u8 hit_shape_tag_count,
	kstring_id* hit_shape_tags,
	const char* on_enter_command,
	const char* on_leave_command,
	const char* on_tick_command) {

	// Get an typed entity index
	u16 volume_count = darray_length(scene->volumes);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < volume_count; ++i) {
		if (FLAG_GET(scene->volumes[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = volume_count;
		darray_push(scene->volumes, (volume_entity){0});
	}

	volume_entity* new_ent = &scene->volumes[entity_index];

	extents_3d ex;
	switch (shape.shape_type) {
	default:
	case KSHAPE_TYPE_SPHERE:
		ex = extents_3d_from_scalar(shape.radius ? shape.radius : 1.0f);
		break;
	case KSHAPE_TYPE_RECTANGLE:
		ex = extents_3d_from_size(shape.extents);
		break;
	}

	kentity entity = init_base_entity_with_extents(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_VOLUME, transform, parent, ex);

	new_ent->shape = shape;
	new_ent->type = type;
	new_ent->hit_shape_tag_count = hit_shape_tag_count;
	new_ent->hit_shape_tags = KALLOC_TYPE_CARRAY(kstring_id, new_ent->hit_shape_tag_count);
	KCOPY_TYPE_CARRAY(new_ent->hit_shape_tags, hit_shape_tags, kstring_id, new_ent->hit_shape_tag_count);
	if (on_enter_command) {
		new_ent->on_enter_command = string_duplicate(on_enter_command);
	}
	if (on_leave_command) {
		new_ent->on_leave_command = string_duplicate(on_leave_command);
	}
	if (on_tick_command) {
		new_ent->on_tick_command = string_duplicate(on_tick_command);
	}

	create_debug_data(scene, extents_3d_half(new_ent->base.extents), vec3_zero(), entity, debug_type_from_shape_type(new_ent->shape.shape_type), ENTITY_VOLUME_DEBUG_COLOUR, true, &new_ent->base.debug_data_index);

	return entity;
}

static void volume_entity_destroy(kscene* scene, volume_entity* typed_entity, kentity entity_handle) {
	string_free(typed_entity->on_enter_command);
	typed_entity->on_enter_command = KNULL;
	string_free(typed_entity->on_leave_command);
	typed_entity->on_leave_command = KNULL;
	string_free(typed_entity->on_tick_command);
	typed_entity->on_tick_command = KNULL;

	if (typed_entity->hit_shape_tag_count && typed_entity->hit_shape_tags) {
		KFREE_TYPE_CARRAY(typed_entity->hit_shape_tags, kstring_id, typed_entity->hit_shape_tag_count);
	}

	typed_entity->hit_shape_tag_count = 0;
	typed_entity->hit_shape_tags = KNULL;

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_hit_shape(
	struct kscene* scene,
	kname name,
	ktransform transform,
	kentity parent,
	kcollision_shape shape,
	u8 tag_count,
	kstring_id* tags) {

	// Get an typed entity index
	u16 hit_shape_count = darray_length(scene->hit_shapes);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < hit_shape_count; ++i) {
		if (FLAG_GET(scene->hit_shapes[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = hit_shape_count;
		darray_push(scene->hit_shapes, (hit_shape_entity){0});
	}

	hit_shape_entity* new_ent = &scene->hit_shapes[entity_index];

	kentity entity = init_base_entity(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_HIT_SHAPE, transform, parent);

	new_ent->shape = shape;
	new_ent->base.tag_count = tag_count;
	if (new_ent->base.tag_count) {
		new_ent->base.tags = KALLOC_TYPE_CARRAY(kstring_id, new_ent->base.tag_count);
		KCOPY_TYPE_CARRAY(new_ent->base.tags, tags, kstring_id, new_ent->base.tag_count);
	}
	return entity;
}

static void hit_shape_entity_destroy(kscene* scene, hit_shape_entity* typed_entity, kentity entity_handle) {
	// NOTE: Nothing here needing destruction aside from the base.

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_water_plane(struct kscene* scene, kname name, ktransform transform, kentity parent, f32 size) {

	// Get an typed entity index
	u16 water_plane_count = darray_length(scene->water_planes);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < water_plane_count; ++i) {
		if (FLAG_GET(scene->water_planes[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = water_plane_count;
		darray_push(scene->water_planes, (water_plane_entity){0});
	}

	water_plane_entity* new_ent = &scene->water_planes[entity_index];
	new_ent->size = size;

	kentity entity = init_base_entity(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_WATER_PLANE, transform, parent);

	new_ent->base.extents.min = (vec3){-size, 0, -size};
	new_ent->base.extents.max = (vec3){size, 0, size};

	// Setup geometry
	vertex_3d vertices[4];
	vertices[0] = (vertex_3d){-size, 0, -size, 0, 0, 1, 0, 0};
	vertices[1] = (vertex_3d){-size, 0, +size, 0, 0, 1, 0, 1};
	vertices[2] = (vertex_3d){+size, 0, +size, 0, 0, 1, 1, 1};
	vertices[3] = (vertex_3d){+size, 0, -size, 0, 0, 1, 1, 0};

	for (u8 i = 0; i < 4; ++i) {
		vertices[i].normal = (vec3){0, 1, 0};
		vertices[i].colour = vec4_one();
		vertices[i].tangent = (vec4){1, 0, 0, 1};
	}

	u32 indices[6];
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 2;
	indices[4] = 3;
	indices[5] = 0;

	new_ent->geo.type = KGEOMETRY_TYPE_3D_STATIC;
	new_ent->geo.generation = INVALID_ID_U16;
	new_ent->geo.vertex_count = 4;
	new_ent->geo.vertex_element_size = sizeof(vertex_3d);
	new_ent->geo.vertex_buffer_offset = 0;
	new_ent->geo.vertices = KALLOC_TYPE_CARRAY(vertex_3d, new_ent->geo.vertex_count);
	KCOPY_TYPE_CARRAY(new_ent->geo.vertices, vertices, vertex_3d, new_ent->geo.vertex_count);

	new_ent->geo.index_count = 6;
	new_ent->geo.index_element_size = sizeof(u32);
	new_ent->geo.index_buffer_offset = 0;
	new_ent->geo.indices = KALLOC_TYPE_CARRAY(u32, new_ent->geo.index_count);
	KCOPY_TYPE_CARRAY(new_ent->geo.indices, indices, u32, new_ent->geo.index_count);

	if (!renderer_geometry_upload(&new_ent->geo)) {
		KERROR("Water plane geometry upload failed. See logs for details.");
		return KENTITY_INVALID;
	}

	// Search for an empty slot first.
	u16 geometry_array_index = INVALID_ID_U16;
	{
		u16 len = (u16)darray_length(scene->model_geometry_datas);
		for (u16 i = 0; i < len; ++i) {
			if (FLAG_GET(scene->model_geometry_datas[i].flags, KGEOMETRY_DATA_FLAG_FREE_BIT)) {
				// Found a free slot - use it.
				FLAG_SET(scene->model_geometry_datas[i].flags, KGEOMETRY_DATA_FLAG_FREE_BIT, false);
				geometry_array_index = i;
				break;
			}
		}
		if (geometry_array_index == INVALID_ID_U16) {
			// No free entry found. Push an empty one. The index will be the former length of the array before the push.
			geometry_array_index = darray_length(scene->model_geometry_datas);
			darray_push(scene->model_geometry_datas, (kgeometry_data){0});
			darray_push(scene->model_geometry_extents, (extents_3d){0});
		}
	}
	kgeometry_data* new_geo = &scene->model_geometry_datas[geometry_array_index];

	// Get water material.
	// FIXME: Make this configurable.
	kmaterial_instance mat_inst = kmaterial_system_get_default_water(engine_systems_get()->material_system);

	// Extract the required data into a new entry into the global flat list.
	new_geo->vertex_count = 4;
	new_geo->vertex_offset = new_ent->geo.vertex_buffer_offset;
	new_geo->index_count = 6;
	new_geo->index_offset = new_ent->geo.index_buffer_offset;
	new_geo->material_instance_id = mat_inst.instance_id;

	// Set flags.
	new_geo->flags = KGEOMETRY_DATA_FLAG_NONE;
	new_geo->flags = FLAG_SET(new_geo->flags, KGEOMETRY_DATA_FLAG_WINDING_INVERTED_BIT, false);

	new_ent->ref.entity = entity;
	new_ent->ref.geometry_index = geometry_array_index;
	new_ent->base_material = mat_inst.base_material;

	extents_3d* new_extents = &scene->model_geometry_extents[geometry_array_index];
	new_extents->min = (vec3){-size, 0, -size};
	new_extents->max = (vec3){size, 0, size};

	return entity;
}

static void water_plane_entity_destroy(kscene* scene, water_plane_entity* typed_entity, kentity entity_handle) {
	kgeometry_data* geo_data = &scene->model_geometry_datas[typed_entity->ref.geometry_index];
	extents_3d* extents = &scene->model_geometry_extents[typed_entity->ref.geometry_index];

	// Release the material
	kmaterial_instance mat_inst = {
		.instance_id = geo_data->material_instance_id,
		.base_material = typed_entity->base_material};
	kmaterial_system_release(engine_systems_get()->material_system, &mat_inst);

	// Free the geometry.
	renderer_geometry_destroy(&typed_entity->geo);
	geometry_destroy(&typed_entity->geo);

	// Free up the geometry references
	kzero_memory(geo_data, sizeof(kgeometry_data));
	kzero_memory(extents, sizeof(extents_3d));
	FLAG_SET(geo_data->flags, KGEOMETRY_DATA_FLAG_FREE_BIT, true);

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kentity kscene_add_audio_emitter(
	struct kscene* scene,
	kname name,
	ktransform transform,
	kentity parent,
	f32 inner_radius,
	f32 outer_radius,
	f32 volume,
	f32 falloff,
	b8 is_looping,
	b8 is_streaming,
	kname asset_name,
	kname package_name) {

	// Get an typed entity index
	u16 audio_emitter_count = darray_length(scene->audio_emitters);
	u16 entity_index = INVALID_ID_U16;
	for (u16 i = 0; i < audio_emitter_count; ++i) {
		if (FLAG_GET(scene->audio_emitters[i].base.flags, KENTITY_FLAG_FREE_BIT)) {
			entity_index = i;
			break;
		}
	}
	if (entity_index == INVALID_ID_U16) {
		entity_index = audio_emitter_count;
		darray_push(scene->audio_emitters, (audio_emitter_entity){0});
	}

	audio_emitter_entity* new_ent = &scene->audio_emitters[entity_index];
	new_ent->asset_name = asset_name;
	new_ent->package_name = package_name;
	new_ent->inner_radius = inner_radius;
	new_ent->outer_radius = outer_radius;
	new_ent->falloff = falloff;
	new_ent->volume = volume;
	new_ent->is_looping = is_looping;
	new_ent->is_streaming = is_streaming;

	extents_3d ex = extents_3d_from_scalar(outer_radius);

	kentity entity = init_base_entity_with_extents(scene, &new_ent->base, entity_index, name, KENTITY_TYPE_AUDIO_EMITTER, transform, parent, ex);

	if (!kaudio_emitter_create(
			engine_systems_get()->audio_system,
			inner_radius,
			outer_radius,
			volume,
			falloff,
			is_looping,
			is_streaming,
			asset_name,
			package_name,
			&new_ent->emitter)) {
		KERROR("Failed to create audio emitter. See logs for details.");
	}

	mat4 world;
	if (transform != KTRANSFORM_INVALID) {
		world = ktransform_world_get(transform);
	} else {
		// TODO: traverse tree to try and find a ancestor node with a transform.
		world = mat4_identity();
	}
	// Get world position for the audio emitter based on it's owning node's ktransform.
	vec3 emitter_world_pos = mat4_position(world);
	kaudio_emitter_world_position_set(engine_systems_get()->audio_system, new_ent->emitter, emitter_world_pos);

	create_debug_data(scene, extents_3d_half(new_ent->base.extents), vec3_zero(), entity, KSCENE_DEBUG_DATA_TYPE_SPHERE, ENTITY_AUDIO_EMITTER_DEBUG_COLOUR, true, &new_ent->base.debug_data_index);

	return entity;
}

static void audio_emitter_entity_destroy(kscene* scene, audio_emitter_entity* typed_entity, kentity entity_handle) {
	kaudio_emitter_destroy(engine_systems_get()->audio_system, &typed_entity->emitter);

	base_entity_destroy(scene, &typed_entity->base, entity_handle);
}

kmodel_instance kscene_model_entity_get_instance(struct kscene* scene, kentity entity) {
	u16 type_index = kentity_unpack_type_index(entity);
	return scene->models[type_index].model;
}

kdirectional_light_data kscene_get_directional_light_data(struct kscene* scene) {
	return (kdirectional_light_data){
		.light = scene->directional_light,
		.direction = directional_light_get_direction(engine_systems_get()->light_system, scene->directional_light)};
}

kskybox_render_data kscene_get_skybox_render_data(struct kscene* scene) {
	kskybox_render_data out_data = {
		.skybox_texture = scene->sb.cubemap,
		.shader_set0_instance_id = scene->sb.shader_set0_instance_id,
		.sb_index_count = scene->sb.geometry.index_count,
		.sb_vertex_count = scene->sb.geometry.vertex_count,
		.sb_index_offset = scene->sb.geometry.index_buffer_offset,
		.sb_vertex_offset = scene->sb.geometry.vertex_buffer_offset};

	return out_data;
}

// Gets model render data, organized by material.
static kmaterial_render_data* kscene_get_model_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	kscene_render_data_flag_bits flags,
	b8 is_animated,
	u16* out_material_count) {

	KASSERT_DEBUG(scene);
	KASSERT_DEBUG(p_frame_data);
	KASSERT_DEBUG(out_material_count);

	frame_allocator_int* frame_allocator = &p_frame_data->allocator;

	kmaterial_to_geometry_map* map = 0;
	if (FLAG_GET(flags, KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT)) {
		// Only get transparent geometries
		map = is_animated ? &scene->transparent_animated_model_material_map : &scene->transparent_static_model_material_map;
	} else {
		// Only get opaque geometries
		map = is_animated ? &scene->opaque_animated_model_material_map : &scene->opaque_static_model_material_map;
	}

	// Extract geometry to be rendered from the appropriate map.
	kmaterial_render_data* mats = darray_create_with_allocator(kmaterial_render_data, frame_allocator);

	for (u16 i = 0; i < map->count; ++i) {
		kmaterial_geometry_list* list = &map->lists[i];

		kmaterial_render_data mat_render_data = {0};

		mat_render_data.base_material = list->base_material;
		mat_render_data.geometries = darray_create_with_allocator(kgeometry_render_data, frame_allocator);

		// Each geometry in the material.
		for (u16 g = 0; g < list->count; ++g) {
			// Use the geometry reference to get the geometry data and entity.
			kgeometry_ref* ref = &list->geometries[g];
			kgeometry_data* geo = &scene->model_geometry_datas[ref->geometry_index];
			u16 entity_index = kentity_unpack_type_index(ref->entity);
			model_entity* entity = &scene->models[entity_index];

			// TODO: check entity visibility

			/* mat4 world_model = ktransform_world_get(entity->base.transform); */

			if (frustum) {
				// TODO: frustum cull check, continue to next if fails.
			}

			kmodel_system_state* model_state = engine_systems_get()->model_system;

			// If it passes all tests, create/push the render data.
			kgeometry_render_data rd = {
				.vertex_count = geo->vertex_count,
				.vertex_offset = geo->vertex_offset,
				.index_count = geo->index_count,
				.index_offset = geo->index_offset,
				.material_instance_id = geo->material_instance_id,
				.transform = entity->base.transform,
				.animation_id = INVALID_ID_U16,
			};
			if (is_animated) {
				rd.animation_id = kmodel_instance_animation_id_get(model_state, entity->model);
			}

			// FIXME: Pick the closest lights that actually interact with this geometry and add them
			// to the list. For now this is just adding the closest 8.
			rd.bound_point_light_count = KMIN(darray_length(scene->point_lights), KMATERIAL_MAX_BOUND_POINT_LIGHTS);
			for (u8 l = 0; l < rd.bound_point_light_count; ++l) {
				// TODO: distance check.
				rd.bound_point_light_indices[l] = scene->point_lights[l].handle;
			}

			// Flags - note that these aren't a straight copy, as the flag values between these two sets vary.
			rd.flags = FLAG_SET(rd.flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT, FLAG_GET(geo->flags, KGEOMETRY_DATA_FLAG_WINDING_INVERTED_BIT));

			// This is building the render data array, so just pushing here is fine.
			darray_push(mat_render_data.geometries, rd);
			mat_render_data.geometry_count++;
		}

		// If there are actually things to render, push the mat_render_data to the list.
		if (mat_render_data.geometry_count) {
			darray_push(mats, mat_render_data);
		}
	}

	// Once finished, return the list of geometries-by-material.
	*out_material_count = (u16)darray_length(mats);
	return mats;
}

// Gets static model render data, organized by material.
kmaterial_render_data* kscene_get_static_model_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	kscene_render_data_flag_bits flags,
	u16* out_material_count) {

	return kscene_get_model_render_data(scene, p_frame_data, frustum, flags, false, out_material_count);
}

// Gets animated model render data, organized by material.
kmaterial_render_data* kscene_get_animated_model_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	kscene_render_data_flag_bits flags,
	u16* out_material_count) {

	return kscene_get_model_render_data(scene, p_frame_data, frustum, flags, true, out_material_count);
}

// Gets terrain chunk render data.
hm_terrain_render_data* kscene_get_hm_terrain_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	u32 flags,
	u16* out_terrain_count) {

	// FIXME: implement this

	return 0;
}

hf_terrain_render_data kscene_get_hf_terrain_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	u32 flags) {

	hf_terrain_render_data data = {0};

	const hf_terrain* t = &scene->hf;

	data.index_count = HF_INDEX_COUNT;
	data.index_buffer_offset = t->index_buffer_offset;
	data.block_count = t->block_count_z * t->block_count_x;
	data.blocks = p_frame_data->allocator.allocate(sizeof(hf_terrain_block_render_data) * data.block_count);

	// Array textures.
	data.albedo_texture_array = t->albedo_texture_array;
	data.normal_texture_array = t->normal_texture_array;
	data.mra_texture_array = t->mra_texture_array;

	// TODO: frustum culling.
	for (u16 i = 0; i < data.block_count; ++i) {
		// TODO: frustum cull the block
		hf_block* block = &t->blocks[i];
		hf_terrain_block_render_data* block_rd = &data.blocks[i];

		block_rd->chunk_count = HF_BLOCK_CHUNK_COUNT;
		block_rd->chunks = p_frame_data->allocator.allocate(sizeof(hf_terrain_chunk_render_data) * block_rd->chunk_count);
		block_rd->splatmap = block->splatmap;
		block_rd->shader_instance_id = block->shader_instance_id;

		// TODO: frustum culling within this block
		for (u16 c = 0; c < block_rd->chunk_count; ++c) {
			hf_chunk* chunk = &block->chunks[c];
			hf_terrain_chunk_render_data* chunk_rd = &block_rd->chunks[c];

			// TODO: LOD
			chunk_rd->vertex_count = HF_CHUNK_VERTEX_COUNT;
			chunk_rd->vertex_buffer_offset = chunk->vertex_buffer_offset;

			chunk_rd->base_material_index = chunk->material_indices[0];
			for (u8 a = 0; a < HF_TERRAIN_CHUNK_MAX_MATERIALS - 1; ++a) {
				chunk_rd->material_indices.elements[a] = chunk->material_indices[a + 1];
			}

			// FIXME: Pick the closest lights that actually interact with this geometry and add them
			// to the list. For now this is just adding the closest 8.
			chunk_rd->bound_point_light_count = KMIN(darray_length(scene->point_lights), KMATERIAL_MAX_BOUND_POINT_LIGHTS);
			for (u8 l = 0; l < chunk_rd->bound_point_light_count; ++l) {
				// TODO: distance check.
				chunk_rd->bound_point_light_indices[l] = scene->point_lights[l].handle;
			}
		}
	}

	return data;
}

hf_terrain_material_data* kscene_get_hf_terrain_materials(struct kscene* scene, u8* out_count) {
	if (!scene || !out_count || !scene->hf.vertices) {
		return KNULL;
	}

	hf_terrain_material_data* materials = KALLOC_TYPE_CARRAY(hf_terrain_material_data, scene->hf.material_count);
	*out_count = scene->hf.material_count;

	for (u8 i = 0; i < (*out_count); ++i) {
		materials[i].name = scene->hf.material_names[i];

		materials[i].albedo_asset_name = scene->hf.albedo_asset_names[i];
		materials[i].albedo_asset_package_name = scene->hf.albedo_package_names[i];

		materials[i].normal_asset_name = scene->hf.normal_asset_names[i];
		materials[i].normal_asset_package_name = scene->hf.normal_package_names[i];

		materials[i].mra_asset_name = scene->hf.mra_asset_names[i];
		materials[i].mra_asset_package_name = scene->hf.mra_package_names[i];
	}

	return materials;
}

#if KOHI_DEBUG
kdebug_geometry_render_data* kscene_get_debug_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	u32 flags,
	u16* out_geometry_count) {

	// If debug in general is not enabled, skip it.
	if (!FLAG_GET(scene->flags, KSCENE_FLAG_DEBUG_ENABLED_BIT)) {
		*out_geometry_count = 0;
		return KNULL;
	}

	u16 debug_data_count = darray_length(scene->debug_datas);
	if (!debug_data_count) {
		*out_geometry_count = 0;
		return 0;
	}

	u16 total_count = debug_data_count;

	for (u32 i = 0; i < scene->bvh_tree.capacity; ++i) {
		bvh_node* n = &scene->bvh_tree.nodes[i];
		if (n->height >= 0) {
			total_count++;
		}
	}

	kdebug_geometry_render_data* out_render_data = p_frame_data->allocator.allocate(sizeof(kdebug_geometry_render_data) * total_count);
	kzero_memory(out_render_data, sizeof(kdebug_geometry_render_data) * total_count);

	i16 rd_idx = 0;
	for (u16 i = 0; i < debug_data_count; ++i) {
		kscene_debug_data* data = &scene->debug_datas[i];
		if (data->type != KSCENE_DEBUG_DATA_TYPE_NONE) {
			kdebug_geometry_render_data* rd = &out_render_data[rd_idx];
			rd->geo.index_count = data->geometry.index_count;
			rd->geo.index_offset = data->geometry.index_buffer_offset;
			rd->geo.vertex_count = data->geometry.vertex_count;
			rd->geo.vertex_offset = data->geometry.vertex_buffer_offset;
			rd->model = data->model;
			rd->colour = data->colour;
			rd_idx++;
		}
	}

	// render BVH AABBs
	if (scene->flags & KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT) {
		for (u32 i = 0; i < scene->bvh_tree.capacity; ++i) {
			bvh_node* n = &scene->bvh_tree.nodes[i];
			scene_bvh_debug_data* data = &scene->bvh_debug_pool[i];
			if (n->height >= 0) {
				kdebug_geometry_render_data* rd = &out_render_data[rd_idx];
				rd->geo.index_count = data->geo.index_count;
				rd->geo.index_offset = data->geo.index_buffer_offset;
				rd->geo.vertex_count = data->geo.vertex_count;
				rd->geo.vertex_offset = data->geo.vertex_buffer_offset;
				rd->geo.transform = scene->bvh_transform;
				rd->colour = n->height ? (colour4){1.0f - (n->height * 0.1f), 0, 0, 1} : (colour4)vec4_create(0, 1, 1, 1);
				rd->model = data->model;
				rd_idx++;
			}
		}
	}

	*out_geometry_count = rd_idx - 1;

	return out_render_data;
}

kdebug_geometry_render_data kscene_get_editor_gizmo_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	u32 flags) {

	kdebug_geometry_render_data out_data = {0};

	// FIXME: implement this

	return out_data;
}
#endif

kwater_plane_render_data* kscene_get_water_plane_render_data(
	struct kscene* scene,
	struct frame_data* p_frame_data,
	kfrustum* frustum,
	u32 flags,
	u16* out_water_plane_count) {

	*out_water_plane_count = darray_length(scene->water_planes);
	kwater_plane_render_data* prd = p_frame_data->allocator.allocate(sizeof(kwater_plane_render_data) * *out_water_plane_count);
	for (u16 i = 0; i < *out_water_plane_count; ++i) {
		water_plane_entity* wp = &scene->water_planes[i];
		kwater_plane_render_data* p = &prd[i];

		kgeometry_data* g = &scene->model_geometry_datas[wp->ref.geometry_index];

		p->material.base_material = wp->base_material;
		p->material.instance_id = g->material_instance_id;
		p->transform = wp->base.transform;
		p->index_buffer_offset = g->index_offset;
		p->vertex_buffer_offset = g->vertex_offset;

		// FIXME: Pick the closest lights that actually interact with this geometry and add them
		// to the list. For now this is just adding the closest 8.
		p->bound_point_light_count = KMIN(darray_length(scene->point_lights), KMATERIAL_MAX_BOUND_POINT_LIGHTS);
		for (u8 l = 0; l < p->bound_point_light_count; ++l) {
			// TODO: distance check.
			p->bound_point_light_indices[l] = scene->point_lights[l].handle;
		}
	}

	return prd;
}

kentity* kscene_get_spawn_points(
	struct kscene* scene,
	u32 flags,
	u16* out_spawn_point_count) {

	KASSERT_DEBUG(scene);

	*out_spawn_point_count = scene->spawn_points ? darray_length(scene->spawn_points) : 0;
	if (*out_spawn_point_count) {
		kentity* entities = KALLOC_TYPE_CARRAY(kentity, *out_spawn_point_count);
		for (u16 i = 0; i < *out_spawn_point_count; ++i) {
			entities[i] = kentity_pack(
				KENTITY_TYPE_SPAWN_POINT,
				i,
				0,
				0);
		}

		return entities;
	}
	return KNULL;
}

klight_render_data* kscene_get_all_point_lights(
	struct kscene* scene,
	frame_data* p_frame_data,
	u32 flags,
	u16* out_point_light_count) {

	if (scene) {
		if (out_point_light_count) {

			u8 count = KMIN(darray_length(scene->point_lights), KMATERIAL_MAX_GLOBAL_POINT_LIGHTS);
			klight_render_data* out_lights = p_frame_data->allocator.allocate(sizeof(klight_render_data) * count);
			for (u8 i = 0; i < count; ++i) {
				point_light_entity* e = &scene->point_lights[i];
				klight_render_data* rd = &out_lights[i];

				rd->light = e->handle;
				rd->transform = e->base.transform;
			}

			*out_point_light_count = 0;
			return out_lights;
		}

		*out_point_light_count = 0;
	}

	return 0;
}

static kentity init_base_entity_with_extents(kscene* scene, base_entity* base, u16 entity_index, kname name, kentity_type type, ktransform transform, kentity parent, extents_3d extents) {
	base->name = name;
	base->type = type;

	// Ensure the 'free' flag is off.
	FLAG_SET(base->flags, KENTITY_FLAG_FREE_BIT, false);

	// Default to serializable
	FLAG_SET(base->flags, KENTITY_FLAG_SERIALIZABLE_BIT, true);

	kentity entity = kentity_pack((u16)type, (u16)entity_index, 0, 0);

	bt_node_value val = {.u64 = entity};
	bt_node* lookup = u64_bst_insert(scene->name_lookup, name, val);
	if (!scene->name_lookup) {
		scene->name_lookup = lookup;
	}

	// Create a default transform if one is not provided.
	if (transform == KTRANSFORM_INVALID) {
		transform = ktransform_create(entity);
	} else {
		// Ensure this gets set.
		ktransform_user_set(transform, entity);
	}
	base->transform = transform;
	base->parent = parent;

	// Ensure extents are zeroed, update them later.
	base->extents = extents;

	// Add it as a child to the parent (if it exists)
	entity_add_child(scene, parent, entity);

	// If doing an inital load, add to the queued initial asset load count for types requiring it.
	// Used for async asset loads.
	if (scene->state == KSCENE_STATE_PARSING_CONFIG) {
		switch (type) {
		case KENTITY_TYPE_MODEL:
			notify_initial_load_entity_started(scene, entity);
			break;
		default:
			// NOTE: Intentionally a no-op
			break;
		}
	}

	// Add to BVH.
	aabb b = extents;
	if (extents_3d_is_zero(b)) {
		b = extents_3d_from_scalar(0.1f);
	}

	base->bvh_id = bvh_insert(&scene->bvh_tree, b, entity);

#if KOHI_DEBUG
	base->debug_data_index = INVALID_ID_U32;
#endif

	return entity;
}

static kentity init_base_entity(kscene* scene, base_entity* base, u16 entity_index, kname name, kentity_type type, ktransform transform, kentity parent) {
	return init_base_entity_with_extents(scene, base, entity_index, name, type, transform, parent, extents_3d_from_scalar(0.1f));
}

static void kmaterial_list_ensure_allocated(kmaterial_geometry_list* list) {
	if (list->count >= list->capacity) {
		u16 new_capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
		list->geometries = KREALLOC_TYPE_CARRAY(list->geometries, kgeometry_ref, list->capacity, new_capacity);
		list->capacity = new_capacity;
	}
}

static void kmaterial_map_ensure_allocated(kmaterial_to_geometry_map* map) {
	if (map->count >= map->capacity) {
		u16 new_capacity = (map->capacity == 0) ? 4 : map->capacity * 2;
		map->lists = KREALLOC_TYPE_CARRAY(map->lists, kmaterial_geometry_list, map->capacity, new_capacity);
		map->capacity = new_capacity;
	}
}

static kmaterial_geometry_list* get_or_create_material_geo_list(kmaterial_to_geometry_map* map, kmaterial material) {
	for (u16 i = 0; i < map->count; ++i) {
		if (map->lists[i].base_material == material) {
			return &map->lists[i];
		}
	}

	// A new one must be created.
	kmaterial_map_ensure_allocated(map);

	// Make sure to assign the material to it.
	kmaterial_geometry_list* list = &map->lists[map->count];
	list->base_material = material;
	map->count++;
	return list;
}

static void on_model_loaded(kmodel_instance instance, void* context) {
	kscene_model_load_context* typed_context = context;

	u16 entity_type_index = kentity_unpack_type_index(typed_context->entity);
	model_entity* entity = &typed_context->scene->models[entity_type_index];
	entity->model = instance;

	map_model_entity_geometries(typed_context->scene, typed_context->entity);

	if (typed_context->on_loaded_callback) {
		typed_context->on_loaded_callback(typed_context->entity, entity->model, typed_context->model_loaded_context);
	}

	// Notify the scene that a queued initial asset load occurred, if relevant.
	notify_initial_load_entity_complete(typed_context->scene, typed_context->entity);

	// Clean up the context.
	kfree(typed_context, sizeof(kscene_model_load_context), MEMORY_TAG_SCENE);
}

static void map_model_submesh_geometries(kscene* scene, kentity entity, u16 submesh_index, b8 winding_inverted, const kmaterial_instance* mat_inst) {

	u16 entity_type_index = kentity_unpack_type_index(entity);
	model_entity* typed_entity = &scene->models[entity_type_index];
	kmodel_system_state* model_state = engine_systems_get()->model_system;
	struct kmaterial_system_state* material_state = engine_systems_get()->material_system;
	const kgeometry* geo = kmodel_submesh_geometry_get_at(model_state, typed_entity->model.base_mesh, submesh_index);

	// TODO: Find a better way to classify this.
	b8 is_animated = geo->type == KGEOMETRY_TYPE_3D_SKINNED;

	// Choose the appropriate map.
	kmaterial_to_geometry_map* map = 0;
	b8 transparent = kmaterial_has_transparency_get(material_state, mat_inst->base_material);
	if (is_animated) {
		map = transparent ? &scene->transparent_animated_model_material_map : &scene->opaque_animated_model_material_map;
	} else {
		map = transparent ? &scene->transparent_static_model_material_map : &scene->opaque_static_model_material_map;
	}
	// The material-geometry list for this submesh's material.
	kmaterial_geometry_list* list = get_or_create_material_geo_list(map, mat_inst->base_material);

	// Search for an empty slot first.
	u16 array_index = INVALID_ID_U16;
	{
		u16 len = (u16)darray_length(scene->model_geometry_datas);
		for (u16 i = 0; i < len; ++i) {
			if (FLAG_GET(scene->model_geometry_datas[i].flags, KGEOMETRY_DATA_FLAG_FREE_BIT)) {
				// Found a free slot - use it.
				array_index = i;
				break;
			}
		}
		if (array_index == INVALID_ID_U16) {
			// No free entry found. Push an empty one. The index will be the former length of the array before the push.
			array_index = len;
			darray_push(scene->model_geometry_datas, (kgeometry_data){0});
			darray_push(scene->model_geometry_extents, (extents_3d){0});
		}
	}
	kgeometry_data* new_geo = &scene->model_geometry_datas[array_index];

	// Extract the required data into a new entry into the global flat list.
	new_geo->vertex_count = geo->vertex_count;
	new_geo->vertex_offset = geo->vertex_buffer_offset;
	new_geo->index_count = geo->index_count;
	new_geo->index_offset = geo->index_buffer_offset;
	new_geo->material_instance_id = mat_inst->instance_id;

	// Set flags.
	new_geo->flags = KGEOMETRY_DATA_FLAG_NONE;
	new_geo->flags = FLAG_SET(new_geo->flags, KGEOMETRY_DATA_FLAG_WINDING_INVERTED_BIT, winding_inverted);

	// Store the animated geometry extents.
	scene->model_geometry_extents[array_index] = geo->extents;

	// Add geometry reference to the material's list.
	// Search first for a free slot and use that, then fall back to adding a new one if need be.
	u16 ref_index = INVALID_ID_U16;
	{
		u16 len = list->count;
		for (u16 i = 0; i < len; ++i) {
			if (list->geometries[i].entity == KENTITY_INVALID && list->geometries[i].geometry_index == INVALID_ID_U16) {
				// Found a free slot - use it.
				ref_index = i;
				break;
			}
		}
		if (ref_index == INVALID_ID_U16) {
			ref_index = list->count;
			// Ensure there is enough space allocated.
			kmaterial_list_ensure_allocated(list);
			list->count++;
		}
	}

	// Setup the new index.
	kgeometry_ref* ref = &list->geometries[ref_index];
	ref->geometry_index = array_index; // NOTE: Links to the global array, not just this material's array.
	ref->entity = entity;
}

// maps animated model entity geometries by material. Should only be used for loaded entities.
static void map_model_entity_geometries(kscene* scene, kentity entity) {
	kmodel_system_state* model_state = engine_systems_get()->model_system;
	u16 entity_index = kentity_unpack_type_index(entity);
	model_entity* typed_entity = &scene->models[entity_index];

	base_entity* base = get_entity_base(scene, entity);

	base->extents.min = vec3_create(99999999.9f, 99999999.9f, 99999999.9f);
	base->extents.max = vec3_create(-99999999.9f, -99999999.9f, -99999999.9f);

	// Pre-determine winding for submodel. TODO: will need to listen for transform changes and update this data accordingly.
	mat4 model = ktransform_local_get(base->transform);
	f32 determinant = mat4_determinant(model);
	b8 winding_inverted = determinant < 0;

	// Iterate submodel.
	u16 submesh_count = 0;
	b8 is_animated = false;
	kmodel_submesh_count_get(model_state, typed_entity->model.base_mesh, &submesh_count);
	for (u16 g = 0; g < submesh_count; ++g) {

		const kgeometry* geo = kmodel_submesh_geometry_get_at(model_state, typed_entity->model.base_mesh, g);
		if (geo->type == KGEOMETRY_TYPE_3D_SKINNED) {
			is_animated = true;
		}

		// Take all the extents and combine them to get the outer extents for the entire thing.
		base->extents = extents_combine(base->extents, geo->extents);

		// Material instance for this submesh.
		const kmaterial_instance* mat_inst = kmodel_submesh_material_instance_get_at(model_state, typed_entity->model, g);

		// Map the submesh geometries to the material.
		map_model_submesh_geometries(scene, entity, g, winding_inverted, mat_inst);
	}
#if KOHI_DEBUG
	vec3 center = extents_3d_center(base->extents);

	// Debug data can be created at this point.
	vec3 size = size_from_extents_3d(base->extents);
	create_debug_data(
		scene,
		size,
		center,
		entity,
		KSCENE_DEBUG_DATA_TYPE_RECTANGLE,
		is_animated ? ENTITY_MODEL_ANIMATED_DEBUG_COLOUR : ENTITY_MODEL_STATIC_DEBUG_COLOUR,
		false,
		&base->debug_data_index);
#else
	if (is_animated) {
	}
#endif
}

static void unmap_model_entity_geometries(kscene* scene, kentity entity) {
	kmodel_system_state* model_state = engine_systems_get()->model_system;
	struct kmaterial_system_state* material_state = engine_systems_get()->material_system;
	u16 entity_index = kentity_unpack_type_index(entity);
	model_entity* typed_entity = &scene->models[entity_index];
	// Get a list of geometry references for this entity.
	// For each:
	// TODO: Should probably have some sort of reverse-mapping to be able to look this up quicker.
	// This is going to be somewhat slow since it can almost be guaranteed that these submeshes are
	// not organized in order by material, resulting in many lookups here.
	// However, this should only be used to dynamically unload mesh entities, as an entire scene unload
	// would traverse the maps/lists in order and release things in bulk, and in order.
	u16 mesh_count = 0;
	kmodel_submesh_count_get(model_state, typed_entity->model.base_mesh, &mesh_count);
	for (u16 i = 0; i < mesh_count; ++i) {
		const kmaterial_instance* mat_inst = kmodel_submesh_material_instance_get_at(model_state, typed_entity->model, i);

		// Choose the appropriate map.
		b8 transparent = kmaterial_has_transparency_get(material_state, mat_inst->base_material);
		kmaterial_to_geometry_map* map = transparent ? &scene->transparent_animated_model_material_map : &scene->opaque_animated_model_material_map;

		// The material-geometry list for this submesh's material.
		kmaterial_geometry_list* list = get_or_create_material_geo_list(map, mat_inst->base_material);

		// Look for geometry references within this material list.
		for (u16 r = 0; i < list->count; ++r) {
			kgeometry_ref* ref = &list->geometries[r];
			if (ref->entity == entity) {
				kgeometry_data* gd = &scene->model_geometry_datas[ref->geometry_index];

				KZERO_TYPE(gd, kgeometry_data);

				// Mark the entry in the animated_model array as free.
				gd->flags = FLAG_SET(gd->flags, KGEOMETRY_DATA_FLAG_FREE_BIT, true);
			}

			// -> Mark the geometry reference list entry as free.
			ref->entity = KENTITY_INVALID;
			ref->geometry_index = INVALID_ID_U16;
		}
	}
}

static b8 deserialize_entity(kson_object* obj, kentity parent, kscene* out_scene) {

	const char* type_str = 0;
	kentity_type entity_type = KENTITY_TYPE_NONE;
	if (kson_object_property_value_get_string(obj, "type", &type_str)) {
		entity_type = kentity_type_from_string(type_str);
		string_free(type_str);
	}

	kname entity_name = INVALID_KNAME;
	kson_object_property_value_get_string_as_kname(obj, "name", &entity_name);

	// Transform is optional, use a default one if one does not exist or was invalid.
	const char* transform_str = 0;
	ktransform t;
	if (kson_object_property_value_get_string(obj, "transform", &transform_str)) {
		if (!ktransform_from_string(transform_str, 0, &t)) {
			KWARN("Invalid transform provided, defaulting to identity transform.");
			t = ktransform_create(0);
		}
		string_free(transform_str);
	} else {
		t = ktransform_create(0);
	}

	// Parse tags.
	const char* tag_str = 0;
	u32 tag_count = 0;
	kstring_id* tags = 0;
	if (kson_object_property_value_get_string(obj, "tags", &tag_str)) {
		// Split string by commas, and build a list
		char** parts = darray_create(char*);
		u32 count = string_split(tag_str, ',', &parts, true, false, false);
		if (count) {
			tag_count = count;
			tags = KALLOC_TYPE_CARRAY(kstring_id, count);
			for (u32 i = 0; i < count; ++i) {
				tags[i] = kstring_id_create(parts[i]);
			}
		}
	}

	// The new entity.
	kentity new_entity = KENTITY_INVALID;

	switch (entity_type) {
	case KENTITY_TYPE_NONE:
		// Intentionally blank
		new_entity = kscene_add_entity(out_scene, entity_name, t, parent);
		break;
	case KENTITY_TYPE_MODEL: {
		kname asset_name = INVALID_KNAME;
		if (!kson_object_property_value_get_string_as_kname(obj, "asset_name", &asset_name)) {
			KERROR("Failed to deserialize model entity - missing asset_name");
			return false;
		}

		kname package_name = INVALID_KNAME;
		kson_object_property_value_get_string_as_kname(obj, "asset_package_name", &package_name);

		// Add the model to the scene.
		new_entity = kscene_add_model(out_scene, entity_name, t, parent, asset_name, package_name, 0, 0);
	} break;
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
		// FIXME: Implement this
		KASSERT_MSG(false, "not yet implemented");
		return false;
		/* break; */
		// FIXME: kscene_add_heightmap_terrain()
	case KENTITY_TYPE_WATER_PLANE: {
		i64 size_i64 = 128;
		kson_object_property_value_get_int(obj, "size", &size_i64);
		// TODO: water material asset_name/asset_package_name

		new_entity = kscene_add_water_plane(out_scene, entity_name, t, parent, (f32)size_i64);
	} break;
	case KENTITY_TYPE_AUDIO_EMITTER: {
		// required
		kname asset_name = INVALID_KNAME;
		if (!kson_object_property_value_get_string_as_kname(obj, "asset_name", &asset_name)) {
			KERROR("An asset_name is required to load an audio asset for an audio emitter!");
			return false;
		}
		// optional, defaults to application package.
		kname asset_package_name = INVALID_KNAME;
		kson_object_property_value_get_string_as_kname(obj, "asset_package_name", &asset_package_name);

		f32 inner_radius = 1.0f;
		f32 outer_radius = 2.0f;
		f32 volume = 1.0f;
		f32 falloff = 1.0f;
		kson_object_property_value_get_float(obj, "inner_radius", &inner_radius);
		kson_object_property_value_get_float(obj, "outer_radius", &outer_radius);
		kson_object_property_value_get_float(obj, "volume", &volume);
		kson_object_property_value_get_float(obj, "falloff", &falloff);

		b8 is_streaming = false;
		b8 is_looping = false;
		kson_object_property_value_get_bool(obj, "is_streaming", &is_streaming);
		kson_object_property_value_get_bool(obj, "is_looping", &is_looping);

		new_entity = kscene_add_audio_emitter(out_scene, entity_name, t, parent, inner_radius, outer_radius, volume, falloff, is_looping, is_streaming, asset_name, asset_package_name);
	} break;
	case KENTITY_TYPE_VOLUME: {
		// volume type
		kscene_volume_type vol_type = KSCENE_VOLUME_TYPE_TRIGGER;
		const char* vol_type_str = 0;
		kson_object_property_value_get_string(obj, "volume_type", &vol_type_str);
		if (vol_type_str) {
			vol_type = scene_volume_type_from_string(vol_type_str);
			string_free(vol_type_str);
		}

		// Shape type
		kshape_type shape_type = KSHAPE_TYPE_SPHERE;
		const char* shape_type_str = 0;
		kson_object_property_value_get_string(obj, "shape_type", &shape_type_str);
		if (shape_type_str) {
			shape_type = kshape_type_from_string(shape_type_str);
			string_free(shape_type_str);
		}

		// Volume shape properties
		kcollision_shape shape = {
			.shape_type = shape_type,
		};
		switch (shape_type) {
		case KSHAPE_TYPE_SPHERE: {
			// Radius
			shape.radius = 1.0f;
			kson_object_property_value_get_float(obj, "radius", &shape.radius);
		} break;
		case KSHAPE_TYPE_RECTANGLE: {
			// extents
			shape.extents = vec3_zero();
			kson_object_property_value_get_vec3(obj, "extents", &shape.extents);
		} break;
		}

		// Hit shape tags
		const char* hit_tag_str = 0;
		u32 hit_shape_tag_count = 0;
		kstring_id* hit_shape_tags = 0;
		if (kson_object_property_value_get_string(obj, "hit_shape_tags", &hit_tag_str)) {
			// Split string by commas, and build a list
			char** parts = darray_create(char*);
			u32 count = string_split(hit_tag_str, ',', &parts, true, false, false);
			if (count) {
				hit_shape_tag_count = count;
				hit_shape_tags = KALLOC_TYPE_CARRAY(kstring_id, count);
				for (u32 i = 0; i < count; ++i) {
					hit_shape_tags[i] = kstring_id_create(parts[i]);
				}
			}
			string_cleanup_split_darray(parts);
			darray_destroy(parts);
			string_free(hit_tag_str);
		}

		const char* on_enter_command = 0;
		kson_object_property_value_get_string(obj, "on_enter", &on_enter_command);

		const char* on_leave_command = 0;
		kson_object_property_value_get_string(obj, "on_leave", &on_leave_command);

		const char* on_tick_command = 0;
		kson_object_property_value_get_string(obj, "on_tick", &on_tick_command);

		new_entity = kscene_add_volume(out_scene, entity_name, t, parent, vol_type, shape, hit_shape_tag_count, hit_shape_tags, on_enter_command, on_leave_command, on_tick_command);
		if (hit_shape_tags) {
			KFREE_TYPE_CARRAY(hit_shape_tags, kstring_id, hit_shape_tag_count);
		}

		if (on_enter_command) {
			string_free(on_enter_command);
		}
		if (on_leave_command) {
			string_free(on_leave_command);
		}
		if (on_tick_command) {
			string_free(on_tick_command);
		}
	} break;
	case KENTITY_TYPE_HIT_SHAPE: {
		// Shape type
		kshape_type shape_type = KSHAPE_TYPE_SPHERE;
		const char* shape_type_str = 0;
		kson_object_property_value_get_string(obj, "shape_type", &shape_type_str);
		if (shape_type_str) {
			shape_type = kshape_type_from_string(shape_type_str);
			string_free(shape_type_str);
		}

		kcollision_shape shape = {
			.shape_type = shape_type,
		};
		switch (shape_type) {
		case KSHAPE_TYPE_SPHERE: {
			// Radius
			shape.radius = 1.0f;
			kson_object_property_value_get_float(obj, "radius", &shape.radius);
		} break;
		case KSHAPE_TYPE_RECTANGLE: {
			shape.extents = vec3_zero();
			kson_object_property_value_get_vec3(obj, "extents", &shape.extents);
		} break;
		}

		new_entity = kscene_add_hit_shape(out_scene, entity_name, t, parent, shape, tag_count, tags);
	} break;
	case KENTITY_TYPE_POINT_LIGHT: {
		vec4 colour = vec4_one();
		kson_object_property_value_get_vec4(obj, "colour", &colour);

		f32 linear = 0.35f;
		kson_object_property_value_get_float(obj, "linear", &linear);

		f32 quadratic = 0.44f;
		kson_object_property_value_get_float(obj, "quadratic", &quadratic);

		new_entity = kscene_add_point_light(out_scene, entity_name, t, parent, vec3_from_vec4(colour), linear, quadratic);
	} break;

	case KENTITY_TYPE_SPAWN_POINT: {
		f32 radius = 1.0f;
		kson_object_property_value_get_float(obj, "radius", &radius);

		new_entity = kscene_add_spawn_point(out_scene, entity_name, t, parent, radius);
	} break;

	case KENTITY_TYPE_COUNT:
	case KENTITY_TYPE_INVALID:
		KWARN("Invalid entity type found, no type-specific properties will be loaded.");
		break;
	}

	// Ensure the entity was created.
	KASSERT_DEBUG_MSG(new_entity != KENTITY_INVALID, "new_entity not created! Check logic.");

	// Recurse children if there are any.
	kson_array children_array = {0};
	if (kson_object_property_value_get_array(obj, "children", &children_array)) {
		u32 array_len = 0;
		if (!kson_array_element_count_get(&children_array, &array_len)) {
			KWARN("Could not retrieve length of children array. Skipping.");
		} else {
			for (u32 i = 0; i < array_len; ++i) {
				kson_object child = {0};
				if (kson_array_element_value_get_object(&children_array, i, &child)) {
					if (!deserialize_entity(&child, new_entity, out_scene)) {
						KERROR("Failed to deserialize child entity.");
						return false;
					}
				}
			}
		}
	}

	return true;
}

static b8 deserialize(const char* file_content, kscene* out_scene) {
#if KOHI_DEBUG
	if (!file_content || !out_scene) {
		KERROR("%s - Cannot deserialize without file_content and out_scene.", __FUNCTION__);
		return false;
	}
#endif

	kson_tree tree = {0};
	if (!kson_tree_from_string(file_content, &tree)) {
		KERROR("Failed to parse kscene.");
		return false;
	}

	i64 version_i64 = 0;
	if (!kson_object_property_value_get_int(&tree.root, "version", &version_i64)) {
		KERROR("Missing root property 'version'.");
		return false;
	}
	if (version_i64 != 1) {
		KERROR("%s - Invalid kscene version: %lli", __FUNCTION__, version_i64);
		return false;
	}
	KASSERT_DEBUG(version_i64 < U8_MAX);

	out_scene->version = (u8)version_i64;

	// name
	if (!kson_object_property_value_get_string(&tree.root, "name", &out_scene->name)) {
		KERROR("%s - Missing kscene name", __FUNCTION__);
		return false;
	}

	// Desc - optional
	kson_object_property_value_get_string(&tree.root, "description", &out_scene->description);

	// Skybox is optional
	kson_object_property_value_get_string_as_kname(&tree.root, "skybox_asset_name", &out_scene->skybox_asset_name);
	kson_object_property_value_get_string_as_kname(&tree.root, "skybox_asset_package_name", &out_scene->skybox_asset_package_name);
	if (out_scene->skybox_asset_name != INVALID_KNAME) {
		// Load it on up.
		skybox_config sbc = {
			// FIXME: Change skybox config to accept asset_name and package_name
			.cubemap_name = out_scene->skybox_asset_name};
		skybox_create(sbc, &out_scene->sb);
		skybox_initialize(&out_scene->sb);
		skybox_load(&out_scene->sb);
	}
	out_scene->default_irradiance_texture = texture_acquire_sync(kname_create(DEFAULT_CUBE_TEXTURE_NAME));

	// Directional lights are optional, with fallbacks.
	vec4 dir_colour_v4 = (vec4){1, 1, 1, 1};
	kson_object_property_value_get_vec4(&tree.root, "directional_light_colour", &dir_colour_v4);
	// Check other light/shadow properties, or use defaults if they do not exist.

	vec4 dir_direction_v4 = (vec4){-0.577350f, -0.577350f, 0.577350f, 0.000000f};
	kson_object_property_value_get_vec4(&tree.root, "directional_light_direction", &dir_direction_v4);

	out_scene->directional_light = directional_light_create(
		engine_systems_get()->light_system,
		vec3_from_vec4(dir_direction_v4),
		vec3_from_vec4(dir_colour_v4));

	// Shadow mapping properties. Not required, as there are defaults.
	out_scene->shadow_dist = DEFAULT_SHADOW_DIST;
	kson_object_property_value_get_float(&tree.root, "shadow_distance", &out_scene->shadow_dist);

	out_scene->shadow_fade_dist = DEFAULT_SHADOW_FADE_DIST;
	kson_object_property_value_get_float(&tree.root, "shadow_fade_distance", &out_scene->shadow_fade_dist);

	out_scene->shadow_split_mult = DEFAULT_SHADOW_SPLIT_MULT;
	kson_object_property_value_get_float(&tree.root, "shadow_split_mult", &out_scene->shadow_split_mult);

	out_scene->shadow_bias = DEFAULT_SHADOW_BIAS;
	kson_object_property_value_get_float(&tree.root, "shadow_bias", &out_scene->shadow_bias);

	// Fog settings.
	out_scene->fog_colour = (colour4){1, 1, 1, 1};
	kson_object_property_value_get_vec4(&tree.root, "fog_colour", &out_scene->fog_colour);
	out_scene->fog_near = 10.0f;
	kson_object_property_value_get_float(&tree.root, "fog_near", &out_scene->fog_near);
	out_scene->fog_far = 1000.0f;
	kson_object_property_value_get_float(&tree.root, "fog_far", &out_scene->fog_far);

	// Parse entities.
	kson_array entities = {0};
	if (kson_object_property_value_get_array(&tree.root, "entities", &entities)) {
		u32 root_entity_count = 0;
		if (kson_array_element_count_get(&entities, &root_entity_count)) {
			for (u32 i = 0; i < root_entity_count; ++i) {
				kson_object root_entity = {0};
				kson_array_element_value_get_object(&entities, i, &root_entity);
				if (!deserialize_entity(&root_entity, KENTITY_INVALID, out_scene)) {
					// Bleat about it, but move on.
					KERROR("Root entity failed deserialization. See logs for details.");
				}
			}
		}
	}

	out_scene->state = KSCENE_STATE_LOADING;

	kson_tree_cleanup(&tree);

	return true;
}

static b8 entity_serialize_r(const kscene* scene, kentity entity, kson_object* s_obj) {
	KASSERT(s_obj);

	*s_obj = kson_object_create();

	base_entity* base = get_entity_base((kscene*)scene, entity);

	// Check if serializable and only complete this if so.
	if (!FLAG_GET(base->flags, KENTITY_FLAG_SERIALIZABLE_BIT)) {
		return false;
	}

	// Base properties.
	if (base->name != INVALID_KNAME) {
		kson_object_value_add_kname_as_string(s_obj, "name", base->name);
	}
	if (base->type != KENTITY_TYPE_NONE) {
		kson_object_value_add_string(s_obj, "type", kentity_type_to_string(base->type));
	}

	if (!ktransform_is_identity(base->transform)) {
		kson_object_value_add_string(s_obj, "transform", ktransform_to_string(base->transform));
	}

	if (base->tag_count && base->tags) {
		kson_array tags_arr = kson_array_create();
		for (u32 t = 0; t < base->tag_count; ++t) {
			kson_array_value_add_kstring_id_as_string(&tags_arr, base->tags[t]);
		}
		kson_object_value_add_array(s_obj, "tags", tags_arr);
	}

	u16 type_index = kentity_unpack_type_index(entity);

	switch (base->type) {
	case KENTITY_TYPE_NONE:
		// NOTE: Nothing more to do here since this is just a base entity.
		break;
	case KENTITY_TYPE_MODEL: {
		model_entity* typed = &scene->models[type_index];
		kson_object_value_add_kname_as_string(s_obj, "asset_name", typed->asset_name);
		kson_object_value_add_kname_as_string(s_obj, "asset_package_name", typed->package_name);
	} break;
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
		// FIXME: Implement this
		KASSERT_MSG(false, "not yet implemented");
		break;
	case KENTITY_TYPE_WATER_PLANE: {
		water_plane_entity* typed = &scene->water_planes[type_index];
		kson_object_value_add_int(s_obj, "size", typed->size);
	} break;
	case KENTITY_TYPE_AUDIO_EMITTER: {
		audio_emitter_entity* typed = &scene->audio_emitters[type_index];
		kson_object_value_add_kname_as_string(s_obj, "asset_name", typed->asset_name);
		kson_object_value_add_kname_as_string(s_obj, "asset_package_name", typed->package_name);
		kson_object_value_add_float(s_obj, "inner_radius", typed->inner_radius);
		kson_object_value_add_float(s_obj, "outer_radius", typed->outer_radius);
		kson_object_value_add_float(s_obj, "falloff", typed->falloff);
		kson_object_value_add_float(s_obj, "volume", typed->volume);
		kson_object_value_add_boolean(s_obj, "is_streaming", typed->is_streaming);
		kson_object_value_add_boolean(s_obj, "is_looping", typed->is_looping);
	} break;
	case KENTITY_TYPE_VOLUME: {
		volume_entity* typed = &scene->volumes[type_index];
		kson_object_value_add_string(s_obj, "volume_type", scene_volume_type_to_string(typed->type));
		kson_object_value_add_string(s_obj, "shape_type", kshape_type_to_string(typed->shape.shape_type));
		if (typed->shape.shape_type == KSHAPE_TYPE_SPHERE) {
			kson_object_value_add_float(s_obj, "radius", typed->shape.radius);
		} else if (typed->shape.shape_type == KSHAPE_TYPE_RECTANGLE) {
			kson_object_value_add_vec3(s_obj, "extents", typed->shape.extents);
		} else {
			// FIXME: Implement more shape types.
			KFATAL("not implemented");
		}

		const char* hit_shape_tags = kstring_id_join(typed->hit_shape_tags, typed->hit_shape_tag_count, ',');
		kson_object_value_add_string(s_obj, "hit_shape_tags", hit_shape_tags);
		string_free(hit_shape_tags);

		if (typed->on_enter_command) {
			kson_object_value_add_string(s_obj, "on_enter", typed->on_enter_command);
		}
		if (typed->on_leave_command) {
			kson_object_value_add_string(s_obj, "on_leave", typed->on_leave_command);
		}
		if (typed->on_tick_command) {
			kson_object_value_add_string(s_obj, "on_tick", typed->on_tick_command);
		}
	} break;
	case KENTITY_TYPE_HIT_SHAPE: {
		hit_shape_entity* typed = &scene->hit_shapes[type_index];
		kson_object_value_add_string(s_obj, "shape_type", kshape_type_to_string(typed->shape.shape_type));
		if (typed->shape.shape_type == KSHAPE_TYPE_SPHERE) {
			kson_object_value_add_float(s_obj, "radius", typed->shape.radius);
		} else if (typed->shape.shape_type == KSHAPE_TYPE_RECTANGLE) {
			kson_object_value_add_vec3(s_obj, "extents", typed->shape.extents);
		} else {
			// FIXME: Implement more shape types.
			KFATAL("not implemented");
		}
	} break;
	case KENTITY_TYPE_POINT_LIGHT: {
		point_light_entity* typed = &scene->point_lights[type_index];
		kson_object_value_add_vec4(s_obj, "colour", vec4_from_vec3(typed->colour, 1.0f));
		kson_object_value_add_float(s_obj, "linear", typed->linear);
		kson_object_value_add_float(s_obj, "quadratic", typed->quadratic);
	} break;

	case KENTITY_TYPE_SPAWN_POINT: {
		spawn_point_entity* typed = &scene->spawn_points[type_index];
		kson_object_value_add_float(s_obj, "radius", typed->radius);
	} break;
	case KENTITY_TYPE_COUNT:
	case KENTITY_TYPE_INVALID:
		// NOTE: these don't do anything. Perhaps should error here.
		KWARN("Entity type of 'count' or 'invalid' don't have properties to be serialized.");
		break;
	}

	// Recurse children.
	kson_array children_array = kson_array_create();

	u32 child_count = base->children ? darray_length(base->children) : 0;
	for (u32 i = 0; i < child_count; ++i) {
		kson_object child_obj = {0};
		if (entity_serialize_r(scene, base->children[i], &child_obj)) {
			kson_array_value_add_object(&children_array, child_obj);
		}
	}

	if (child_count) {
		kson_object_value_add_array(s_obj, "children", children_array);
	}

	return true;
}

const char* kscene_serialize(const kscene* scene) {
	kson_tree tree = {0};
	// The root of the tree.
	tree.root = kson_object_create();

	kson_object_value_add_int(&tree.root, "version", kSCENE_CURRENT_VERSION);
	kson_object_value_add_string(&tree.root, "name", scene->name);
	if (scene->description) {
		kson_object_value_add_string(&tree.root, "description", scene->description);
	}

	kson_object_value_add_kname_as_string(&tree.root, "skybox_asset_name", scene->skybox_asset_name);
	kson_object_value_add_kname_as_string(&tree.root, "skybox_asset_package_name", scene->skybox_asset_package_name);

	vec3 directional_light_colour = directional_light_get_colour(engine_systems_get()->light_system, scene->directional_light);
	vec3 directional_light_direction = directional_light_get_direction(engine_systems_get()->light_system, scene->directional_light);

	kson_object_value_add_vec4(&tree.root, "directional_light_colour", vec4_from_vec3(directional_light_colour, 1.0f));
	kson_object_value_add_vec4(&tree.root, "directional_light_direction", vec4_from_vec3(directional_light_direction, 0.0f));

	kson_object_value_add_float(&tree.root, "shadow_distance", scene->shadow_dist);
	kson_object_value_add_float(&tree.root, "shadow_fade_distance", scene->shadow_fade_dist);
	kson_object_value_add_float(&tree.root, "shadow_split_mult", scene->shadow_split_mult);
	kson_object_value_add_float(&tree.root, "shadow_bias", scene->shadow_bias);

	// Fog settings.
	kson_object_value_add_vec4(&tree.root, "fog_colour", scene->fog_colour);
	kson_object_value_add_float(&tree.root, "fog_near", scene->fog_near);
	kson_object_value_add_float(&tree.root, "fog_far", scene->fog_far);

	kson_array entities_array = kson_array_create();

	u32 entity_count = darray_length(scene->root_entities);
	for (u32 i = 0; i < entity_count; ++i) {
		kson_object s_obj = {0};
		if (entity_serialize_r(scene, scene->root_entities[i], &s_obj)) {
			kson_array_value_add_object(&entities_array, s_obj);
		}
	}

	kson_object_value_add_array(&tree.root, "entities", entities_array);

	const char* output = kson_tree_to_string(&tree);
	kson_tree_cleanup(&tree);
	return output;
}

b8 kscene_hf_terrain_save(const struct kscene* scene) {
	if (!scene) {
		return false;
	}
	if (!scene->hf.vertices) {
		// Nothing to save. Return true, as this isn't always an error?
		return true;
	}

	kasset_hf_terrain asset = {
		.vertex_count = scene->hf.vertex_count,
		.block_count_x = scene->hf.block_count_x,
		.block_count_z = scene->hf.block_count_z,
		.material_count = scene->hf.material_count,
		.version = 0x0001};

	asset.vertices = KALLOC_TYPE_CARRAY(kasset_hf_terrain_vertex, asset.vertex_count);
	for (u32 i = 0; i < asset.vertex_count; ++i) {
		asset.vertices[i].y_offset = scene->hf.vertices[i].position.y;
	}

	u32 block_count = asset.block_count_x * asset.block_count_z;
	asset.blocks = KALLOC_TYPE_CARRAY(kasset_hf_terrain_block, block_count);
	for (u32 i = 0; i < block_count; ++i) {
		hf_block* block = &scene->hf.blocks[i];
		kasset_hf_terrain_block* asset_block = &asset.blocks[i];

		asset_block->splatmap_size_x = HF_TERRAIN_SPLATMAP_RESOLUTION;
		asset_block->splatmap_size_y = HF_TERRAIN_SPLATMAP_RESOLUTION;
		kcopy_memory(asset_block->splatmap_pixels, block->splatmap_pixels, KASSET_HF_SPLAT_RES * KASSET_HF_SPLAT_RES * 4);

		for (u32 c = 0; c < 256; ++c) {
			hf_chunk* chunk = &block->chunks[c];
			kasset_hf_terrain_chunk* asset_chunk = &asset_block->chunks[c];

			for (u8 m = 0; m < HF_TERRAIN_CHUNK_MAX_MATERIALS; ++m) {
				asset_chunk->material_indices[m] = chunk->material_indices[m];
			}
		}
	}

	asset.material_names = KALLOC_TYPE_CARRAY(const char*, asset.material_count);
	asset.material_map_names = KALLOC_TYPE_CARRAY(kasset_hf_terrain_material_map_names, asset.material_count);
	asset.materials = KALLOC_TYPE_CARRAY(kasset_hf_terrain_material, asset.material_count);
	for (u32 i = 0; i < scene->hf.material_count; ++i) {
		asset.material_map_names[i].albedo_str = kname_string_get(scene->hf.albedo_asset_names[i]);
		asset.material_map_names[i].normal_str = kname_string_get(scene->hf.normal_asset_names[i]);
		asset.material_map_names[i].mra_str = kname_string_get(scene->hf.mra_asset_names[i]);
		asset.material_names[i] = kstring_id_string_get(scene->hf.material_names[i]);
	}

	u64 asset_size = 0;
	void* data = kasset_hf_terrain_serialize(&asset, &asset_size);

	return asset_system_write_binary(engine_systems_get()->asset_state, INVALID_KNAME, kname_create(scene->hf_terrain_asset_name), asset_size, data);
}

static void kscene_dump_hierarchy_entity_r(const kscene* scene, kentity entity, u32 depth) {
	char spacing[65] = {0};
	kzero_memory(spacing, sizeof(spacing));

	depth = KMIN(depth, 64);
	for (u32 i = 0; i < depth; ++i) {
		spacing[i] = ' ';
	}

	base_entity* base = get_entity_base((kscene*)scene, entity);
	KINFO("%s%k", spacing, base->name);

	u32 child_count = darray_length(base->children);
	for (u32 i = 0; i < child_count; ++i) {
		kscene_dump_hierarchy_entity_r(scene, base->children[i], depth + 1);
	}
}

void kscene_dump_hierarchy(const kscene* scene) {
	u32 entity_count = darray_length(scene->root_entities);
	for (u32 i = 0; i < entity_count; ++i) {
		kscene_dump_hierarchy_entity_r(scene, scene->root_entities[i], 0);
	}
}

kscene_hierarchy_node kscene_get_hierarchy_internal_r(const struct kscene* scene, kentity parent) {
	base_entity* base = get_entity_base((kscene*)scene, parent);

	u32 child_count = darray_length(base->children);

	kscene_hierarchy_node node = {
		.entity = parent,
		.child_count = child_count,
		.children = child_count ? KALLOC_TYPE_CARRAY(kscene_hierarchy_node, child_count) : KNULL};

	for (u32 i = 0; i < child_count; ++i) {
		node.children[i] = kscene_get_hierarchy_internal_r(scene, base->children[i]);
	}

	return node;
}

kscene_hierarchy_node* kscene_get_hierarchy(const struct kscene* scene, u32* out_count) {
	u32 len = darray_length(scene->root_entities);
	*out_count = len;
	kscene_hierarchy_node* nodes = KNULL;
	if (len) {
		nodes = KALLOC_TYPE_CARRAY(kscene_hierarchy_node, len);

		for (u32 i = 0; i < len; ++i) {
			nodes[i] = kscene_get_hierarchy_internal_r(scene, scene->root_entities[i]);
		}
	}

	return nodes;
}

static void cleanup_hierarchy_node_r(kscene_hierarchy_node* node) {
	if (node->child_count && node->children) {
		for (u32 i = 0; i < node->child_count; ++i) {
			cleanup_hierarchy_node_r(&node->children[i]);
		}
		KFREE_TYPE_CARRAY(node->children, kscene_hierarchy_node, node->child_count);
	}
}

void kscene_cleanup_hierarchy(kscene_hierarchy_node* nodes, u32 count) {
	for (u32 i = 0; i < count; ++i) {
		cleanup_hierarchy_node_r(&nodes[i]);
	}
	KFREE_TYPE_CARRAY(nodes, kscene_hierarchy_node, count);
}

static void notify_initial_load_entity_started(kscene* scene, kentity entity) {
	// Only counts as initial load if currently in the 'loading' state.
	if (scene->state == KSCENE_STATE_LOADING || scene->state == KSCENE_STATE_PARSING_CONFIG) {
		scene->queued_initial_asset_loads++;
		KTRACE("(+) Scene queued initial asset loads is now: %u", scene->queued_initial_asset_loads);
	}
}

// Handles notifications of initial asset load completion and updates counts.
static void notify_initial_load_entity_complete(kscene* scene, kentity entity) {
	if (scene->state == KSCENE_STATE_LOADING || scene->state == KSCENE_STATE_PARSING_CONFIG) {
		scene->queued_initial_asset_loads--;
		KTRACE("(-) Scene queued initial asset loads is now: %u", scene->queued_initial_asset_loads);
	}
}

static base_entity* get_entity_base(kscene* scene, kentity entity) {
	if (entity == KENTITY_INVALID) {
		return KNULL;
	}
	kentity_type type = kentity_unpack_type(entity);
	u16 typed_index = kentity_unpack_type_index(entity);
	switch (type) {
	case KENTITY_TYPE_NONE:
		return &scene->bases[typed_index];
	case KENTITY_TYPE_MODEL:
		return &scene->models[typed_index].base;
	case KENTITY_TYPE_POINT_LIGHT:
		return &scene->point_lights[typed_index].base;
	case KENTITY_TYPE_VOLUME:
		return &scene->volumes[typed_index].base;
	case KENTITY_TYPE_HIT_SHAPE:
		return &scene->hit_shapes[typed_index].base;
	case KENTITY_TYPE_WATER_PLANE:
		return &scene->water_planes[typed_index].base;
	case KENTITY_TYPE_AUDIO_EMITTER:
		return &scene->audio_emitters[typed_index].base;
	case KENTITY_TYPE_SPAWN_POINT:
		return &scene->spawn_points[typed_index].base;
	case KENTITY_TYPE_HEIGHTMAP_TERRAIN:
		KERROR("%s - heightmap_terrain not yet implemented");
		return KNULL;
	default:
		return KNULL;
	}
}

#if KOHI_DEBUG
static void create_debug_data(kscene* scene, vec3 size, vec3 center, kentity entity, kscene_debug_data_type type, colour4 colour, b8 ignore_scale, u32* out_debug_data_index) {

	// Find free index.
	u32 index = INVALID_ID;
	kscene_debug_data* data = 0;
	u32 len = darray_length(scene->debug_datas);
	for (u32 i = 0; i < len; ++i) {
		// Determine if "free"
		data = &scene->debug_datas[i];
		if (data->type == KSCENE_DEBUG_DATA_TYPE_NONE) {
			// found a free one, use it.
			index = i;
			break;
		}
	}
	if (index == INVALID_ID_U32) {
		index = len;
		darray_push(scene->debug_datas, (kscene_debug_data){0});
		data = &scene->debug_datas[index];
	}

	data->owner = entity;
	data->model = mat4_identity();
	data->colour = colour;
	data->type = type;
	data->ignore_scale = ignore_scale;
	switch (data->type) {
	case KSCENE_DEBUG_DATA_TYPE_NONE:
		KWARN("Trying to create debug data of type none. Don't do that, ya dingus! Creating a box instead.");
		// Note: intentional fall-through.
	case KSCENE_DEBUG_DATA_TYPE_RECTANGLE:
		data->geometry = geometry_generate_line_box3d_typed(size, 0, KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY, center);
		break;
	case KSCENE_DEBUG_DATA_TYPE_SPHERE: {
		f32 radius = KMAX(size.x, KMAX(size.y, size.z));
		// NOTE: hardcode debug sphere resolution.
		data->geometry = geometry_generate_line_sphere3d_typed(radius, 16, 0, KGEOMETRY_TYPE_3D_STATIC_POSITION_ONLY);
	} break;
	}

	// Send the geometry off to the renderer to be uploaded to the GPU.
	if (!renderer_geometry_upload(&data->geometry)) {
		KERROR("Error uploading debug geometry.");
	}
	data->geometry.generation++;
	*out_debug_data_index = index;
}

static kscene_debug_data_type debug_type_from_shape_type(kshape_type type) {
	switch (type) {
	case KSHAPE_TYPE_SPHERE:
		return KSCENE_DEBUG_DATA_TYPE_SPHERE;
	case KSHAPE_TYPE_RECTANGLE:
		return KSCENE_DEBUG_DATA_TYPE_RECTANGLE;
	}
}

#endif
