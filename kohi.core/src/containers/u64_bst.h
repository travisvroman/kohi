#pragma once

#include "defines.h"

// Represents the value of a binary tree node.
typedef union bt_node_value {
    void* p;
    const char* str;
    u64 u64;
    i64 i64;
    u32 u32;
    i32 i32;
    u16 u16;
    i16 i16;
    u8 u8;
    i8 i8;
    b8 b8;
} bt_node_value;

/**
 * A binary tree node, which also represents the base node of the BST itself.
 */
typedef struct bt_node {
    u64 key;
    bt_node_value value;
    struct bt_node* left;
    struct bt_node* right;
} bt_node;

/**
 * Inserts a node into the given tree (represented by the root node).
 *
 * @param root A pointer to the root node.
 * @param key The key to be inserted.
 * @param value The value to be inserted. NOTE: The BST does NOT take its own copy of this data.
 * @returns A pointer to the inserted node. This should be saved off if creating the root node.
 */
KAPI bt_node* u64_bst_insert(bt_node* root, u64 key, bt_node_value value);

/**
 * Attempts to delete a node with the given key from the tree.
 * This should be cleaned up by the caller.
 *
 * @param root A pointer to the node to begin the search from.
 * @param key The key to be deleted.
 * @returns A pointer to the deleted key, if found; otherwise 0.
 */
KAPI bt_node* u64_bst_delete(bt_node* root, u64 key);

/**
 * Attempts to find a node with the given key.
 *
 * @param root A constant pointer to the root node to search from.
 * @param key The key to search for.
 * @returns A constant pointer to the node, if found; otherwise 0/null.
 */
KAPI const bt_node* u64_bst_find(const bt_node* root, u64 key);

/**
 * Performs cleanup operations on the given node and its branches.
 * Recursive.
 *
 * @param node A pointer to the node to cleanup.
 */
KAPI void u64_bst_cleanup(bt_node* node);
