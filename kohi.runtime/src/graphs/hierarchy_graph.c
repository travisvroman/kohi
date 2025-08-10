#include "hierarchy_graph.h"

#include "containers/darray.h"
#include "containers/stack.h"
#include "core_resource_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "systems/ktransform_system.h"

static khierarchy_node node_acquire(hierarchy_graph* graph, u32 parent_index, ktransform ktransform_handle);
static void node_release(hierarchy_graph* graph, khierarchy_node* node_handle, b8 release_transform);
static void child_levels_update(hierarchy_graph* graph, u32 parent_index);
static void ensure_allocated(hierarchy_graph* graph, u32 new_node_count);
static void build_view_tree(hierarchy_graph* graph, hierarchy_graph_view* out_view);
static void destroy_view_tree(hierarchy_graph* graph, hierarchy_graph_view* out_view);
static void hierarchy_graph_update_tree_view_node(hierarchy_graph* graph, u32 node_index);
static u32 hierarchy_graph_parent_index_get(const hierarchy_graph* graph, khierarchy_node node_handle);

b8 hierarchy_graph_create(hierarchy_graph* out_graph, u64 default_meta_value) {
    if (!out_graph) {
        KERROR("hierarchy_graph_create requires a valid pointer to hold the created graph.");
        return false;
    }

    out_graph->default_meta_value = default_meta_value;

    // Start with a reasonably large count of nodes.
    ensure_allocated(out_graph, 128);

    return true;
}

void hierarchy_graph_destroy(hierarchy_graph* graph) {
    if (graph) {
        // Realloc all the arrays.
        if (graph->node_handles) {
            kfree(graph->node_handles, sizeof(khierarchy_node) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->node_handles = 0;
        }

        if (graph->parent_indices) {
            kfree(graph->parent_indices, sizeof(u32) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->parent_indices = 0;
        }

        if (graph->levels) {
            kfree(graph->levels, sizeof(u8) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->levels = 0;
        }

        if (graph->dirty_flags) {
            kfree(graph->dirty_flags, sizeof(b8) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->dirty_flags = 0;
        }

        if (graph->ktransform_handles) {
            kfree(graph->ktransform_handles, sizeof(ktransform) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->ktransform_handles = 0;
        }

        if (graph->meta) {
            kfree(graph->meta, sizeof(u64) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->meta = 0;
        }

        graph->nodes_allocated = 0;

        destroy_view_tree(graph, &graph->view);
    }
}

void hierarchy_graph_update(hierarchy_graph* graph) {
    // Destroy the old tree
    destroy_view_tree(graph, &graph->view);

    // Build up the view tree.
    build_view_tree(graph, &graph->view);

    // Traverse the tree and update the transforms.
    u32 root_count = darray_length(graph->view.root_indices);
    for (u32 i = 0; i < root_count; ++i) {
        // Roots have no parent, so no world matrix is passed.
        hierarchy_graph_update_tree_view_node(graph, graph->view.root_indices[i]);
    }
}

ktransform hierarchy_graph_ktransform_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    return graph->ktransform_handles[node_handle];
}

b8 hierarchy_graph_ktransform_local_matrix_get(const hierarchy_graph* graph, khierarchy_node node_handle, mat4* out_matrix) {
    if (!graph || node_handle == KHIERARCHY_NODE_INVALID || !out_matrix) {
        return false;
    }

    ktransform ktransform_handle = graph->ktransform_handles[node_handle];
    ktransform_calculate_local(ktransform_handle);
    *out_matrix = ktransform_local_get(ktransform_handle);

    return true;
}

khierarchy_node hierarchy_graph_parent_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    u32 parent_index = hierarchy_graph_parent_index_get(graph, node_handle);
    if (parent_index == KHIERARCHY_NODE_INVALID) {
        return KHIERARCHY_NODE_INVALID;
    }
    return graph->node_handles[parent_index];
}

ktransform hierarchy_graph_parent_ktransform_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    u32 parent_index = hierarchy_graph_parent_index_get(graph, node_handle);
    if (parent_index == KHIERARCHY_NODE_INVALID) {
        return KHIERARCHY_NODE_INVALID;
    }
    return graph->ktransform_handles[parent_index];
}

khierarchy_node hierarchy_graph_root_add(hierarchy_graph* graph) {
    return hierarchy_graph_child_add_with_ktransform(graph, KHIERARCHY_NODE_INVALID, KTRANSFORM_INVALID);
}

khierarchy_node hierarchy_graph_root_add_with_ktransform(hierarchy_graph* graph, ktransform ktransform_handle) {
    return hierarchy_graph_child_add_with_ktransform(graph, KHIERARCHY_NODE_INVALID, ktransform_handle);
}

khierarchy_node hierarchy_graph_child_add(hierarchy_graph* graph, khierarchy_node parent_node_handle) {
    return hierarchy_graph_child_add_with_ktransform(graph, parent_node_handle, KTRANSFORM_INVALID);
}

khierarchy_node hierarchy_graph_child_add_with_ktransform(hierarchy_graph* graph, khierarchy_node parent_node_handle, ktransform ktransform_handle) {
    return node_acquire(graph, parent_node_handle, ktransform_handle);
}

u64 hierarchy_graph_meta_get(hierarchy_graph* graph, khierarchy_node node) {
    if (!graph) {
        KERROR("%s - null graph pointer passed, returning INVALID_ID_U64", __FUNCTION__);
        return INVALID_ID_U64;
    } else if (node == KHIERARCHY_NODE_INVALID || node >= graph->nodes_allocated) {
        return graph->default_meta_value;
    }

    return graph->meta[node];
}

b8 hierarchy_graph_meta_set(hierarchy_graph* graph, khierarchy_node node, u64 meta) {
    if (!graph) {
        KERROR("%s - null graph pointer passed, nothing to be done.", __FUNCTION__);
        return false;
    } else if (node == KHIERARCHY_NODE_INVALID || node >= graph->nodes_allocated) {
        return false;
    }
    graph->meta[node] = meta;
    return true;
}

u32 hierarchy_graph_child_count_get(const hierarchy_graph* graph, khierarchy_node parent_node_handle) {
    if (!graph || parent_node_handle == KHIERARCHY_NODE_INVALID) {
        return 0;
    }

    u32 count = 0;
    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        if (graph->parent_indices[i] == parent_node_handle) {
            count++;
        }
    }

    return count;
}

b8 hierarchy_graph_child_get_by_index(const hierarchy_graph* graph, khierarchy_node parent_node_handle, u32 index, khierarchy_node* out_handle) {
    if (!graph || parent_node_handle == KHIERARCHY_NODE_INVALID) {
        return 0;
    }

    u32 child_index = 0;

    // Search for children with the given parent index.
    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        if (graph->parent_indices[i] == parent_node_handle) {
            if (child_index == index) {
                *out_handle = graph->node_handles[i];
                return true;
            }
            child_index++;
        }
    }

    return false;
}

void hierarchy_graph_node_remove(hierarchy_graph* graph, khierarchy_node* node_handle, b8 release_transform) {
    node_release(graph, node_handle, release_transform);
}

quat hierarchy_graph_world_rotation_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    KASSERT(graph);

    if (node_handle == KHIERARCHY_NODE_INVALID) {
        KERROR("Invalid handle passed to get world rotation. Returning identity rotation.");
        return quat_identity();
    }

    stack rot_stack = {0};
    stack_create(&rot_stack, sizeof(quat));

    u32 handle_index = node_handle;
    u32 parent_index = graph->parent_indices[handle_index];
    // Push the current "local" rotation onto the stack first.
    quat rot = ktransform_rotation_get(graph->ktransform_handles[handle_index]);
    stack_push(&rot_stack, &rot);
    while (parent_index != KHIERARCHY_NODE_INVALID) {
        // Get the parent transform's rotation and push onto the stack.
        ktransform ktransform_handle = graph->ktransform_handles[parent_index];
        rot = ktransform_rotation_get(ktransform_handle);
        stack_push(&rot_stack, &rot);

        handle_index = parent_index;
        parent_index = graph->parent_indices[handle_index];
    }

    quat world_rot = quat_identity();
    while (rot_stack.element_count) {
        quat r;
        if (!stack_pop(&rot_stack, &r)) {
            KERROR("Failed to pop from rotation stack. Result might be invalid.");
        } else {
            world_rot = quat_mul(world_rot, r);
        }
    }

    stack_destroy(&rot_stack);

    return world_rot;
}

vec3 hierarchy_graph_world_position_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    KASSERT(graph);

    if (node_handle == KHIERARCHY_NODE_INVALID) {
        KERROR("Invalid handle passed to get world position. Returning zero position.");
        return vec3_zero();
    }

    mat4 world = ktransform_world_get(graph->ktransform_handles[node_handle]);
    vec3 world_pos = mat4_position(world);

    return world_pos;
}

vec3 hierarchy_graph_world_scale_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    KASSERT(graph);

    if (node_handle == KHIERARCHY_NODE_INVALID) {
        KERROR("Invalid handle passed to get world rotation. Returning one vector.");
        return vec3_one();
    }

    stack scale_stack = {0};
    stack_create(&scale_stack, sizeof(vec3));

    u32 parent_index = graph->parent_indices[node_handle];
    // Push the current "local" scale onto the stack first.
    vec3 scale = ktransform_scale_get(graph->ktransform_handles[node_handle]);
    stack_push(&scale_stack, &scale);
    while (parent_index != KHIERARCHY_NODE_INVALID) {
        // Get the parent transform's scale and push onto the stack.
        ktransform ktransform_handle = graph->ktransform_handles[parent_index];
        scale = ktransform_scale_get(ktransform_handle);
        stack_push(&scale_stack, &scale);

        node_handle = parent_index;
        parent_index = graph->parent_indices[node_handle];
    }

    vec3 world_scale = vec3_one();
    while (scale_stack.element_count) {
        vec3 s;
        if (!stack_pop(&scale_stack, &s)) {
            KERROR("Failed to pop from scale stack. Result might be invalid.");
        } else {
            world_scale = vec3_mul(world_scale, s);
        }
    }

    stack_destroy(&scale_stack);

    return world_scale;
}

static khierarchy_node node_acquire(hierarchy_graph* graph, u32 parent_index, ktransform ktransform_handle) {
    KASSERT(graph);
    khierarchy_node node = KHIERARCHY_NODE_INVALID;
    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        if (graph->node_handles[i] == KHIERARCHY_NODE_INVALID) {
            // Found a free slot.
            node = i;
            break;
        }
    }

    if (node == KHIERARCHY_NODE_INVALID) {
        // Invalid means there is no more space in the table. Realloc everything,
        // and move to a larger list. Doubling the size should be sufficient.
        // The first free slot will be in the newly allocated block, at the end of the existing block.
        node = graph->nodes_allocated;
        ensure_allocated(graph, graph->nodes_allocated ? (graph->nodes_allocated * 2) : 1);
    }

    graph->node_handles[node] = node;
    // If parent is KHIERARCHY_NODE_INVALID, then it is a root node. Otherwise,
    // nest it below the parent in the hierarchy.
    graph->levels[node] = parent_index == KHIERARCHY_NODE_INVALID ? 0 : graph->levels[parent_index] + 1;
    graph->parent_indices[node] = parent_index;
    graph->dirty_flags[node] = false;
    graph->ktransform_handles[node] = ktransform_handle;

    return node;
}

static void node_release(hierarchy_graph* graph, khierarchy_node* node_handle, b8 release_transform) {
    KASSERT(graph);
    if (*node_handle == KHIERARCHY_NODE_INVALID) {
        KERROR("Tried to release a node using an invalid handle. Nothing was done.");
    } else {
        // The handle is valid and matching. Take any node that is a child of this node and move it up
        // in the hierarchy. This may mean these nodes become roots themselves.
        // Recursively update child levels.This also would mean that children of children would need thier levels updated.
        child_levels_update(graph, graph->parent_indices[*node_handle]);

        // Release the node entry back into the list by invalidating all the fields.
        graph->parent_indices[*node_handle] = KHIERARCHY_NODE_INVALID;
        graph->levels[*node_handle] = 0;
        graph->dirty_flags[*node_handle] = false;

        // Release the ktransform (if needed) and invalidate the handle.
        if (release_transform) {
            ktransform_destroy(&graph->ktransform_handles[*node_handle]);
        }
        graph->ktransform_handles[*node_handle] = KTRANSFORM_INVALID;

        // Finally, invalidate the node handle itself.
        graph->node_handles[*node_handle] = KHIERARCHY_NODE_INVALID;

        // Also default the metadata
        graph->meta[*node_handle] = graph->default_meta_value;

        // Also hit the one passed in.
        *node_handle = KHIERARCHY_NODE_INVALID;
    }
}

static void child_levels_update(hierarchy_graph* graph, u32 parent_index) {
    // If there is no parent, node becomes a root. Otherwise, nest below the parent.
    u32 new_level = parent_index == KHIERARCHY_NODE_INVALID ? 0 : graph->levels[parent_index] + 1;
    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        if (graph->parent_indices[i] == parent_index) {
            graph->levels[i] = new_level;
            // Recurse down the tree.
            child_levels_update(graph, i);
        }
    }
}

static void ensure_allocated(hierarchy_graph* graph, u32 new_node_count) {
    KASSERT(graph);
    if (graph->nodes_allocated <= new_node_count) {
        // Realloc all the arrays.
        khierarchy_node* new_node_handles = kallocate(sizeof(khierarchy_node) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->node_handles) {
            kcopy_memory(new_node_handles, graph->node_handles, sizeof(khierarchy_node) * graph->nodes_allocated);
            kfree(graph->node_handles, sizeof(khierarchy_node) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->node_handles = new_node_handles;
        // Invalidate all new entries in the array.
        for (u32 node_handle_index = graph->nodes_allocated; node_handle_index < new_node_count; ++node_handle_index) {
            graph->node_handles[node_handle_index] = KHIERARCHY_NODE_INVALID;
        }

        u32* new_parent_indices = kallocate(sizeof(u32) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->parent_indices) {
            kcopy_memory(new_parent_indices, graph->parent_indices, sizeof(u32) * graph->nodes_allocated);
            kfree(graph->parent_indices, sizeof(u32) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->parent_indices = new_parent_indices;
        // Invalidate all new entries in the array.
        for (u32 parent_index = graph->nodes_allocated; parent_index < new_node_count; ++parent_index) {
            graph->parent_indices[parent_index] = KHIERARCHY_NODE_INVALID;
        }

        u8* new_levels = kallocate(sizeof(u8) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->levels) {
            kcopy_memory(new_levels, graph->levels, sizeof(u8) * graph->nodes_allocated);
            kfree(graph->levels, sizeof(u8) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->levels = new_levels;

        b8* new_dirty_flags = kallocate(sizeof(b8) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->dirty_flags) {
            kcopy_memory(new_dirty_flags, graph->dirty_flags, sizeof(b8) * graph->nodes_allocated);
            kfree(graph->dirty_flags, sizeof(b8) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->dirty_flags = new_dirty_flags;

        ktransform* new_ktransform_handles = kallocate(sizeof(ktransform) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->ktransform_handles) {
            kcopy_memory(new_ktransform_handles, graph->ktransform_handles, sizeof(ktransform) * graph->nodes_allocated);
            kfree(graph->ktransform_handles, sizeof(ktransform) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->ktransform_handles = new_ktransform_handles;
        // Invalidate all new entries in the array.
        for (u32 ktransform_handle_index = graph->nodes_allocated; ktransform_handle_index < new_node_count; ++ktransform_handle_index) {
            graph->ktransform_handles[ktransform_handle_index] = KTRANSFORM_INVALID;
        }

        // Metadata
        u64* new_meta = kallocate(sizeof(u64) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->meta) {
            kcopy_memory(new_meta, graph->meta, sizeof(u64) * graph->nodes_allocated);
            kfree(graph->meta, sizeof(u64) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->meta = new_meta;
        // Invalidate all new entries in the array.
        for (u32 meta_index = graph->nodes_allocated; meta_index < new_node_count; ++meta_index) {
            graph->meta[meta_index] = graph->default_meta_value;
        }

        graph->nodes_allocated = new_node_count;
    }
}

static u32 hierarchy_node_create(hierarchy_graph_view* view, khierarchy_node node_handle, ktransform ktransform_handle, u32 parent_index) {
    hierarchy_graph_view_node node = {0};
    node.node_handle = node_handle;
    node.ktransform_handle = ktransform_handle;
    node.children = 0;
    node.parent_index = parent_index;

    darray_push(view->nodes, node);
    u32 node_count = darray_length(view->nodes);

    return node_count - 1;
}

static void build_view_tree_node_children(hierarchy_graph* graph, u32 parent_index) {
    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        hierarchy_graph_view_node* parent = &graph->view.nodes[parent_index];
        if (graph->parent_indices[i] == parent->node_handle) {
            // Found a child.
            if (!parent->children) {
                parent->children = darray_create(u32);
            }

            u32 node_index = hierarchy_node_create(&graph->view, graph->node_handles[i], graph->ktransform_handles[i], parent_index);

            // Recurse
            build_view_tree_node_children(graph, node_index);

            // Add to children list.
            darray_push(parent->children, node_index);
        }
    }
}

static void build_view_tree(hierarchy_graph* graph, hierarchy_graph_view* out_view) {
    out_view->nodes = darray_create(hierarchy_graph_view_node);
    out_view->root_indices = darray_create(u32);

    for (u32 i = 1; i < graph->nodes_allocated; ++i) {
        // Only work on root nodes.
        if (graph->node_handles[i] != KHIERARCHY_NODE_INVALID && graph->parent_indices[i] == KHIERARCHY_NODE_INVALID) {
            u32 root_index = hierarchy_node_create(out_view, graph->node_handles[i], graph->ktransform_handles[i], KTRANSFORM_INVALID);

            // Recurse
            build_view_tree_node_children(graph, root_index);

            // Add to roots list.
            darray_push(out_view->root_indices, root_index);
        }
    }
}

static void destroy_view_tree_node(hierarchy_graph_view* view, hierarchy_graph_view_node* node) {
    // Clean up children first, if any
    if (node->children) {
        u32 child_count = darray_length(node->children);
        for (u32 i = 0; i < child_count; ++i) {
            hierarchy_graph_view_node* child = &view->nodes[node->children[i]];
            destroy_view_tree_node(view, child);
        }
        darray_destroy(node->children);
    }
}

static void destroy_view_tree(hierarchy_graph* graph, hierarchy_graph_view* view) {
    if (!graph || !view) {
        return;
    }

    if (view->root_indices) {
        u32 root_count = darray_length(view->root_indices);

        for (u32 i = 0; i < root_count; ++i) {
            hierarchy_graph_view_node* root = &view->nodes[view->root_indices[i]];
            destroy_view_tree_node(view, root);
        }

        darray_destroy(view->root_indices);
        view->root_indices = 0;
    }
}

static void hierarchy_graph_update_tree_view_node(hierarchy_graph* graph, u32 node_index) {
    if (node_index == KHIERARCHY_NODE_INVALID) {
        return;
    }

    hierarchy_graph_view_node* node = &graph->view.nodes[node_index];

    if (node->ktransform_handle == KHIERARCHY_NODE_INVALID) {
        return;
    }

    // Update the local matrix.
    // TODO: check if dirty
    ktransform_calculate_local(node->ktransform_handle);
    mat4 node_local = ktransform_local_get(node->ktransform_handle);

    // Calculate and assign world matrix.
    mat4 world;
    if (node->parent_index != KHIERARCHY_NODE_INVALID) {
        hierarchy_graph_view_node* parent = &graph->view.nodes[node->parent_index];
        ktransform parent_ktransform_handle = parent->ktransform_handle;
        while (parent_ktransform_handle == KTRANSFORM_INVALID) {
            u32 parent_index = parent->parent_index;
            parent = &graph->view.nodes[parent_index];
            if (parent) {
                parent_ktransform_handle = parent->ktransform_handle;
            } else {
                parent_ktransform_handle = KTRANSFORM_INVALID;
                break;
            }
        }
        if (parent_ktransform_handle == KTRANSFORM_INVALID) {
            // There is no parent with a transform anywhere up the tree. Just use local.
            world = node_local;
        } else {
            mat4 parent_world = ktransform_world_get(parent_ktransform_handle);
            world = mat4_mul(node_local, parent_world);
        }
    } else {
        world = node_local;
    }
    ktransform_world_set(node->ktransform_handle, world);

    if (node->children) {
        u32 child_count = darray_length(node->children);
        for (u32 i = 0; i < child_count; ++i) {
            // Proces children based off world matrix of this node.
            hierarchy_graph_update_tree_view_node(graph, node->children[i]);
        }
    }
}

static u32 hierarchy_graph_parent_index_get(const hierarchy_graph* graph, khierarchy_node node_handle) {
    return graph->parent_indices[node_handle];
}
