/**
 * The hierarchy graph manages a hierarchy of xforms. Xforms themselves know nothing of
 * hierarchy. This is managed instead by this graph. The graph then calls to the
 * xform to recalculate based on values it passes.
 *
 * In the update of the graph, the graph is traversed from each root, and recursively updates
 * down the tree. Along the way, the graph reaches out to to the xform system and provides the
 * "parent" (if there is one) and caches that result instead.
 *
 * The steps of this update would be:
 * - Build up a list of dirty nodes.
 * - Mark all of the children of those nodes, recursively, as dirty.
 * - Traverse the graph, starting at roots, and store the result of parent*local as the cached matrix
 *   for that node.
 * - Reset the dirty list.
 */

#ifndef _XFORM_GRAPH_H_
#define _XFORM_GRAPH_H_

#include "core/khandle.h"
#include "math/math_types.h"

struct frame_data;

typedef struct hierarchy_graph_view_node {
    k_handle node_handle;
    k_handle xform_handle;

    // darray
    struct hierarchy_graph_view_node* children;
} hierarchy_graph_view_node;

typedef struct hierarchy_graph_view {
    // darray
    hierarchy_graph_view_node* roots;
} hierarchy_graph_view;

// LEFTOFF: This node structure and the graph below should
// likely be opaque to the outside. External references should
// only deal with handles and nothing more.
// A separate, read-only external "view" tree structure could be
// provided to anything that needs to know about the heirachy (i.e. an editor).

typedef struct hierarchy_graph {
    u32 nodes_allocated;
    // Node indices. Populated nodes will match index in the array. Invalid handle = empty slot.
    k_handle* node_handles;
    // Parent indices in the internal node array.
    u32* parent_indices;
    // Levels within the hierarchy. 0 = a root node.
    // NOTE: might just keep this in debug builds only, but it might
    // be useful for something.
    u8* levels;
    // Flags to mark the node as dirty.
    b8* dirty_flags;

    // Handles to the transforms.
    // NOTE: This can be an invalid handle, meaning that this node
    // does not have a transform. This allows nodes to exist in the hierarchy
    // which do not have transforms (i.e. a skybox doesn't need one).
    k_handle* xform_handles;

    // A view of the tree.
    hierarchy_graph_view view;
} hierarchy_graph;

KAPI b8 hierarchy_graph_create(hierarchy_graph* out_graph);
KAPI void hierarchy_graph_destroy(hierarchy_graph* graph);

KAPI void hierarchy_graph_update_tree_view_node(hierarchy_graph* graph, mat4* parent_world, hierarchy_graph_view_node* node);

KAPI void hierarchy_graph_update(hierarchy_graph* graph, const struct frame_data* p_frame_data);

KAPI k_handle hierarchy_graph_root_add(hierarchy_graph* graph);
KAPI k_handle hierarchy_graph_root_add_with_xform(hierarchy_graph* graph, k_handle xform_handle);
KAPI k_handle hierarchy_graph_child_add(hierarchy_graph* graph, k_handle parent_node_handle);
KAPI k_handle hierarchy_graph_child_add_with_xform(hierarchy_graph* graph, k_handle parent_node_handle, k_handle xform_handle);
KAPI void hierarchy_graph_node_remove(hierarchy_graph* graph, k_handle* node_handle, b8 release_transform);

KAPI quat hierarchy_graph_world_rotation_get(hierarchy_graph* graph, k_handle node_handle);

KAPI vec3 hierarchy_graph_world_position_get(hierarchy_graph* graph, k_handle node_handle);

KAPI vec3 hierarchy_graph_world_scale_get(hierarchy_graph* graph, k_handle node_handle);

#endif
