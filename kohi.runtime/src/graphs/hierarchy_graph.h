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

#include "identifiers/khandle.h"
#include "math/math_types.h"

struct frame_data;

typedef struct hierarchy_graph_view_node {
    khandle node_handle;
    khandle xform_handle;

    // An index into the view's nodes array. INVALID_ID if no parent.
    u32 parent_index;

    // darray An array of indices into the view's nodes array.
    u32* children;
} hierarchy_graph_view_node;

typedef struct hierarchy_graph_view {
    // darray A collective list of all view nodes.
    hierarchy_graph_view_node* nodes;
    // darray An array of indices into the nodes array.
    u32* root_indices;
} hierarchy_graph_view;

// LEFTOFF: This node structure and the graph below should
// likely be opaque to the outside. External references should
// only deal with handles and nothing more.
// A separate, read-only external "view" tree structure could be
// provided to anything that needs to know about the heirachy (i.e. an editor).

typedef struct hierarchy_graph {
    u32 nodes_allocated;
    // Node indices. Populated nodes will match index in the array. Invalid handle = empty slot.
    khandle* node_handles;
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
    khandle* xform_handles;

    // A view of the tree.
    hierarchy_graph_view view;
} hierarchy_graph;

/**
 * @brief Creates a new hierarchy graph, used to manage parent-child relationships
 * with any kind of resource via the use of handles and handles to xforms.
 *
 * @param out_graph A pointer to hold the newly-created graph.
 * @returns True if successful; otherwise false.
 */
KAPI b8 hierarchy_graph_create(hierarchy_graph* out_graph);

/**
 * @brief Destroys the given hierarchy graph, releasing all resources.
 * FIXME: This should also optionally release xforms if requested to do so. It currently does not.
 *
 * @param graph A pointer to the graph to be destroyed.
 */
KAPI void hierarchy_graph_destroy(hierarchy_graph* graph);

/**
 * @brief Performs internal update routines on the hierarchy, which includes rebuilding
 * the internal view tree.
 *
 * @param graph A pointer to the graph to update.
 */
KAPI void hierarchy_graph_update(hierarchy_graph* graph);

/**
 * @brief Obtains the xform handle of the node provided. Will return an invalid handle if the
 * node (or an xform for it) does not exist.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to an xform, or an invalid handle if the node (or an xform for it) doesn't exist.
 */
KAPI khandle hierarchy_graph_xform_handle_get(const hierarchy_graph* graph, khandle node_handle);

/**
 * @brief Obtains the handle of the parent of the node provided. Will return an invalid handle if the
 * node does not exist or if it has no parent.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to a node, or an invalid handle if the node (or it's parent) doesn't exist.
 */
KAPI khandle hierarchy_graph_parent_handle_get(const hierarchy_graph* graph, khandle node_handle);

/**
 * @brief Obtains the xform handle of the parent of node provided. Will return an invalid handle if the
 * node, its parent, or an xform for it does not exist.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to an xform, or an invalid handle if the node, its parent, or an xform for the parent doesn't exist.
 */
KAPI khandle hierarchy_graph_parent_xform_handle_get(const hierarchy_graph* graph, khandle node_handle);

/**
 * @brief Adds a new node to the root of the graph. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @returns A copy of the handle to the new node.
 */
KAPI khandle hierarchy_graph_root_add(hierarchy_graph* graph);

/**
 * @brief Adds a new node to the root of the graph and assigns the given xform handle to it. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param xform_handle The handle to the xform to use when adding the node.
 * @returns A copy of the handle to the new node.
 */
KAPI khandle hierarchy_graph_root_add_with_xform(hierarchy_graph* graph, khandle xform_handle);

/**
 * @brief Adds a new node to the given parent node within the graph. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param parent_node_handle The handle to the node to add the new child node to.
 * @returns A copy of the handle to the new node.
 */
KAPI khandle hierarchy_graph_child_add(hierarchy_graph* graph, khandle parent_node_handle);

/**
 * @brief Adds a new node to the given parent node within the graph and assigns the given xform handle to it.
 * Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param parent_node_handle The handle to the node to add the new child node to.
 * @param xform_handle The handle to the xform to use when adding the node.
 * @returns A copy of the handle to the new node.
 */
KAPI khandle hierarchy_graph_child_add_with_xform(hierarchy_graph* graph, khandle parent_node_handle, khandle xform_handle);

/**
 * @brief Removes the given node from the hierarchy. This automatically handles reorganization of the
 * hierarchy upon removal (i.e. children, if they exist, are moved to this node's parent or the root).
 * Also invalidates the given handle. Optionally releases the xform.
 *
 * @param graph A pointer to the graph to remove the node from.
 * @param node_handle A pointer to the handle of the node to be removed. Handle is invalidated upon completion.
 * @param release_xform Indicates if the associated xform (assuming there is one) should also be released.
 */
KAPI void hierarchy_graph_node_remove(hierarchy_graph* graph, khandle* node_handle, b8 release_xform);

/**
 * @brief Obtains the world rotation for the given node. Returns identity quaternion if xform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world rotation.
 */
KAPI quat hierarchy_graph_world_rotation_get(const hierarchy_graph* graph, khandle node_handle);

/**
 * @brief Obtains the world position for the given node. Returns a zero vector if xform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world position.
 */
KAPI vec3 hierarchy_graph_world_position_get(const hierarchy_graph* graph, khandle node_handle);

/**
 * @brief Obtains the world scale for the given node. Returns {1, 1, 1} if xform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world scale.
 */
KAPI vec3 hierarchy_graph_world_scale_get(const hierarchy_graph* graph, khandle node_handle);

#endif
