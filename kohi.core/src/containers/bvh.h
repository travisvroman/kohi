#pragma once

#include "defines.h"
#include "math/math_types.h"

typedef u32 bvh_id;
typedef u64 bvh_userdata;

#define BVH_INVALID_NODE INVALID_ID_U32

typedef struct bvh_node {
	aabb aabb;		   // padded AABB for leaves, tight AABB for internal.
	bvh_userdata user; // user payload for leaves
	u32 parent;		   // index
	u32 left;		   // index
	u32 right;		   // index
	i32 height;		   // -1 means free, 0 = leaf, >0 = internal
	u32 next;
	b8 moved; // hint for incremental queries
} bvh_node;

typedef struct bvh {
	u32 root;		 // index of root node
	bvh_node *nodes; // pool of nodes
	u32 capacity;
	u32 count;
	u32 free_list;
	void *owner_context;
} bvh;

KAPI b8 bvh_create (u32 inital_capacity, void *owner_context, bvh *out_bvh);
KAPI void bvh_destroy (bvh *t);
// Reserve capacity for at least leaf_capacity leaves (internally, 2n-1 nodes)
KAPI b8 bvh_reserve (bvh *t, u32 leaf_capacity);

KAPI bvh_id bvh_insert (bvh *t, aabb tight_aabb, bvh_userdata user);

KAPI void bvh_remove (bvh *t, bvh_id id);

// Update an existing leaf's AABB. If it moves outside its padded AABB, the leaf is reinserted.
KAPI void bvh_update (bvh *t, bvh_id id, aabb new_tight_aabb);

// Query - call cb(user, id) for every leaf overlapping query AABB, return number of hits.
typedef u32 (*bvh_query_callback)(bvh_userdata user, bvh_id id, void *usr);
KAPI u32 bvh_query_overlaps (const bvh *t, aabb query, bvh_query_callback callback, void *context);

// Ray cast (origin + dir, max). Callback gets fraction [0,hit], return 0 to terminate early.
typedef b8 (*bvh_raycast_callback)(bvh_userdata user, bvh_id id, const ray *r, f32 min, f32 max, f32 dist, vec3 pos, void *usr, raycast_hit *out_result);
KAPI raycast_result bvh_raycast (const bvh *t, const ray *r, bvh_raycast_callback callback, void *usr);

KAPI void bvh_rebalance (bvh *t, u32 iterations);

KAPI void bvh_debug_trace_to_leaf (const bvh *t, bvh_userdata target_user, const ray *r);

KAPI void bvh_debug_print (const bvh *t);
