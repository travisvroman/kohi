#include "bst_tests.h"

#include "../expect.h"
#include "../test_manager.h"

#include <containers/u32_bst.h>
#include <containers/u64_bst.h>
#include <defines.h>

// Keys inserted in this order give a root (50) with two children, so deleting the root takes
// the two-child path, where the successor's key and value move into the root node.
static const u64 keys[] = {50, 30, 70, 20, 40, 60, 80};
#define KEY_COUNT (sizeof(keys) / sizeof(keys[0]))

static u8 u64_bst_delete_keeps_values_with_keys (void) {
	bt_node *root = 0;
	for (u32 i = 0; i < KEY_COUNT; ++i) {
		bt_node_value value = {.u64 = keys[i] * 10};
		root = u64_bst_insert(root, keys[i], value);
	}

	// Two children: 60 replaces 50 in the root node.
	root = u64_bst_delete(root, 50);
	expect_should_be(0, (u64)u64_bst_find(root, 50));
	for (u32 i = 1; i < KEY_COUNT; ++i) {
		const bt_node *node = u64_bst_find(root, keys[i]);
		expect_should_not_be(0, (u64)node);
		expect_should_be(keys[i] * 10, node->value.u64);
	}

	u64_bst_cleanup(root);
	return true;
}

static u8 u64_bst_delete_returns_new_root (void) {
	bt_node_value value = {.u64 = 1};
	bt_node *root = u64_bst_insert(0, 10, value);
	root = u64_bst_insert(root, 20, value);

	// Root with one child: the child becomes the root.
	root = u64_bst_delete(root, 10);
	expect_should_not_be(0, (u64)root);
	expect_should_be(20, root->key);

	// Last node: the tree becomes empty.
	root = u64_bst_delete(root, 20);
	expect_should_be(0, (u64)root);

	// Deleting from an empty tree is a no-op.
	root = u64_bst_delete(root, 20);
	expect_should_be(0, (u64)root);
	return true;
}

static u8 u32_bst_delete_keeps_values_with_keys (void) {
	u32_bst_node *root = 0;
	for (u32 i = 0; i < KEY_COUNT; ++i) {
		u32_bst_node_value value = {.u32 = (u32)keys[i] * 10};
		root = u32_bst_insert(root, (u32)keys[i], value);
	}

	root = u32_bst_delete(root, 50);
	expect_should_be(0, (u64)u32_bst_find(root, 50));
	for (u32 i = 1; i < KEY_COUNT; ++i) {
		const u32_bst_node *node = u32_bst_find(root, (u32)keys[i]);
		expect_should_not_be(0, (u64)node);
		expect_should_be((u32)keys[i] * 10, node->value.u32);
	}

	// Delete everything; the returned root must end up empty.
	for (u32 i = 1; i < KEY_COUNT; ++i) {
		root = u32_bst_delete(root, (u32)keys[i]);
	}
	expect_should_be(0, (u64)root);
	return true;
}

void bst_register_tests (void) {
	test_manager_register_test(u64_bst_delete_keeps_values_with_keys, "u64 BST delete keeps each value with its key");
	test_manager_register_test(u64_bst_delete_returns_new_root, "u64 BST delete returns the new root");
	test_manager_register_test(u32_bst_delete_keeps_values_with_keys, "u32 BST delete keeps each value with its key");
}
