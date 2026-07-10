#pragma once

#include "defines.h"

typedef struct pool_allocator_free_node {
	u64 offset;
	struct pool_allocator_free_node *next;
} pool_allocator_free_node;

typedef struct pool_allocator {
	void *memory;
	pool_allocator_free_node *free_list_nodes;
	u64 element_size;
	u64 capacity;
	pool_allocator_free_node *free_list_head;
} pool_allocator;

KAPI pool_allocator pool_allocator_create (u64 element_size, u64 capacity);
KAPI void pool_allocator_destroy (pool_allocator *allocator);

KAPI void *pool_allocator_allocate (pool_allocator *allocator, u32 *out_index);
KAPI void pool_allocator_free (pool_allocator *allocator, void *block);

KAPI u32 pool_allocator_elements_free (const pool_allocator *allocator);
KAPI u64 pool_allocator_space_free (const pool_allocator *allocator);
