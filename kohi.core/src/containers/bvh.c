#include "bvh.h"
#include "containers/darray.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"

#include <stdio.h> // FIXME: for snprintf, replace that and remove this.

// The amount of padding around a tight AABB.
#define BVH_PADDING 0.1f

// Uncomment the below line if wanting to trace BVH selection, etc.
#define BVH_TRACE 0

static u32 bvh_alloc_node (bvh *t);
static void bvh_free_node (bvh *t, u32 id);
static b8 bvh_is_leaf (const bvh_node *node);
static u32 bvh_balance (bvh *t, u32 index_a);
static void bvh_fix_upwards (bvh *t, u32 i);
static void bvh_insert_leaf (bvh *t, u32 leaf);
static void bvh_remove_leaf (bvh *t, u32 leaf);
static void bvh_validate (const bvh *t);
static void bvh_validate_containment (const bvh *t, u32 node_id);

b8 bvh_create (u32 inital_capacity, void *owner_context, bvh *out_bvh) {
	out_bvh->root = BVH_INVALID_NODE;
	out_bvh->nodes = KNULL;
	out_bvh->capacity = 0;
	out_bvh->count = 0;
	out_bvh->free_list = BVH_INVALID_NODE;
	out_bvh->owner_context = owner_context;
	if (inital_capacity > 0) {
		if (!bvh_reserve(out_bvh, inital_capacity)) {
			return false;
		}
	}
	return true;
}

void bvh_destroy (bvh *t) {
	if (t) {
		kfree(t->nodes);
		t->nodes = KNULL;
		t->capacity = 0;
		t->count = 0;
		t->root = BVH_INVALID_NODE;
		t->free_list = BVH_INVALID_NODE;
	}
}

b8 bvh_reserve (bvh *t, u32 leaf_capacity) {
	// NOTE: This actually requires 2 * leaf_capacity + 1 nodes
	u32 need = leaf_capacity * 2 + 1;
	if (need <= t->capacity) {
		return true;
	}
	u32 old_capacity = t->capacity;
	bvh_node *new_nodes = kreallocate(t->nodes, need);
	if (!new_nodes) {
		return false;
	}
	t->nodes = new_nodes;
	t->capacity = need;
	// Link new nodes into free list.
	u32 old_free = t->free_list;
	for (u32 i = old_capacity; i < need; ++i) {
		t->nodes[i].height = -1;
		t->nodes[i].next = (i + 1 < need) ? i + 1 : old_free;
	}
	t->free_list = old_capacity;
	return true;
}

bvh_id bvh_insert (bvh *t, aabb tight_aabb, bvh_userdata user) {
	u32 id = bvh_alloc_node(t);
	bvh_node *n = &t->nodes[id];
	n->aabb = aabb_expand(tight_aabb, BVH_PADDING);
	n->user = user;
	n->left = n->right = BVH_INVALID_NODE;
	n->height = 0;
	n->moved = 1;
	bvh_insert_leaf(t, id);

	bvh_validate(t);
	bvh_validate_containment(t, t->root);

	return id;
}

void bvh_remove (bvh *t, bvh_id id) {
	if (id == BVH_INVALID_NODE) {
		return;
	}
	bvh_remove_leaf(t, id);
	bvh_free_node(t, id);

	bvh_validate(t);
	bvh_validate_containment(t, t->root);
}

void bvh_update (bvh *t, bvh_id id, aabb new_tight_aabb) {
	// If a new tight aabb is still inside of the padded aabb, boot. Otherwise insert.
	aabb old_padded = t->nodes[id].aabb;
	aabb new_expanded = aabb_expand(new_tight_aabb, BVH_PADDING);
	if (
		new_expanded.min.x >= old_padded.min.x && new_expanded.min.y >= old_padded.min.y && new_expanded.min.z >= old_padded.min.z &&
		new_expanded.max.x <= old_padded.max.x && new_expanded.max.y <= old_padded.max.y && new_expanded.max.z <= old_padded.max.z) {
		// Still inside, boot.
		return;
	}

	// Needs reinsertion
	bvh_remove_leaf(t, id);

	t->nodes[id].aabb = new_expanded;
	bvh_insert_leaf(t, id);

	t->nodes[id].moved = true;

	bvh_validate(t);
	bvh_validate_containment(t, t->root);
}

u32 bvh_query_overlaps (const bvh *t, aabb query, bvh_query_callback callback, void *usr) {
	if (t->root == BVH_INVALID_NODE) {
		return 0;
	}

	u32 stack_capacity = 64;
	u32 *stack = KALLOC_TYPE_CARRAY(u32, stack_capacity);
	if (!stack) {
		return 0;
	}
	u32 top = 0;
	u32 hits = 0;
	stack[top++] = t->root;
	while (top) {
		u32 id = stack[--top];
		if (!aabbs_intersect(t->nodes[id].aabb, query)) {
			continue;
		}
		if (bvh_is_leaf(&t->nodes[id])) {
			hits += callback(t->nodes[id].user, id, usr);
		} else {
			if (top + 2 > stack_capacity) {
				u32 new_capacity = stack_capacity * 2;
				u32 *new_stack = KREALLOC_TYPE_CARRAY(stack, u32, new_capacity);
				if (!new_stack) {
					break;
				}
				stack = new_stack;
				stack_capacity = new_capacity;
			}
			stack[top++] = t->nodes[id].left;
			stack[top++] = t->nodes[id].right;
		}
	}
	kfree(stack);
	return hits;
}

raycast_result bvh_raycast (const bvh *t, const ray *r, bvh_raycast_callback callback, void *usr) {
	raycast_result result = {0};
	if (t->root == BVH_INVALID_NODE) {
		return result;
	}

#if BVH_TRACE
	KINFO("=== RAYCAST START ===");
	KINFO("Ray: origin(%.3f,%.3f,%.3f) dir(%.3f,%.3f,%.3f) max=%.3f",
		  r->origin.x, r->origin.y, r->origin.z,
		  r->direction.x, r->direction.y, r->direction.z,
		  r->max_distance);
#endif

	u32 stack_capacity = 64;
	u32 *stack = KALLOC_TYPE_CARRAY(u32, stack_capacity);
	if (!stack) {
		return result;
	}

	b8 ignore_if_inside = FLAG_GET(r->flags, RAY_FLAG_IGNORE_IF_INSIDE_BIT);

	u32 top = 0;
#if BVH_TRACE
	u32 nodes_tested = 0;
	u32 nodes_passed = 0;
	u32 leaves_tested = 0;
#endif
	stack[top++] = t->root;
	while (top) {
		u32 id = stack[--top];

		const bvh_node *n = &t->nodes[id];

		f32 tmin = 0.0f;
		f32 tmaxi = r->max_distance;

#if BVH_TRACE
		nodes_tested++;
#endif

		b8 hit = ray_intersects_aabb(n->aabb, r->origin, r->direction, r->max_distance, &tmin, &tmaxi);

#if BVH_TRACE
		if (bvh_is_leaf(n)) {
			KINFO("  Leaf %u (user=%llu): hit=%d, tmin=%.3f, AABB: min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
				  id, n->user, hit, tmin,
				  n->aabb.min.x, n->aabb.min.y, n->aabb.min.z,
				  n->aabb.max.x, n->aabb.max.y, n->aabb.max.z);
		} else {
			KINFO("  Internal %u: hit=%d, tmin=%.3f, children=(%u,%u)",
				  id, hit, tmin, n->left, n->right);
		}
#endif

		if (!hit) {
			continue;
		}

#if BVH_TRACE
		nodes_passed++;
#endif

		if (tmin < 0.0f || tmin > r->max_distance) {
#if BVH_TRACE
			KINFO("    -> Rejected: tmin out of range");
#endif
			continue;
		}

		if (bvh_is_leaf(n)) {
			// Ignore if the origin is inside, depending on flags.
			if (ignore_if_inside && aabb_contains_point(r->origin, n->aabb)) {
				KINFO("    -> Rejected: inside AABB");
				continue;
			}

			f32 distance = tmin;
			vec3 pos = vec3_add(r->origin, vec3_mul_scalar(r->direction, distance));

			// Default to the AABB hit information.
			raycast_hit hit = {
				.type = RAYCAST_HIT_TYPE_BVH_AABB,
				.distance = distance,
				.user = n->user,
				.position = pos,
			};
			// If no callback, assume every hit is counted.
			// If there is a callback and it returns a success, it should override the data set above.
			if (!callback || callback(n->user, id, r, tmin, tmaxi, distance, pos, usr, &hit)) {
#if BVH_TRACE
				KINFO("    -> ACCEPTED");
#endif
				if (!result.hits) {
					result.hits = darray_create(raycast_hit);
				}

				darray_push(result.hits, hit);
			} else {
#if BVH_TRACE
				KINFO("    -> Rejected by callback");
#endif
			}
		} else {
			if (top + 2 > stack_capacity) {
				u32 new_capacity = stack_capacity * 2;
				u32 *new_stack = KREALLOC_TYPE_CARRAY(stack, u32, new_capacity);
				if (!new_stack) {
					break;
				}
				stack = new_stack;
				stack_capacity = new_capacity;
			}
			stack[top++] = t->nodes[id].left;
			stack[top++] = t->nodes[id].right;
		}
	}

#if BVH_TRACE
	KINFO("=== RAYCAST END: nodes_tested=%u, nodes_passed=%u, leaves_tested=%u, hits=%u ===",
		  nodes_tested, nodes_passed, leaves_tested, result.hits ? darray_length(result.hits) : 0);
#endif

	kfree(stack);

	return result;
}

void bvh_rebalance (bvh *t, u32 iterations) {
	u32 it = 0;
	u32 index = t->root;
	while (index != BVH_INVALID_NODE && it < iterations) {
		if (!bvh_is_leaf(&t->nodes[index])) {
			index = bvh_balance(t, index);
			++it;
		}
		// Advance, try right child, otherwise go up to find next sub-tree
		if (t->nodes[index].right != BVH_INVALID_NODE) {
			index = t->nodes[index].right;
		} else {
			break;
		}
	}
}

void bvh_debug_trace_to_leaf (const bvh *t, bvh_userdata target_user, const ray *r) {
	// First, find the leaf with this user data
	u32 target_leaf = BVH_INVALID_NODE;
	for (u32 i = 0; i < t->capacity; i++) {
		if (t->nodes[i].height != -1 && bvh_is_leaf(&t->nodes[i]) && t->nodes[i].user == target_user) {
			target_leaf = i;
			break;
		}
	}

	if (target_leaf == BVH_INVALID_NODE) {
		KERROR("Could not find leaf with user=%llu", target_user);
		return;
	}

	KINFO("=== Tracing path from root to leaf %u (user=%llu) ===", target_leaf, target_user);

	// Walk up from leaf to root, storing the path
	u32 path[64];
	u32 path_len = 0;
	u32 current = target_leaf;
	while (current != BVH_INVALID_NODE && path_len < 64) {
		path[path_len++] = current;
		current = t->nodes[current].parent;
	}

	// Print path from root to leaf
	KINFO("Path length: %u nodes", path_len);
	for (i32 i = path_len - 1; i >= 0; i--) {
		u32 node_id = path[i];
		const bvh_node *n = &t->nodes[node_id];

		f32 tmin = 0.0f;
		f32 tmax = r->max_distance;
		b8 hits = ray_intersects_aabb(n->aabb, r->origin, r->direction, r->max_distance, &tmin, &tmax);

		KINFO("  [%d] Node %u: %s, height=%d, hits=%d, tmin=%.3f",
			  path_len - 1 - i, node_id, bvh_is_leaf(n) ? "LEAF" : "INTERNAL", n->height, hits, tmin);
		KINFO("      AABB: min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
			  n->aabb.min.x, n->aabb.min.y, n->aabb.min.z,
			  n->aabb.max.x, n->aabb.max.y, n->aabb.max.z);

		if (!hits) {
			KERROR("      ^^^ RAY MISSES THIS NODE - This is where traversal stops!");

			// Debug the ray intersection in detail
			KINFO("      Ray origin: (%.3f,%.3f,%.3f)", r->origin.x, r->origin.y, r->origin.z);
			KINFO("      Ray direction: (%.3f,%.3f,%.3f)", r->direction.x, r->direction.y, r->direction.z);

			// Check each axis
			for (u32 a = 0; a < 3; ++a) {
				const char *axis_name[] = {"X", "Y", "Z"};
				f32 origin_a = r->origin.elements[a];
				f32 direction_a = r->direction.elements[a];
				f32 min_a = n->aabb.min.elements[a];
				f32 max_a = n->aabb.max.elements[a];

				if (kabs(direction_a) < K_FLOAT_EPSILON) {
					KINFO("      %s axis: ray parallel, origin=%.3f, box=[%.3f,%.3f] %s",
						  axis_name[a], origin_a, min_a, max_a,
						  (origin_a >= min_a && origin_a <= max_a) ? "PASS" : "FAIL");
				} else {
					f32 inv = 1.0f / direction_a;
					f32 t1 = (min_a - origin_a) * inv;
					f32 t2 = (max_a - origin_a) * inv;
					KINFO("      %s axis: t1=%.3f, t2=%.3f", axis_name[a], t1, t2);
				}
			}

			break;
		}
	}
}

static void bvh_debug_print_node (const bvh *t, u32 id, u32 depth) {
	const bvh_node *n = &t->nodes[id];

	char line[512];
	u32 offset = 0;

	// Indentation
	for (u32 i = 0; i < depth && offset < sizeof(line) - 2; ++i) {
		line[offset++] = ' ';
		line[offset++] = ' ';
	}

	b8 is_leaf = (n->left == BVH_INVALID_NODE && n->right == BVH_INVALID_NODE);

	offset += snprintf(
		line + offset,
		sizeof(line) - offset,
		"[%u] %s h=%d parent=%u "
		"AABB[(%.2f %.2f %.2f)->(%.2f %.2f %.2f)]",
		id,
		is_leaf ? "LEAF " : "INNER",
		n->height,
		n->parent,
		n->aabb.min.x, n->aabb.min.y, n->aabb.min.z,
		n->aabb.max.x, n->aabb.max.y, n->aabb.max.z);

#if KOHI_DEBUG
	// Inline invariant warnings (still one log call)
	if (is_leaf && (n->left != BVH_INVALID_NODE || n->right != BVH_INVALID_NODE)) {
		offset += snprintf(line + offset, sizeof(line) - offset, " ⚠leaf_has_children");
	}
	if (!is_leaf && (n->left == BVH_INVALID_NODE || n->right == BVH_INVALID_NODE)) {
		offset += snprintf(line + offset, sizeof(line) - offset, " ⚠missing_child");
	}
	if (n->left == id || n->right == id) {
		offset += snprintf(line + offset, sizeof(line) - offset, " ⚠self_ref");
	}
#endif

	KINFO("%s", line);

	if (!is_leaf) {
		bvh_debug_print_node(t, n->left, depth + 1);
		bvh_debug_print_node(t, n->right, depth + 1);
	}
}
static void bvh_debug_print_unreachable (const bvh *t) {
	b8 *visited = KALLOC_TYPE_CARRAY(b8, t->capacity);

	// DFS mark
	u32 stack[256];
	u32 top = 0;
	stack[top++] = t->root;

	while (top) {
		u32 id = stack[--top];
		if (visited[id])
			continue;
		visited[id] = true;

		if (t->nodes[id].left != BVH_INVALID_NODE)
			stack[top++] = t->nodes[id].left;
		if (t->nodes[id].right != BVH_INVALID_NODE)
			stack[top++] = t->nodes[id].right;
	}

	for (u32 i = 0; i < t->capacity; ++i) {
		if (t->nodes[i].height >= 0 && !visited[i]) {
			KINFO("UNREACHABLE NODE %u parent=%u", i, t->nodes[i].parent);
		}
	}

	kfree(visited);
}

void bvh_debug_print (const bvh *t) {
#if BVH_TRACE
	if (!t || t->root == BVH_INVALID_NODE) {
		KINFO("BVH: <empty>");
		return;
	}

	KINFO("BVH Debug Print:");
	bvh_debug_print_node(t, t->root, 0);

	KDEBUG("=== UNREACHABLE NODES ===");
	bvh_debug_print_unreachable(t);
#endif
}

static u32 bvh_alloc_node (bvh *t) {
	if (t->free_list == BVH_INVALID_NODE) {
		// Grow the pool
		u32 old_capacity = t->capacity;
		u32 new_capacity = old_capacity ? old_capacity * 2 : 64;
		bvh_node *new_nodes = KREALLOC_TYPE_CARRAY(t->nodes, bvh_node, new_capacity);
		if (!new_nodes) {
			return BVH_INVALID_NODE;
		}
		t->nodes = new_nodes;
		t->capacity = new_capacity;
		// Link new nodes into free list.
		for (u32 i = old_capacity; i < new_capacity; ++i) {
			t->nodes[i].height = -1;
			t->nodes[i].next = (i + 1 < new_capacity) ? i + 1 : BVH_INVALID_NODE;
		}
		t->free_list = old_capacity;
	}
	u32 id = t->free_list;
	t->free_list = t->nodes[id].next;
	bvh_node *n = &t->nodes[id];
	n->parent = BVH_INVALID_NODE;
	n->left = BVH_INVALID_NODE;
	n->right = BVH_INVALID_NODE;
	n->height = 0;
	n->user = 0;
	n->moved = false;
	n->aabb = extents_3d_zero();
	t->count++;
	return id;
}

static void bvh_free_node (bvh *t, u32 id) {
	bvh_node *n = &t->nodes[id];
	n->height = -1;
	n->next = t->free_list;
	t->free_list = id;
	t->count--;
}

static b8 bvh_is_leaf (const bvh_node *node) {
	return node->left == BVH_INVALID_NODE && node->right == BVH_INVALID_NODE;
}

static void bvh_check_node (const bvh *t, u32 i) {
#if KOHI_DEBUG
	if (i == BVH_INVALID_NODE) {
		return;
	}
	const bvh_node *n = &t->nodes[i];
	if (n->height == 0) {
		KASSERT(n->left == BVH_INVALID_NODE && n->right == BVH_INVALID_NODE);
	} else {
		KASSERT(n->left != BVH_INVALID_NODE && n->right != BVH_INVALID_NODE);
		KASSERT(n->left != i);
		KASSERT(n->right != i);
		KASSERT(n->left != n->right);
	}
	if (n->parent != BVH_INVALID_NODE) {
		KASSERT(t->nodes[n->parent].left == i || t->nodes[n->parent].right == i);
	}
#endif
}

static void bvh_recalc (bvh *t, u32 i) {
	u32 left = t->nodes[i].left;
	u32 right = t->nodes[i].right;
	t->nodes[i].aabb = aabb_combine(t->nodes[left].aabb, t->nodes[right].aabb);
	t->nodes[i].height = 1 + KMAX(t->nodes[left].height, t->nodes[right].height);
}

static u32 bvh_balance (bvh *t, u32 index_a) {
	bvh_node *a = &t->nodes[index_a];

	// Don't try to balance leaves.
	if (bvh_is_leaf(a)) {
		return index_a;
	}

	if (a->height < 2 || a->left == BVH_INVALID_NODE || a->right == BVH_INVALID_NODE) {
		return index_a;
	}

	u32 index_b = a->left;
	u32 index_c = a->right;
	bvh_node *b = &t->nodes[index_b];
	bvh_node *c = &t->nodes[index_c];

	i32 balance = c->height - b->height;

	// Right side is heavy, rotate left.
	if (balance > 1) {
		u32 index_f = c->left;
		u32 index_g = c->right;
		KASSERT(index_f != BVH_INVALID_NODE && index_g != BVH_INVALID_NODE);
		bvh_node *f = &t->nodes[index_f];
		bvh_node *g = &t->nodes[index_g];

		// C becomes parent of A
		c->parent = a->parent;
		if (c->parent != BVH_INVALID_NODE) {
			if (t->nodes[c->parent].left == index_a) {
				t->nodes[c->parent].left = index_c;
			} else {
				t->nodes[c->parent].right = index_c;
			}
		} else {
			t->root = index_c;
		}
		c->left = index_a;
		a->parent = index_c;

		// Pick a taller child for node A.
		if (f->height > g->height) {
			c->right = index_f;
			/* f->parent = index_c; */
			a->right = index_g;
			g->parent = index_a;
		} else {
			c->right = index_g;
			/* g->parent = index_c; */
			a->right = index_f;
			f->parent = index_a;
		}

		// Recalculate a then c
		bvh_recalc(t, index_a);
		bvh_recalc(t, index_c);

		bvh_check_node(t, index_a);
		bvh_check_node(t, index_c);
		return index_c;
	}

	// Left side is heavy, rotate right.
	if (balance < -1) {
		u32 index_d = b->left;
		u32 index_e = b->right;
		KASSERT(index_d != BVH_INVALID_NODE && index_e != BVH_INVALID_NODE);
		bvh_node *d = &t->nodes[index_d];
		bvh_node *e = &t->nodes[index_e];

		// B becomes parent of A
		b->parent = a->parent;
		if (b->parent != BVH_INVALID_NODE) {
			if (t->nodes[b->parent].left == index_a) {
				t->nodes[b->parent].left = index_b;
			} else {
				t->nodes[b->parent].right = index_b;
			}
		} else {
			t->root = index_b;
		}
		b->left = index_a;
		a->parent = index_b;

		// Pick a taller child for node A.
		if (d->height > e->height) {
			b->right = index_d;
			/* d->parent = index_b; */
			a->left = index_e;
			e->parent = index_a;
		} else {
			b->right = index_e;
			/* e->parent = index_b; */
			a->left = index_d;
			d->parent = index_a;
		}

		// Recalculate a then b
		bvh_recalc(t, index_a);
		bvh_recalc(t, index_b);

		bvh_check_node(t, index_a);
		bvh_check_node(t, index_b);
		return index_b;
	}

	return index_a;
}

static void bvh_fix_upwards (bvh *t, u32 i) {
	/*
	while (i != BVH_INVALID_NODE) {
		u32 index_left = t->nodes[i].left;
		u32 index_right = t->nodes[i].right;
		t->nodes[i].height = 1 + KMAX(t->nodes[index_left].height, t->nodes[index_right].height);
		t->nodes[i].aabb = aabb_combine(t->nodes[index_left].aabb, t->nodes[index_right].aabb);

		i = bvh_balance(t, i);
		i = t->nodes[i].parent;
	}*/

	while (i != BVH_INVALID_NODE) {
		u32 left = t->nodes[i].left;
		u32 right = t->nodes[i].right;

		KASSERT(left != BVH_INVALID_NODE);
		KASSERT(right != BVH_INVALID_NODE);

		t->nodes[i].height = 1 + KMAX(t->nodes[left].height, t->nodes[right].height);

		t->nodes[i].aabb = aabb_combine(t->nodes[left].aabb, t->nodes[right].aabb);

		// Balance returns the new subtree root
		i = bvh_balance(t, i);

		// Move UP from the new root, not the old one
		i = t->nodes[i].parent;
	}
}

static f32 calculate_cost (aabb leaf_aabb, f32 inheritance, const bvh_node *node) {
	aabb a = aabb_combine(leaf_aabb, node->aabb);
	if (bvh_is_leaf(node)) {
		return aabb_surface_area(a) + inheritance;
	} else {
		return (aabb_surface_area(a) - aabb_surface_area(node->aabb)) + inheritance;
	}
}

static void bvh_insert_leaf (bvh *t, u32 leaf) {
	if (t->root == BVH_INVALID_NODE) {
		t->root = leaf;
		t->nodes[leaf].parent = BVH_INVALID_NODE;
		return;
	}

	// Choose the next best sibling by minimal cost increase.
	aabb leaf_aabb = t->nodes[leaf].aabb;
	u32 index = t->root;
	while (!bvh_is_leaf(&t->nodes[index])) {
		u32 left = t->nodes[index].left;
		u32 right = t->nodes[index].right;
		f32 area = aabb_surface_area(t->nodes[index].aabb);
		aabb combined = aabb_combine(t->nodes[index].aabb, leaf_aabb);
		f32 combined_surf_area = aabb_surface_area(combined);
		f32 cost = 2.0f * combined_surf_area;
		f32 inheritance = 2.0f * (combined_surf_area - area);

		f32 cost_left = calculate_cost(leaf_aabb, inheritance, &t->nodes[left]);
		f32 cost_right = calculate_cost(leaf_aabb, inheritance, &t->nodes[right]);

		if (cost < cost_left && cost < cost_right) {
			break;
		}
		index = (cost_left < cost_right) ? left : right;
	}

	u32 sibling = index;
	u32 old_parent = t->nodes[sibling].parent;
	u32 new_parent = bvh_alloc_node(t);
	t->nodes[new_parent].parent = old_parent;
	t->nodes[new_parent].aabb = aabb_combine(leaf_aabb, t->nodes[sibling].aabb);
	t->nodes[new_parent].height = t->nodes[sibling].height + 1;

	if (old_parent != BVH_INVALID_NODE) {
		if (t->nodes[old_parent].left == sibling) {
			t->nodes[old_parent].left = new_parent;
		} else {
			t->nodes[old_parent].right = new_parent;
		}
	} else {
		t->root = new_parent;
	}

	t->nodes[new_parent].left = sibling;
	t->nodes[sibling].parent = new_parent;
	t->nodes[new_parent].right = leaf;
	t->nodes[leaf].parent = new_parent;

	bvh_fix_upwards(t, new_parent);
}

static void bvh_remove_leaf (bvh *t, u32 leaf) {
	if (leaf == t->root) {
		KASSERT(t->nodes[leaf].left == BVH_INVALID_NODE);
		KASSERT(t->nodes[leaf].right == BVH_INVALID_NODE);
		t->root = BVH_INVALID_NODE;
		return;
	}

	u32 parent = t->nodes[leaf].parent;
	u32 grand = t->nodes[parent].parent;
	u32 sibling = (t->nodes[parent].left == leaf) ? t->nodes[parent].right : t->nodes[parent].left;

	if (grand != BVH_INVALID_NODE) {
		if (t->nodes[grand].left == parent) {
			t->nodes[grand].left = sibling;
		} else {
			t->nodes[grand].right = sibling;
		}

		t->nodes[sibling].parent = grand;
		bvh_free_node(t, parent);
		bvh_fix_upwards(t, grand);
	} else {
		t->root = sibling;
		t->nodes[sibling].parent = BVH_INVALID_NODE;
		bvh_free_node(t, parent);
	}
}

static b8 bvh_validate_tree (const bvh *t, u32 node_id, u32 expected_parent) {
	if (node_id == BVH_INVALID_NODE) {
		return true;
	}

	const bvh_node *n = &t->nodes[node_id];

	// Check parent relationship
	if (n->parent != expected_parent) {
		KERROR("Node %u has wrong parent: expected %u, got %u", node_id, expected_parent, n->parent);
		return false;
	}

	if (bvh_is_leaf(n)) {
		// Leaf checks
		if (n->height != 0) {
			KERROR("Leaf node %u has non-zero height: %d", node_id, n->height);
			return false;
		}
		return true;
	}

	// Internal node checks
	if (n->left == BVH_INVALID_NODE || n->right == BVH_INVALID_NODE) {
		KERROR("Internal node %u missing children (left=%u, right=%u)", node_id, n->left, n->right);
		return false;
	}

	// Check that children's AABBs are contained in parent
	if (!aabb_contains_aabb(n->aabb, t->nodes[n->left].aabb)) {
		KERROR("Node %u AABB doesn't contain left child %u", node_id, n->left);
		return false;
	}
	if (!aabb_contains_aabb(n->aabb, t->nodes[n->right].aabb)) {
		KERROR("Node %u AABB doesn't contain right child %u", node_id, n->right);
		return false;
	}

	// Recursively validate children
	return bvh_validate_tree(t, n->left, node_id) && bvh_validate_tree(t, n->right, node_id);
}

static void bvh_validate (const bvh *t) {
#if KOHI_DEBUG
	if (t->root != BVH_INVALID_NODE) {
		if (!bvh_validate_tree(t, t->root, BVH_INVALID_NODE)) {
			KERROR("BVH tree validation failed!");
		}
	}
#endif
}

static void bvh_validate_containment (const bvh *t, u32 node_id) {
	if (node_id == BVH_INVALID_NODE || bvh_is_leaf(&t->nodes[node_id])) {
		return;
	}

	const bvh_node *n = &t->nodes[node_id];
	const bvh_node *left = &t->nodes[n->left];
	const bvh_node *right = &t->nodes[n->right];

	// Check if parent AABB actually contains children
	b8 contains_left =
		n->aabb.min.x <= left->aabb.min.x && n->aabb.min.y <= left->aabb.min.y && n->aabb.min.z <= left->aabb.min.z &&
		n->aabb.max.x >= left->aabb.max.x && n->aabb.max.y >= left->aabb.max.y && n->aabb.max.z >= left->aabb.max.z;

	b8 contains_right =
		n->aabb.min.x <= right->aabb.min.x && n->aabb.min.y <= right->aabb.min.y && n->aabb.min.z <= right->aabb.min.z &&
		n->aabb.max.x >= right->aabb.max.x && n->aabb.max.y >= right->aabb.max.y && n->aabb.max.z >= right->aabb.max.z;

	if (!contains_left) {
		KERROR("Node %u does NOT contain left child %u!", node_id, n->left);
		KERROR("  Parent: min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
			   n->aabb.min.x, n->aabb.min.y, n->aabb.min.z,
			   n->aabb.max.x, n->aabb.max.y, n->aabb.max.z);
		KERROR("  Left:   min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
			   left->aabb.min.x, left->aabb.min.y, left->aabb.min.z,
			   left->aabb.max.x, left->aabb.max.y, left->aabb.max.z);
	}

	if (!contains_right) {
		KERROR("Node %u does NOT contain right child %u!", node_id, n->right);
		KERROR("  Parent: min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
			   n->aabb.min.x, n->aabb.min.y, n->aabb.min.z,
			   n->aabb.max.x, n->aabb.max.y, n->aabb.max.z);
		KERROR("  Right:  min(%.3f,%.3f,%.3f) max(%.3f,%.3f,%.3f)",
			   right->aabb.min.x, right->aabb.min.y, right->aabb.min.z,
			   right->aabb.max.x, right->aabb.max.y, right->aabb.max.z);
	}

	bvh_validate_containment(t, n->left);
	bvh_validate_containment(t, n->right);
}
