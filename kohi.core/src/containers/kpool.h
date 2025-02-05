#pragma once

#include "defines.h"

typedef struct kpool {
    u32 element_size;
    u32 capacity;
    u32 allocated_count;
    void* block;
} kpool;

KAPI b8 kpool_create(u32 element_size, u32 capacity, kpool* out_pool);

KAPI void kpool_destroy(kpool* pool);

KAPI void* kpool_allocate(kpool* pool, u32* out_index);

KAPI void kpool_free(kpool* pool, void* element);

KAPI void kpool_free_by_index(kpool* pool, u32 index);

KAPI void* kpool_get_by_index(kpool* pool, u32 index);
