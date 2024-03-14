#include "hierarchy_graph.h"

#include "containers/darray.h"
#include "containers/stack.h"
#include "core/asserts.h"
#include "core/identifier.h"
#include "core/khandle.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"
#include "systems/xform_system.h"

static k_handle node_acquire(hierarchy_graph* graph, u32 parent_index, k_handle xform_handle);
static void node_release(hierarchy_graph* graph, k_handle* node_handle, b8 release_transform);
static void child_levels_update(hierarchy_graph* graph, u32 parent_index);
static void ensure_allocated(hierarchy_graph* graph, u32 new_node_count);
static void build_view_tree(hierarchy_graph* graph, hierarchy_graph_view* out_view);
static void destroy_view_tree(hierarchy_graph* graph, hierarchy_graph_view* out_view);

b8 hierarchy_graph_create(hierarchy_graph* out_graph) {
    if (!out_graph) {
        KERROR("hierarchy_graph_create requires a valid pointer to hold the created graph.");
        return false;
    }

    return true;
}

void hierarchy_graph_destroy(hierarchy_graph* graph) {
    if (graph) {
        // Realloc all the arrays.
        if (graph->node_handles) {
            kfree(graph->node_handles, sizeof(k_handle) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
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

        if (graph->xform_handles) {
            kfree(graph->xform_handles, sizeof(k_handle) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
            graph->xform_handles = 0;
        }

        graph->nodes_allocated = 0;

        destroy_view_tree(graph, &graph->view);
    }
}

void hierarchy_graph_update_tree_view_node(hierarchy_graph* graph, u32 node_index) {
    if (node_index == INVALID_ID) {
        return;
    }

    hierarchy_graph_view_node* node = &graph->view.nodes[node_index];

    if (k_handle_is_invalid(node->xform_handle)) {
        return;
    }

    // Update the local matrix.
    // TODO: check if dirty
    xform_calculate_local(node->xform_handle);
    mat4 node_local = xform_local_get(node->xform_handle);

    // Calculate and assign world matrix.
    mat4 world;
    if (node->parent_index != INVALID_ID) {
        hierarchy_graph_view_node* parent = &graph->view.nodes[node->parent_index];
        k_handle parent_xform_handle = parent->xform_handle;
        while (k_handle_is_invalid(parent_xform_handle)) {
            u32 parent_index = parent->parent_index;
            parent = &graph->view.nodes[parent_index];
            if (parent) {
                parent_xform_handle = parent->xform_handle;
            } else {
                parent_xform_handle = k_handle_invalid();
                break;
            }
        }
        if (k_handle_is_invalid(parent_xform_handle)) {
            // There is no parent with a transform anywhere up the tree. Just use local.
            world = node_local;
        } else {
            mat4 parent_world = xform_world_get(parent_xform_handle);
            world = mat4_mul(node_local, parent_world);
        }
    } else {
        world = node_local;
    }
    xform_world_set(node->xform_handle, world);

    if (node->children) {
        u32 child_count = darray_length(node->children);
        for (u32 i = 0; i < child_count; ++i) {
            // Proces children based off world matrix of this node.
            hierarchy_graph_update_tree_view_node(graph, node->children[i]);
        }
    }
}

void hierarchy_graph_update(hierarchy_graph* graph, const struct frame_data* p_frame_data) {
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

k_handle hierarchy_graph_root_add(hierarchy_graph* graph) {
    return hierarchy_graph_child_add_with_xform(graph, k_handle_invalid(), k_handle_invalid());
}

k_handle hierarchy_graph_root_add_with_xform(hierarchy_graph* graph, k_handle xform_handle) {
    return hierarchy_graph_child_add_with_xform(graph, k_handle_invalid(), xform_handle);
}

k_handle hierarchy_graph_child_add(hierarchy_graph* graph, k_handle parent_node_handle) {
    return hierarchy_graph_child_add_with_xform(graph, parent_node_handle, k_handle_invalid());
}

k_handle hierarchy_graph_child_add_with_xform(hierarchy_graph* graph, k_handle parent_node_handle, k_handle xform_handle) {
    return node_acquire(graph, parent_node_handle.handle_index, xform_handle);
}

void hierarchy_graph_node_remove(hierarchy_graph* graph, k_handle* node_handle, b8 release_transform) {
    node_release(graph, node_handle, release_transform);
}

quat hierarchy_graph_world_rotation_get(hierarchy_graph* graph, k_handle node_handle) {
    KASSERT(graph);

    if (k_handle_is_invalid(node_handle)) {
        KERROR("Invalid handle passed to get world rotation. Returning identity rotation.");
        return quat_identity();
    }

    stack rot_stack = {0};
    stack_create(&rot_stack, sizeof(quat));

    u32 handle_index = node_handle.handle_index;
    u32 parent_index = graph->parent_indices[handle_index];
    // Push the current "local" rotation onto the stack first.
    quat rot = xform_rotation_get(graph->xform_handles[handle_index]);
    stack_push(&rot_stack, &rot);
    while (parent_index != INVALID_ID) {
        // Get the parent transform's rotation and push onto the stack.
        k_handle xform_handle = graph->xform_handles[parent_index];
        rot = xform_rotation_get(xform_handle);
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

vec3 hierarchy_graph_world_position_get(hierarchy_graph* graph, k_handle node_handle) {
    KASSERT(graph);

    if (k_handle_is_invalid(node_handle)) {
        KERROR("Invalid handle passed to get world position. Returning zero position.");
        return vec3_zero();
    }

    mat4 world = xform_world_get(graph->xform_handles[node_handle.handle_index]);
    vec3 world_pos = mat4_position(world);

    return world_pos;
}

vec3 hierarchy_graph_world_scale_get(hierarchy_graph* graph, k_handle node_handle) {
    KASSERT(graph);

    if (k_handle_is_invalid(node_handle)) {
        KERROR("Invalid handle passed to get world rotation. Returning one vector.");
        return vec3_one();
    }

    stack scale_stack = {0};
    stack_create(&scale_stack, sizeof(vec3));

    u32 handle_index = node_handle.handle_index;
    u32 parent_index = graph->parent_indices[handle_index];
    // Push the current "local" scale onto the stack first.
    vec3 scale = xform_scale_get(graph->xform_handles[handle_index]);
    stack_push(&scale_stack, &scale);
    while (parent_index != INVALID_ID) {
        // Get the parent transform's scale and push onto the stack.
        k_handle xform_handle = graph->xform_handles[parent_index];
        scale = xform_scale_get(xform_handle);
        stack_push(&scale_stack, &scale);

        handle_index = parent_index;
        parent_index = graph->parent_indices[handle_index];
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

static k_handle node_acquire(hierarchy_graph* graph, u32 parent_index, k_handle xform_handle) {
    KASSERT(graph);
    for (u32 i = 0; i < graph->nodes_allocated; ++i) {
        if (k_handle_is_invalid(graph->node_handles[i])) {
            // Found a free slot. Setup the handle and id.
            graph->node_handles[i] = k_handle_create(i);
            // If parent is INVALID_ID, then it is a root node. Otherwise,
            // nest it below the parent in the hierarchy.
            graph->levels[i] = parent_index == INVALID_ID ? 0 : graph->levels[parent_index] + 1;
            graph->parent_indices[i] = parent_index;
            graph->dirty_flags[i] = false;
            graph->xform_handles[i] = xform_handle;

            return graph->node_handles[i];
        }
    }

    // Reaching this point means there is no more space in the table. Realloc everything,
    // and move to a larger list. Doubling the size should be sufficient.
    u32 new_index = graph->nodes_allocated;
    ensure_allocated(graph, graph->nodes_allocated ? (graph->nodes_allocated * 2) : 1);

    // The first free slot will be in the newly allocated block, at the end of the existing block.
    graph->node_handles[new_index] = k_handle_create(new_index);
    // If parent is INVALID_ID, then it is a root node. Otherwise,
    // nest it below the parent in the hierarchy.
    graph->levels[new_index] = parent_index == INVALID_ID ? 0 : graph->levels[parent_index] + 1;
    graph->parent_indices[new_index] = parent_index;
    graph->dirty_flags[new_index] = false;
    graph->xform_handles[new_index] = xform_handle;

    return graph->node_handles[new_index];
}

static void node_release(hierarchy_graph* graph, k_handle* node_handle, b8 release_transform) {
    KASSERT(graph);
    if (k_handle_is_invalid(*node_handle)) {
        KERROR("Tried to release a node using an invalid handle. Nothing was done.");
    } else {
        if (node_handle->unique_id.uniqueid != graph->node_handles[node_handle->handle_index].unique_id.uniqueid) {
            KERROR("Tried to release a node using a stale handle. Nothing was done.");
        } else {
            // The handle is valid and matching. Take any node that is a child of this node and move it up
            // in the hierarchy. This may mean these nodes become roots themselves.
            // Recursively update child levels.This also would mean that children of children would need thier levels updated.
            child_levels_update(graph, graph->parent_indices[node_handle->handle_index]);

            // Release the node entry back into the list by invalidating all the fields.
            graph->parent_indices[node_handle->handle_index] = INVALID_ID;
            graph->levels[node_handle->handle_index] = INVALID_ID_U8;
            graph->dirty_flags[node_handle->handle_index] = false;

            // Release the xform (if needed) and invalidate the handle.
            if (release_transform) {
                xform_destroy(&graph->xform_handles[node_handle->handle_index]);
            }
            k_handle_invalidate(&graph->xform_handles[node_handle->handle_index]);

            // Finally, invalidate the node handle itself.
            k_handle_invalidate(&graph->node_handles[node_handle->handle_index]);
            // Also hit the one passed in.
            k_handle_invalidate(node_handle);
        }
    }
}

static void child_levels_update(hierarchy_graph* graph, u32 parent_index) {
    // If there is no parent, node becomes a root. Otherwise, nest below the parent.
    u32 new_level = parent_index == INVALID_ID ? 0 : graph->levels[parent_index] + 1;
    for (u32 i = 0; i < graph->nodes_allocated; ++i) {
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
        k_handle* new_node_handles = kallocate(sizeof(k_handle) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->node_handles) {
            kcopy_memory(new_node_handles, graph->node_handles, sizeof(k_handle) * graph->nodes_allocated);
            kfree(graph->node_handles, sizeof(k_handle) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->node_handles = new_node_handles;
        // Invalidate all new entries in the array.
        for (u32 node_handle_index = graph->nodes_allocated; node_handle_index < new_node_count; ++node_handle_index) {
            graph->node_handles[node_handle_index] = k_handle_invalid();
        }

        u32* new_parent_indices = kallocate(sizeof(u32) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->parent_indices) {
            kcopy_memory(new_parent_indices, graph->parent_indices, sizeof(u32) * graph->nodes_allocated);
            kfree(graph->parent_indices, sizeof(u32) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->parent_indices = new_parent_indices;
        // Invalidate all new entries in the array.
        for (u32 parent_index = graph->nodes_allocated; parent_index < new_node_count; ++parent_index) {
            graph->parent_indices[parent_index] = INVALID_ID;
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

        k_handle* new_xform_handles = kallocate(sizeof(k_handle) * new_node_count, MEMORY_TAG_ARRAY);
        if (graph->xform_handles) {
            kcopy_memory(new_xform_handles, graph->xform_handles, sizeof(k_handle) * graph->nodes_allocated);
            kfree(graph->xform_handles, sizeof(k_handle) * graph->nodes_allocated, MEMORY_TAG_ARRAY);
        }
        graph->xform_handles = new_xform_handles;
        // Invalidate all new entries in the array.
        for (u32 xform_handle_index = graph->nodes_allocated; xform_handle_index < new_node_count; ++xform_handle_index) {
            graph->xform_handles[xform_handle_index] = k_handle_invalid();
        }

        graph->nodes_allocated = new_node_count;
    }
}

static u32 hierarchy_node_create(hierarchy_graph_view* view, k_handle node_handle, k_handle xform_handle, u32 parent_index) {
    hierarchy_graph_view_node node = {0};
    node.node_handle = node_handle;
    node.xform_handle = xform_handle;
    node.children = 0;
    node.parent_index = parent_index;

    darray_push(view->nodes, node);
    u32 node_count = darray_length(view->nodes);

    return node_count - 1;
}

static void build_view_tree_node_children(hierarchy_graph* graph, u32 parent_index) {
    for (u32 i = 0; i < graph->nodes_allocated; ++i) {
        hierarchy_graph_view_node* parent = &graph->view.nodes[parent_index];
        if (graph->parent_indices[i] == parent->node_handle.handle_index) {
            // Found a child.
            if (!parent->children) {
                parent->children = darray_create(u32);
            }

            u32 node_index = hierarchy_node_create(&graph->view, graph->node_handles[i], graph->xform_handles[i], parent_index);

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

    for (u32 i = 0; i < graph->nodes_allocated; ++i) {
        // Only work on root nodes.
        if (!k_handle_is_invalid(graph->node_handles[i]) && graph->parent_indices[i] == INVALID_ID) {
            u32 root_index = hierarchy_node_create(out_view, graph->node_handles[i], graph->xform_handles[i], INVALID_ID);

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
