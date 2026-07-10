#include "pool_allocator.h"
#include "debug/kassert.h"
#include "memory/kmemory.h"

pool_allocator pool_allocator_create (u64 element_size, u64 capacity) {
	KASSERT_DEBUG(element_size);
	KASSERT_DEBUG(capacity);

	pool_allocator allocator = {
		.capacity = capacity,
		.element_size = element_size,
		.memory = kallocate(element_size * capacity, MEMORY_TAG_POOL_ALLOCATOR),
		.free_list_nodes = KALLOC_TYPE_CARRAY(pool_allocator_free_node, capacity),
		.free_list_head = 0};

	allocator.free_list_head = &allocator.free_list_nodes[0];

	// Link each node to the next in the array as the default free list.
	for (u32 i = 0; i < capacity - 1; ++i) {
		allocator.free_list_nodes[i].next = &allocator.free_list_nodes[i + 1];
		allocator.free_list_nodes[i].offset = (i * element_size);
	}
	allocator.free_list_nodes[capacity - 1].next = 0;
	allocator.free_list_nodes[capacity - 1].offset = (capacity - 1) * element_size;

	return allocator;
}
void pool_allocator_destroy (pool_allocator *allocator) {
}

void *pool_allocator_allocate (pool_allocator *allocator, u32 *out_index) {
	pool_allocator_free_node *head = allocator->free_list_head;
	KASSERT_DEBUG(head);
	u64 offset = head->offset;
	allocator->free_list_head = head->next;

	*out_index = (u32)(offset / allocator->element_size);

	return (void *)(((u8 *)allocator->memory) + offset);
}
void pool_allocator_free (pool_allocator *allocator, void *block) {
	// Ensure the block is within range.
	KASSERT_DEBUG(block >= allocator->memory);
	KASSERT_DEBUG((u8 *)block < (u8 *)allocator->memory + allocator->element_size * allocator->capacity);

	u64 offset = (u8 *)block - (u8 *)allocator->memory;
	u32 index = (u32)(offset / allocator->element_size);
	pool_allocator_free_node *freed = &allocator->free_list_nodes[index];
	pool_allocator_free_node **cur = &allocator->free_list_head;
	while (*cur && (*cur)->offset < offset) {
		cur = &(*cur)->next;
	}

	freed->next = *cur;
	*cur = freed;
}

u32 pool_allocator_elements_free (const pool_allocator *allocator) {
	u32 count = 0;
	pool_allocator_free_node *node = allocator->free_list_head;
	while (node) {
		count++;
		node = node->next;
	}
	return count;
}
u64 pool_allocator_space_free (const pool_allocator *allocator) {
	u32 free_count = pool_allocator_elements_free(allocator);
	return allocator->element_size * free_count;
}
