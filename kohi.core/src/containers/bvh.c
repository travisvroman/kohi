#include "bvh.h"
#include "containers/darray.h"
#include "debug/kassert.h"
#include "defines.h"
#include "math/kmath.h"
#include "memory/kmemory.h"

// The amount of padding around a tight AABB.
#define BVH_PADDING 0.2f

static b8 ray_intersects_aabb_internal(aabb box, vec3 origin, vec3 direction, f32 max, f32* out_min, f32* out_max);
static u32 bvh_alloc_node(bvh* t);
static void bvh_free_node(bvh* t, u32 id);
static b8 bvh_is_leaf(const bvh_node* node);
static u32 bvh_balance(bvh* t, u32 index_a);
static void bvh_fix_upwards(bvh* t, u32 i);
static void bvh_insert_leaf(bvh* t, u32 leaf);
static void bvh_remove_leaf(bvh* t, u32 leaf);

b8 bvh_create(u32 inital_capacity, bvh* out_bvh) {
    out_bvh->root = 0;
    out_bvh->nodes = KNULL;
    out_bvh->capacity = 0;
    out_bvh->count = 0;
    out_bvh->free_list = KNULL;
    if (inital_capacity > 0) {
        if (!bvh_reserve(out_bvh, inital_capacity)) {
            return false;
        }
    }
    return true;
}

void bvh_destroy(bvh* t) {
    if (t) {
        KFREE_TYPE_CARRAY(t->nodes, bvh_node, t->capacity);
        t->nodes = KNULL;
        t->capacity = 0;
        t->count = 0;
        t->root = KNULL;
        t->free_list = KNULL;
    }
}

b8 bvh_reserve(bvh* t, u32 leaf_capacity) {
    // NOTE: This actually requires 2 * leaf_capacity + 1 nodes
    u32 need = leaf_capacity * 2 + 1;
    if (need <= t->capacity) {
        return true;
    }
    u32 old_capacity = t->capacity;
    bvh_node* new_nodes = KREALLOC_TYPE_CARRAY(t->nodes, bvh_node, old_capacity, need);
    if (!new_nodes) {
        return false;
    }
    t->nodes = new_nodes;
    t->capacity = need;
    // Link new nodes into free list.
    for (u32 i = old_capacity; i < need; ++i) {
        t->nodes[i].height = -1;
        t->nodes[i].next = (i + 1 < need) ? i + 1 : 0;
    }
    t->free_list = old_capacity;
    return true;
}

bvh_id bvh_insert(bvh* t, aabb tight_aabb, bvh_userdata user) {
    u32 id = bvh_alloc_node(t);
    bvh_node* n = &t->nodes[id];
    n->aabb = aabb_expand(tight_aabb, BVH_PADDING);
    n->user = user;
    n->left = t->nodes[id].right = KNULL;
    n->height = 0;
    n->moved = 1;
    bvh_insert_leaf(t, id);
    return id;
}

void bvh_remove(bvh* t, bvh_id id) {
    if (id == KNULL) {
        return;
    }
    bvh_remove_leaf(t, id);
    bvh_free_node(t, id);
}

void bvh_update(bvh* t, bvh_id id, aabb new_tight_aabb) {
    // If a new tight aabb is still inside of the padded aabb, boot. Otherwise insert.
    aabb padded_aabb = t->nodes[id].aabb;
    aabb expanded = aabb_expand(new_tight_aabb, BVH_PADDING);
    if (
        new_tight_aabb.min.x >= padded_aabb.min.x && new_tight_aabb.min.y >= padded_aabb.min.y && new_tight_aabb.min.z >= padded_aabb.min.z &&
        new_tight_aabb.max.x <= padded_aabb.max.x && new_tight_aabb.max.y <= padded_aabb.max.y && new_tight_aabb.max.z <= padded_aabb.max.z) {
        // Still inside, boot.
        return;
    }

    // Don't free inner resources.
    bvh_remove_leaf(t, id);

    t->nodes[id].aabb = expanded;
    bvh_insert_leaf(t, id);

    t->nodes[id].moved = 1;
}

u32 bvh_query_overlaps(const bvh* t, aabb query, bvh_query_callback callback, void* usr) {
    if (t->root == KNULL) {
        return 0;
    }

    u32 stack_capacity = 64;
    u32* stack = KALLOC_TYPE_CARRAY(u32, stack_capacity);
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
                u32* new_stack = KREALLOC_TYPE_CARRAY(stack, u32, stack_capacity, new_capacity);
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
    KFREE_TYPE_CARRAY(stack, u32, stack_capacity);
    return hits;
}

raycast_result bvh_raycast(const bvh* t, vec3 origin, vec3 direction, f32 max, b8 ignore_if_inside, bvh_raycast_callback callback, void* usr) {
    raycast_result result = {0};
    if (t->root == KNULL) {
        return result;
    }

    u32 stack_capacity = 64;
    u32* stack = KALLOC_TYPE_CARRAY(u32, stack_capacity);
    if (!stack) {
        return result;
    }

    u32 top = 0;
    stack[top++] = t->root;
    while (top) {
        u32 id = stack[--top];
        f32 tmin = 0.0f;
        f32 tmaxi = max;
        if (!ray_intersects_aabb_internal(t->nodes[id].aabb, origin, direction, max, &tmin, &tmaxi)) {
            continue;
        }
        if (bvh_is_leaf(&t->nodes[id])) {
            // Ignore if the origin is inside, depending on flags.
            if (ignore_if_inside && point_inside_aabb(origin, t->nodes[id].aabb)) {
                continue;
            }

            f32 distance = tmin;
            vec3 pos = vec3_add(origin, vec3_mul_scalar(direction, distance));
            // If no callback, assume every hit is counted.
            if (!callback || callback(t->nodes[id].user, id, tmin, tmaxi, distance, pos, usr)) {
                if (!result.hits) {
                    result.hits = darray_create(raycast_hit);
                }
                raycast_hit hit = {
                    .type = RAYCAST_HIT_TYPE_BVH_AABB,
                    .distance = distance,
                    .user = t->nodes[id].user,
                    .position = pos};
                darray_push(result.hits, hit);
            }
        } else {
            if (top + 2 > stack_capacity) {
                u32 new_capacity = stack_capacity * 2;
                u32* new_stack = KREALLOC_TYPE_CARRAY(stack, u32, stack_capacity, new_capacity);
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
    KFREE_TYPE_CARRAY(stack, u32, stack_capacity);

    return result;
}

void bvh_rebalance(bvh* t, u32 iterations) {
    u32 it = 0;
    u32 index = t->root;
    while (index != KNULL && it < iterations) {
        if (!bvh_is_leaf(&t->nodes[index])) {
            index = bvh_balance(t, index);
            ++it;
        }
        // Advance, try right child, otherwise go up to find next sub-tree
        if (t->nodes[index].right != KNULL) {
            index = t->nodes[index].right;
        } else {
            break;
        }
    }
}

static b8 ray_intersects_aabb_internal(aabb box, vec3 origin, vec3 direction, f32 max, f32* out_min, f32* out_max) {
    // Slab method with divide by zero handling.
    f32 min = 0.0f;
    f32 maxi = max;
    for (u32 a = 0; a < 3; ++a) {
        f32 origin_a = origin.elements[a];
        f32 direction_a = direction.elements[a];
        f32 min_a = box.min.elements[a];
        f32 max_a = box.max.elements[a];
        if (kabs(direction_a) < K_FLOAT_EPSILON) {
            if (origin_a < min_a || origin_a > max_a) {
                return false;
            }
        } else {
            f32 inv = 1.0f / direction_a;
            f32 t1 = (min_a - origin_a) * inv;
            f32 t2 = (max_a - origin_a) * inv;
            if (t1 > t2) {
                KSWAP(f32, t1, t2);
            }
            if (t1 > min) {
                min = t1;
            }
            if (t2 < maxi) {
                maxi = t2;
            }
            if (min > maxi) {
                return false;
            }
        }
    }
    if (out_min) {
        *out_min = min;
    }
    if (out_max) {
        *out_max = maxi;
    }
    return true;
}

static u32 bvh_alloc_node(bvh* t) {
    if (t->free_list == 0) {
        // Grow the pool
        u32 old_capacity = t->capacity;
        u32 new_capacity = old_capacity ? old_capacity * 2 : 64;
        bvh_node* new_nodes = KREALLOC_TYPE_CARRAY(t->nodes, bvh_node, old_capacity, new_capacity);
        t->nodes = new_nodes;
        t->capacity = new_capacity;
        // Link new nodes into free list.
        for (u32 i = old_capacity; i < new_capacity; ++i) {
            t->nodes[i].height = -1;
            t->nodes[i].next = (i + 1 < new_capacity) ? i + 1 : 0;
        }
        t->free_list = old_capacity;
    }
    u32 id = t->free_list;
    t->free_list = t->nodes[id].next;
    bvh_node* n = &t->nodes[id];
    n->parent = 0;
    n->left = 0;
    n->right = 0;
    n->height = 0;
    n->moved = 0;
    t->count++;
    return id;
}

static void bvh_free_node(bvh* t, u32 id) {
    bvh_node* n = &t->nodes[id];
    n->height = -1;
    n->next = t->free_list;
    t->free_list = id;
    t->count--;
}

static b8 bvh_is_leaf(const bvh_node* node) {
    return node->left == KNULL;
}

static void bvh_check_node(const bvh* t, u32 i) {
#if KOHI_DEBUG
    if (i == KNULL) {
        return;
    }
    const bvh_node* n = &t->nodes[i];
    if (n->height == 0) {
        KASSERT(n->left == KNULL && n->right == KNULL);
    } else {
        KASSERT(n->left != KNULL && n->right != KNULL);
        KASSERT(n->left != i);
        KASSERT(n->right != i);
        KASSERT(n->left != n->right);
    }
    if (n->parent != KNULL) {
        KASSERT(t->nodes[n->parent].left == i || t->nodes[n->parent].right == i);
    }
#endif
}

static void bvh_recalc(bvh* t, u32 i) {
    u32 left = t->nodes[i].left;
    u32 right = t->nodes[i].right;
    t->nodes[i].aabb = aabb_combine(t->nodes[left].aabb, t->nodes[right].aabb);
    t->nodes[i].height = 1 + KMAX(t->nodes[left].height, t->nodes[right].height);
}

static u32 bvh_balance(bvh* t, u32 index_a) {
    bvh_node* a = &t->nodes[index_a];
    if (a->height < 2 || a->left == KNULL || a->right == KNULL) {
        return index_a;
    }

    u32 index_b = a->left;
    u32 index_c = a->right;
    bvh_node* b = &t->nodes[index_b];
    bvh_node* c = &t->nodes[index_c];

    i32 balance = c->height - b->height;

    // Right side is heavy, rotate left.
    if (balance > 1) {
        u32 index_f = c->left;
        u32 index_g = c->right;
        KASSERT(index_f != KNULL && index_g != KNULL);
        bvh_node* f = &t->nodes[index_f];
        bvh_node* g = &t->nodes[index_g];

        // C becomes parent of A
        c->parent = a->parent;
        if (c->parent != KNULL) {
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
            f->parent = index_c;
            a->right = index_g;
            g->parent = index_a;
        } else {
            c->right = index_g;
            g->parent = index_c;
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
        bvh_node* d = &t->nodes[index_d];
        bvh_node* e = &t->nodes[index_e];

        // B becomes parent of A
        b->parent = a->parent;
        if (b->parent != KNULL) {
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
            d->parent = index_b;
            a->left = index_e;
            e->parent = index_a;
        } else {
            b->right = index_e;
            e->parent = index_b;
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

static void bvh_fix_upwards(bvh* t, u32 i) {
    while (i != KNULL) {
        u32 index_left = t->nodes[i].left;
        u32 index_right = t->nodes[i].right;
        t->nodes[i].height = 1 + KMAX(t->nodes[index_left].height, t->nodes[index_right].height);
        t->nodes[i].aabb = aabb_combine(t->nodes[index_left].aabb, t->nodes[index_right].aabb);
        u32 parent = t->nodes[i].parent;
        u32 new_i = bvh_balance(t, i);

        if (new_i == parent) {
            break;
        }

        i = parent;
    }
}

static f32 calculate_cost(aabb leaf_aabb, f32 inheritance, const bvh_node* node) {
    aabb a = aabb_combine(leaf_aabb, node->aabb);
    if (bvh_is_leaf(node)) {
        return aabb_surface_area(a) + inheritance;
    } else {
        return (aabb_surface_area(a) - aabb_surface_area(node->aabb)) + inheritance;
    }
}

static void bvh_insert_leaf(bvh* t, u32 leaf) {
    if (t->root == KNULL) {
        t->root = leaf;
        t->nodes[leaf].parent = KNULL;
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

    if (old_parent != KNULL) {
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

static void bvh_remove_leaf(bvh* t, u32 leaf) {
    if (leaf == t->root) {
        t->root = KNULL;
        return;
    }

    u32 parent = t->nodes[leaf].parent;
    u32 grand = t->nodes[parent].parent;
    u32 sibling = (t->nodes[parent].left == leaf) ? t->nodes[parent].right : t->nodes[parent].left;

    if (grand != KNULL) {
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
        t->nodes[sibling].parent = KNULL;
        bvh_free_node(t, parent);
    }
}
