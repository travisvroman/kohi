#include "kpool.h"
#include "logger.h"
#include "memory/kmemory.h"

typedef struct pool_element_header {
    b8 allocated;
} pool_element_header;

b8 kpool_create(u32 element_size, u32 capacity, kpool* out_pool) {
    if (!element_size || !capacity || !out_pool) {
        return false;
    }

    u64 required_bytes = element_size * capacity * sizeof(pool_element_header);
    out_pool->block = kallocate(required_bytes, MEMORY_TAG_POOL);
    if (!out_pool) {
        return false;
    }
    out_pool->capacity = capacity;
    out_pool->element_size = element_size;
    out_pool->allocated_count = 0;

    return true;
}

void kpool_destroy(kpool* pool) {
    if (pool) {
        if (pool->block) {
            kfree(pool->block, pool->element_size * pool->capacity * sizeof(pool_element_header), MEMORY_TAG_POOL);
        }
        kzero_memory(pool, sizeof(kpool));
    }
}

void* kpool_allocate(kpool* pool, u32* out_index) {
    if (!pool) {
        return 0;
    }

    if (pool->allocated_count >= pool->capacity) {
        KERROR("Pool is full! (capacity=%llu)", pool->capacity);
        return 0;
    }

    u64 actual_element_size = sizeof(pool_element_header) + pool->element_size;
    for (u32 i = 0; i < pool->capacity; ++i) {
        u64 element_offset = (actual_element_size * i);
        pool_element_header* header = (pool_element_header*)((u64)pool->block + element_offset);
        if (!header->allocated) {
            header->allocated = true;

            *out_index = i;

            // Return the memory after the header as the actual element.
            return (void*)((u64)pool->block + element_offset + sizeof(pool_element_header));
        }
    }

    // If reached the end but also failed the check above, memory corruption is likely.
    KFATAL("kpool_allocate failed to find a free space, but the allocation count succeeded. Memory corruption is likely.");
    return 0;
}

void kpool_free(kpool* pool, void* element) {
    if (pool && element) {
        u64 actual_element_size = sizeof(pool_element_header) + pool->element_size;
        u64 element_offset = (u64)element - sizeof(pool_element_header);
        u64 pool_start = (u64)pool->block;
        if (element_offset < pool_start || element_offset >= (pool_start + actual_element_size * pool->capacity)) {
            KERROR("kpool_free was asked to free an element which is out of range.");
            return;
        }

        // Verify the offset is valid
        if (element_offset % actual_element_size) {
            KERROR("kpool_free called with element address %p that is not aligned correctly within the pool. Possible memory corruption.");
            return;
        }

        pool_element_header* header = (pool_element_header*)element_offset;
        header->allocated = false;
    }
}

void kpool_free_by_index(kpool* pool, u32 index) {
    if (pool) {
        if (index >= pool->capacity) {
            KERROR("kpool_free_by_index was asked to free an index which is out of range.");
            return;
        }

        u64 actual_element_size = sizeof(pool_element_header) + pool->element_size;
        u64 pool_start = (u64)pool->block;
        u64 element_offset = actual_element_size * index;
        u64 element = pool_start + element_offset;

        pool_element_header* header = (pool_element_header*)element;
        header->allocated = false;
    }
}

void* kpool_get_by_index(kpool* pool, u32 index) {
    if (pool) {
        if (index >= pool->capacity) {
            KERROR("kpool_get_by_index was asked to free an index which is out of range.");
            return 0;
        }

        u64 actual_element_size = sizeof(pool_element_header) + pool->element_size;
        u64 pool_start = (u64)pool->block;
        u64 element_offset = actual_element_size * index;
        u64 element = pool_start + element_offset;

        pool_element_header* header = (pool_element_header*)element;
        if (!header->allocated) {
            KERROR("kpool_get_by_index - Attempted to get index which is not allocated.");
            return 0;
        }

        return (void*)(element + sizeof(pool_element_header));
    }

    return 0;
}
