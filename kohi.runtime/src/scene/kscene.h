#pragma once

#include <core/frame_data.h>
#include <defines.h>
#include <kresources/kresource_types.h>
#include <math/math_types.h>
#include <strings/kname.h>

typedef enum kscene_state {
    /** @brief created, but nothing more. This is also the state when a scene is destroyed. */
    KSCENE_STATE_UNINITIALIZED,
    /** @brief Configuration parsed and initial hierarchy is setup (but not loaded). */
    KSCENE_STATE_INITIALIZED,
    /** @brief In the process of loading the hierarchy. */
    KSCENE_STATE_LOADING,
    /** @brief Everything is loaded, ready to play. */
    KSCENE_STATE_LOADED,
    /** @brief In the process of unloading, not ready to play. */
    KSCENE_STATE_UNLOADING,
    /** @brief Unloaded and ready to be destroyed.*/
    KSCENE_STATE_UNLOADED
} kscene_state;

typedef enum kscene_flag_bits {
    KSCENE_FLAG_NONE = 0,
    /*
     * @brief Indicates if the scene can be saved once modified
     * (i.e. read-only would be used for runtime, writing would
     * be used in editor, etc.)
     */
    KSCENE_FLAG_READONLY_BIT = 0x0001
} kscene_flag_bits;

typedef u32 kscene_flags;

typedef struct kscene_transform_data {
    u32 allocated_count;
    vec3* positions;
    quat* rotations;
    vec3* scales;
    // Computed world transform.
    mat4* world_matrices;

    // Tracker for dirty transforms by id.
    u32* dirty_ids;
} kscene_transform_data;

typedef struct kscene_node_hierarchy_data {
    u32 allocated_count;
    kname* names;

    // Unique id used to match up with handles.
    u64* uniqueids;
    // Parent node id, or INVALID_ID if root.
    u32* parent_ids;
    // Id of the first child, or INVALID_ID if no children.
    u32* first_child_ids;
    // Id of the next sibling, or INVALID_ID if no next sibling.
    u32* next_sibling_ids;
    // Id of the transform if there is one, otherwise INVALID_ID.
    u32* transform_ids;
} kscene_node_hierarchy_data;

typedef struct kscene_node_tag_data {
    u32 allocated_count;
    // Tag names
    kname* names;
    // Tracker for nodes using tags by id. One array of ids per tag.
    u32** node_ids;
} kscene_node_tag_data;

typedef enum kscene_known_attachment_type {
    KSCENE_KNOWN_ATTACHMENT_TYPE_UNKNOWN = 0x00,
    KSCENE_KNOWN_ATTACHMENT_TYPE_STATIC_MESH = 0x01,
    KSCENE_KNOWN_ATTACHMENT_TYPE_SKELETAL_MESH,
    KSCENE_KNOWN_ATTACHMENT_TYPE_SKYBOX,
    KSCENE_KNOWN_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN,
    KSCENE_KNOWN_ATTACHMENT_TYPE_WATER_PLANE,
    KSCENE_KNOWN_ATTACHMENT_TYPE_LIGHT_DIRECTIONAL,
    KSCENE_KNOWN_ATTACHMENT_TYPE_LIGHT_POINT,
    KSCENE_KNOWN_ATTACHMENT_TYPE_LIGHT_SPOT,
    KSCENE_KNOWN_ATTACHMENT_TYPE_AUDIO_EMITTER,
    KSCENE_KNOWN_ATTACHMENT_TYPE_PARTICLE_EMITTER,
    KSCENE_KNOWN_ATTACHMENT_TYPE_BILLBOARD,
    KSCENE_KNOWN_ATTACHMENT_TYPE_VOLUME,
    KSCENE_KNOWN_ATTACHMENT_TYPE_HIT_SPHERE,

    // The count of known attachment types.
    KSCENE_KNOWN_ATTACHMENT_TYPE_COUNT,

    // NOTE: The space between count and user type is reserved for future known types

    // Anything beyond this is a user type. User code should create an enum starting at 0x21.
    KSCENE_KNOWN_ATTACHMENT_TYPE_USER = 0x20
} kscene_known_attachment_type;

// Attachment type, to be casted to either a known type or user type.
typedef u32 kscene_attachment_type;

struct kscene;
// Opaque pointer to an internal state for an attachment type.
struct kscene_attachment_type_system_state;

typedef b8 (*PFN_kscene_attachment_type_update)(struct kscene_attachment_type_system_state* type_system_state, struct kscene* scene, const struct frame_data* p_frame_data);

// Represents all attachments in the scene, regardless of type.
typedef struct kscene_attachment_data {
    u32 allocated_count;

    // Attachment name.
    kname* names;
    // Owning node id.
    u32* owner_node_ids;

    // Pointer to system state for a given type.
    struct kscene_attachment_type_system_state* type_system_states;

} kscene_attachment_data;

typedef struct kscene_attachment_tag_data {
    u32 allocated_count;
    // Tag names
    kname* names;
    // Tracker for attachments using tags by id. One array of ids per tag.
    u32** node_ids;
} kscene_attachment_tag_data;

typedef struct kscene_attachment_type_data {
    u32 allocated_count;
    // Names for attachment types. Primarily for debugging purposes.
    kname* names;
    // Ids of attachments associated with a type.
    u32** attachment_ids;
} kscene_attachment_type_data;

typedef struct kscene {
    kname name;
    const char* description;
    kscene_flags flags;
    kscene_state state;
    // Hold onto the scene config until destroyed, then release.
    kresource_scene* config;

    // Transforms
    kscene_transform_data transforms;

    // Nodes
    kscene_node_hierarchy_data nodes;
    kscene_node_tag_data node_tags;

    // Attachments
    kscene_attachment_data attachments;
    kscene_attachment_type_data attachment_types; // used for lookups by type.
    kscene_attachment_tag_data attachment_tags;   // used for lookups by tag

} kscene;

typedef void (*PFN_kscene_node_traverse_callback)(kscene* scene, khandle node_handle);
typedef void (*PFN_kscene_attachment_traverse_callback)(kscene* scene, khandle node_handle);

KAPI b8 kscene_create(kresource_scene* config, kscene_flags flags, kscene* out_scene);
KAPI b8 kscene_initialize(kscene* scene);
KAPI b8 kscene_load(kscene* scene);
KAPI void kscene_unload(kscene* scene);
KAPI void kscene_destroy(kscene* scene);
KAPI b8 kscene_update(kscene* scene, const struct frame_data* p_frame_data);

KAPI b8 kscene_node_exists(kscene* scene, kname name);
KAPI b8 kscene_node_get(kscene* scene, khandle* out_node);
KAPI b8 kscene_node_has_transform(kscene* scene, khandle node);
KAPI b8 kscene_node_has_children(kscene* scene, khandle node);
KAPI b8 kscene_node_child_count_get(kscene* scene, khandle node, u32* out_count);

KAPI b8 kscene_node_local_transform_get(kscene* scene, khandle node, mat4* out_local_transform);
KAPI b8 kscene_node_world_transform_get(kscene* scene, khandle node, mat4* out_world_transform);

KAPI b8 kscene_node_children_traverse(kscene* scene, khandle parent_node, PFN_kscene_node_traverse_callback callback);

KAPI b8 kscene_node_create(kscene* scene, kname name, khandle parent_node, khandle* out_node);
KAPI b8 kscene_node_create_with_position(kscene* scene, kname name, khandle parent_node, vec3 position, khandle* out_node);
KAPI b8 kscene_node_create_with_rotation(kscene* scene, kname name, khandle parent_node, quat rotation, khandle* out_node);
KAPI b8 kscene_node_create_with_scale(kscene* scene, kname name, khandle parent_node, vec3 scale, khandle* out_node);
KAPI b8 kscene_node_create_with_position_rotation(kscene* scene, kname name, khandle parent_node, vec3 position, quat rotation, khandle* out_node);
KAPI b8 kscene_node_create_with_position_rotation_scale(kscene* scene, kname name, khandle parent_node, vec3 position, quat rotation, vec3 scale, khandle* out_node);

// Gets a handle to an attachment of the given node by name.
KAPI b8 kscene_node_attachment_get(kscene* scene, khandle node, kname attachment_name, khandle* out_attachment);
// Adds an attachment to the given node.
KAPI b8 kscene_node_attachment_add(kscene* scene, khandle node, khandle attachment);
KAPI b8 kscene_node_attachment_remove(kscene* scene, khandle node, khandle attachment);

KAPI b8 kscene_node_child_remove(kscene* scene, khandle parent_node, khandle child_node);

// Also recursively destroys children.
KAPI b8 kscene_node_destroy(kscene* scene, khandle node);

KAPI b8 kscene_node_name_set(kscene* scene, khandle node, kname name);
KAPI b8 kscene_node_parent_set(kscene* scene, khandle node, khandle parent_node);

KAPI b8 kscene_node_position_set(kscene* scene, khandle node, vec3 position);
KAPI b8 kscene_node_translate(kscene* scene, khandle node, vec3 translation);

KAPI b8 kscene_node_rotation_set(kscene* scene, khandle node, quat rotation);
KAPI b8 kscene_node_rotate(kscene* scene, khandle node, quat rotation);

KAPI b8 kscene_node_scale_set(kscene* scene, khandle node, vec3 scale);
KAPI b8 kscene_node_scale(kscene* scene, khandle node, vec3 scale);

KAPI b8 kscene_node_position_rotation_set(kscene* scene, khandle node, vec3 position, quat rotation);
KAPI b8 kscene_node_translate_rotate(kscene* scene, khandle node, vec3 translation, quat rotation);

KAPI b8 kscene_node_position_rotation_scale_set(kscene* scene, khandle node, vec3 position, quat rotation, vec3 scale);
KAPI b8 kscene_node_translate_rotate_scale(kscene* scene, khandle node, vec3 translation, quat rotation, vec3 scale);

KAPI b8 kscene_attachment_create(kscene* scene, kname name, kscene_attachment_type type, khandle owning_node, khandle* out_attachment);
// Auto unloads, detaches, and destroys.
KAPI b8 kscene_attachment_destroy(kscene* scene, khandle attachment);

// Traverse all attachments in scene of a given type.
KAPI b8 kscene_attachment_traverse_by_type(kscene* scene, kscene_attachment_type type, PFN_kscene_attachment_traverse_callback callback);

KAPI b8 kscene_save(kscene* scene);
