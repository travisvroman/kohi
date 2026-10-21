/**
 * @file u32_bst.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains an implementation of a binary search tree that
 * uses a 32-bit unsigned int key. It is otherwise functionally identical to u64_bst, and
 * exists solely to save memory where such a large key isn't required.
 *
 * @version 1.0
 * @date 2026-10-05
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#ifndef _KOHI_U32_BST_H_
#define _KOHI_U32_BST_H_

#include "defines.h"

// Represents the value of a binary tree node.
typedef union u32_bst_node_value {
	void *p;
	const char *str;
	u64 u64;
	i64 i64;
	u32 u32;
	i32 i32;
	u16 u16;
	i16 i16;
	u8 u8;
	i8 i8;
	b8 b8;
	f32 f32;
} u32_bst_node_value;

/**
 * A binary tree node, which also represents the base node of the BST itself.
 */
typedef struct u32_bst_node {
	u32 key;
	u32_bst_node_value value;
	struct u32_bst_node *left;
	struct u32_bst_node *right;
} u32_bst_node;

/**
 * Inserts a node into the given tree (represented by the root node).
 *
 * @param root A pointer to the root node.
 * @param key The key to be inserted.
 * @param value The value to be inserted. NOTE: The BST does NOT take its own copy of this data.
 * @returns A pointer to the inserted node. This should be saved off if creating the root node.
 */
KAPI u32_bst_node *u32_bst_insert (u32_bst_node *root, u32 key, u32_bst_node_value value);

/**
 * Attempts to delete a node with the given key from the tree.
 * This should be cleaned up by the caller.
 *
 * @param root A pointer to the node to begin the search from.
 * @param key The key to be deleted.
 * @returns A pointer to the deleted key, if found; otherwise 0.
 */
KAPI u32_bst_node *u32_bst_delete (u32_bst_node *root, u32 key);

/**
 * Attempts to find a node with the given key.
 *
 * @param root A constant pointer to the root node to search from.
 * @param key The key to search for.
 * @returns A constant pointer to the node, if found; otherwise 0/null.
 */
KAPI const u32_bst_node *u32_bst_find (const u32_bst_node *root, u32 key);

/**
 * Performs cleanup operations on the given node and its branches.
 * Recursive.
 *
 * @param node A pointer to the node to cleanup.
 */
KAPI void u32_bst_cleanup (u32_bst_node *node);

/**
 * Performs cleanup operations on the given node and its branches.
 * Recursive. Treats all values as strings and frees them.
 *
 * @param node A pointer to the node to cleanup.
 */
KAPI void u32_bst_cleanup_with_strings (u32_bst_node *node);

#endif
