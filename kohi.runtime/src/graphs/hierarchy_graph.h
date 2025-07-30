/**
 * The hierarchy graph manages a hierarchy of ktransforms. ktransforms themselves know nothing of
 * hierarchy. This is managed instead by this graph. The graph then calls to the
 * ktransform to recalculate based on values it passes.
 *
 * In the update of the graph, the graph is traversed from each root, and recursively updates
 * down the tree. Along the way, the graph reaches out to to the ktransform system and provides the
 * "parent" (if there is one) and caches that result instead.
 *
 * The steps of this update would be:
 * - Build up a list of dirty nodes.
 * - Mark all of the children of those nodes, recursively, as dirty.
 * - Traverse the graph, starting at roots, and store the result of parent*local as the cached matrix
 *   for that node.
 * - Reset the dirty list.
 */

#ifndef _ktransform_GRAPH_H_
#define _ktransform_GRAPH_H_

#include <core_resource_types.h>
#include <math/math_types.h>
#include <systems/ktransform_system.h>

struct frame_data;

typedef struct hierarchy_graph_view_node {
    khierarchy_node node_handle;
    ktransform ktransform_handle;

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
    khierarchy_node* node_handles;
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
    ktransform* ktransform_handles;

    // A view of the tree.
    hierarchy_graph_view view;
} hierarchy_graph;

/**
 * @brief Creates a new hierarchy graph, used to manage parent-child relationships
 * with any kind of resource via the use of handles and handles to ktransforms.
 *
 * @param out_graph A pointer to hold the newly-created graph.
 * @returns True if successful; otherwise false.
 */
KAPI b8 hierarchy_graph_create(hierarchy_graph* out_graph);

/**
 * @brief Destroys the given hierarchy graph, releasing all resources.
 * FIXME: This should also optionally release ktransforms if requested to do so. It currently does not.
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
 * @brief Obtains the ktransform handle of the node provided. Will return an invalid handle if the
 * node (or an ktransform for it) does not exist.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to an ktransform, or an invalid handle if the node (or an ktransform for it) doesn't exist.
 */
KAPI ktransform hierarchy_graph_ktransform_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle);

/**
 * @brief Attempts to get the local transformation matrix for the given hierarchy node, if it exists and has a transform.
 *
 * @param graph A pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @param out_matrix A constant pointer to hold the matrix. Required.
 * @returns True if the node exists and has an existing ktransform; otherwise false.
 */
KAPI b8 hierarchy_graph_ktransform_local_matrix_get(const hierarchy_graph* graph, khierarchy_node node_handle, mat4* out_matrix);

/**
 * @brief Obtains the handle of the parent of the node provided. Will return an invalid handle if the
 * node does not exist or if it has no parent.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to a node, or an invalid handle if the node (or it's parent) doesn't exist.
 */
KAPI khierarchy_node hierarchy_graph_parent_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle);

/**
 * @brief Obtains the ktransform handle of the parent of node provided. Will return an invalid handle if the
 * node, its parent, or an ktransform for it does not exist.
 *
 * @param graph A constant pointer to the graph to examine.
 * @param node_handle The handle to the node to examine.
 * @returns Either a handle to an ktransform, or an invalid handle if the node, its parent, or an ktransform for the parent doesn't exist.
 */
KAPI ktransform hierarchy_graph_parent_ktransform_handle_get(const hierarchy_graph* graph, khierarchy_node node_handle);

/**
 * @brief Adds a new node to the root of the graph. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @returns A copy of the handle to the new node.
 */
KAPI khierarchy_node hierarchy_graph_root_add(hierarchy_graph* graph);

/**
 * @brief Adds a new node to the root of the graph and assigns the given ktransform handle to it. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param ktransform_handle The handle to the ktransform to use when adding the node.
 * @returns A copy of the handle to the new node.
 */
KAPI khierarchy_node hierarchy_graph_root_add_with_ktransform(hierarchy_graph* graph, ktransform ktransform_handle);

/**
 * @brief Adds a new node to the given parent node within the graph. Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param parent_node_handle The handle to the node to add the new child node to.
 * @returns A copy of the handle to the new node.
 */
KAPI khierarchy_node hierarchy_graph_child_add(hierarchy_graph* graph, khierarchy_node parent_node_handle);

/**
 * @brief Adds a new node to the given parent node within the graph and assigns the given ktransform handle to it.
 * Returns the handle to the new node.
 *
 * @param graph A pointer to the graph to add the node to.
 * @param parent_node_handle The handle to the node to add the new child node to.
 * @param ktransform_handle The handle to the ktransform to use when adding the node.
 * @returns A copy of the handle to the new node.
 */
KAPI khierarchy_node hierarchy_graph_child_add_with_ktransform(hierarchy_graph* graph, khierarchy_node parent_node_handle, ktransform ktransform_handle);

/**
 * @brief Attempts to get the number of child nodes of the given parent node.
 *
 * @param graph A pointer to the graph to search.
 * @param parent_node_handle The handle to the parent node.
 * @returns The number of children under the parent. Can also return 0 if the parent was not found.
 */
KAPI u32 hierarchy_graph_child_count_get(const hierarchy_graph* graph, khierarchy_node parent_node_handle);

/**
 * @brief Attempts to get a handle to a child node of the given parent node, at the provided index.
 *
 * @param graph A pointer to the graph to search.
 * @param parent_node_handle The handle to the node to search.
 * @param out_handle A pointer to hold the found handle if successful.
 * @returns True if a handle was found at the index; otherwise false.
 */
KAPI b8 hierarchy_graph_child_get_by_index(const hierarchy_graph* graph, khierarchy_node parent_node_handle, u32 index, khierarchy_node* out_handle);

/**
 * @brief Removes the given node from the hierarchy. This automatically handles reorganization of the
 * hierarchy upon removal (i.e. children, if they exist, are moved to this node's parent or the root).
 * Also invalidates the given handle. Optionally releases the ktransform.
 *
 * @param graph A pointer to the graph to remove the node from.
 * @param node_handle A pointer to the handle of the node to be removed. Handle is invalidated upon completion.
 * @param release_ktransform Indicates if the associated ktransform (assuming there is one) should also be released.
 */
KAPI void hierarchy_graph_node_remove(hierarchy_graph* graph, khierarchy_node* node_handle, b8 release_ktransform);

/**
 * @brief Obtains the world rotation for the given node. Returns identity quaternion if ktransform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world rotation.
 */
KAPI quat hierarchy_graph_world_rotation_get(const hierarchy_graph* graph, khierarchy_node node_handle);

/**
 * @brief Obtains the world position for the given node. Returns a zero vector if ktransform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world position.
 */
KAPI vec3 hierarchy_graph_world_position_get(const hierarchy_graph* graph, khierarchy_node node_handle);

/**
 * @brief Obtains the world scale for the given node. Returns {1, 1, 1} if ktransform (or node itself) does not exist.
 *
 * @param graph A constant pointer to the hierarchy graph to traverse.
 * @param node_handle The handle to the node to examine.
 * @returns The world scale.
 */
KAPI vec3 hierarchy_graph_world_scale_get(const hierarchy_graph* graph, khierarchy_node node_handle);

#endif
