#include "u64_bst.h"
#include "defines.h"
#include "memory/kmemory.h"

static bt_node* node_create(u64 key, bt_node_value value) {
    bt_node* node = kallocate(sizeof(bt_node), MEMORY_TAG_BST);
    node->key = key;
    node->value = value;
    node->left = node->right = 0;
    return node;
}

static bt_node* find_min(bt_node* root) {
    if (!root) {
        return 0;
    } else if (root->left) {
        return find_min(root->left);
    }
    return root;
}

bt_node* u64_bst_insert(bt_node* root, u64 key, bt_node_value value) {
    if (!root) {
        return node_create(key, value);
    }
    if (key < root->key) {
        root->left = u64_bst_insert(root->left, key, value);
    } else if (key > root->key) {
        root->right = u64_bst_insert(root->right, key, value);
    }
    return root;
}

KAPI bt_node* u64_bst_delete(bt_node* root, u64 key) {
    if (!root) {
        return 0;
    }
    if (key > root->key) {
        root->right = u64_bst_delete(root->right, key);
    } else if (key < root->key) {
        root->left = u64_bst_delete(root->left, key);
    } else {
        if (!root->left && !root->right) {
            kfree(root, sizeof(bt_node), MEMORY_TAG_BST);
            return 0;
        } else if (!root->left || !root->right) {
            bt_node* temp;
            if (!root->left) {
                temp = root->right;
            } else {
                temp = root->left;
            }
            kfree(root, sizeof(bt_node), MEMORY_TAG_BST);
            return temp;
        } else {
            bt_node* temp = find_min(root->right);
            root->key = temp->key;
            root->right = u64_bst_delete(root->right, temp->key);
        }
    }
    return root;
}

const bt_node* u64_bst_find(const bt_node* root, u64 key) {
    if (root == 0 || key == root->key) {
        return root;
    }
    if (root->key < key) {
        return u64_bst_find(root->right, key);
    }
    return u64_bst_find(root->left, key);
}

void u64_bst_cleanup(bt_node* node) {
    if (node) {
        if (node->left) {
            u64_bst_cleanup(node->left);
            node->left = 0;
        }
        if (node->right) {
            u64_bst_cleanup(node->right);
            node->right = 0;
        }
        kfree(node, sizeof(bt_node), MEMORY_TAG_BST);
    }
}
