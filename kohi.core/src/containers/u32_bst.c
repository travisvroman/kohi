#include "u32_bst.h"
#include "defines.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

static u32_bst_node *node_create (u32 key, u32_bst_node_value value) {
	u32_bst_node *node = kallocate(sizeof(u32_bst_node), MEMORY_TAG_BST);
	node->key = key;
	node->value = value;
	node->left = node->right = KNULL;
	return node;
}

static u32_bst_node *find_min (u32_bst_node *root) {
	if (!root) {
		return KNULL;
	} else if (root->left) {
		return find_min(root->left);
	}
	return root;
}

u32_bst_node *u32_bst_insert (u32_bst_node *root, u32 key, u32_bst_node_value value) {
	if (!root) {
		return node_create(key, value);
	}
	if (key < root->key) {
		root->left = u32_bst_insert(root->left, key, value);
	} else if (key > root->key) {
		root->right = u32_bst_insert(root->right, key, value);
	}
	return root;
}

u32_bst_node *u32_bst_delete (u32_bst_node *root, u32 key) {
	if (!root) {
		return KNULL;
	}
	if (key > root->key) {
		root->right = u32_bst_delete(root->right, key);
	} else if (key < root->key) {
		root->left = u32_bst_delete(root->left, key);
	} else {
		if (!root->left && !root->right) {
			kfree(root);
			return KNULL;
		} else if (!root->left || !root->right) {
			u32_bst_node *temp;
			if (!root->left) {
				temp = root->right;
			} else {
				temp = root->left;
			}
			kfree(root);
			return temp;
		} else {
			u32_bst_node *temp = find_min(root->right);
			// Move the successor's key and value into this node, then delete the successor.
			root->key = temp->key;
			root->value = temp->value;
			root->right = u32_bst_delete(root->right, temp->key);
		}
	}
	return root;
}

const u32_bst_node *u32_bst_find (const u32_bst_node *root, u32 key) {
	if (root == KNULL || key == root->key) {
		return root;
	}
	if (root->key < key) {
		return u32_bst_find(root->right, key);
	}
	return u32_bst_find(root->left, key);
}

void u32_bst_cleanup (u32_bst_node *node) {
	if (node) {
		if (node->left) {
			u32_bst_cleanup(node->left);
			node->left = KNULL;
		}
		if (node->right) {
			u32_bst_cleanup(node->right);
			node->right = KNULL;
		}
		kfree(node);
	}
}

void u32_bst_cleanup_with_strings (u32_bst_node *node) {
	if (node) {
		if (node->left) {
			u32_bst_cleanup_with_strings(node->left);
			node->left = KNULL;
		}
		if (node->right) {
			u32_bst_cleanup_with_strings(node->right);
			node->right = KNULL;
		}
		if (node->value.str) {
			string_free(node->value.str);
		}
		kfree(node);
	}
}
