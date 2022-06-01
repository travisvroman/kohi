#include "dynamic_allocator.h"

#include "core/kmemory.h"
#include "core/logger.h"
#include "containers/freelist.h"

typedef struct dynamic_allocator_state {
    u64 total_size;
    freelist list;
    void* freelist_block;
    void* memory_block;
} dynamic_allocator_state;

b8 dynamic_allocator_create(u64 total_size, u64* memory_requirement, void* memory, dynamic_allocator* out_allocator) {
    if (total_size < 1) {
        KERROR("dynamic_allocator_create cannot have a total_size of 0. Create failed.");
        return false;
    }
    if (!memory_requirement) {
        KERROR("dynamic_allocator_create requires memory_requirement to exist. Create failed.");
        return false;
    }
    u64 freelist_requirement = 0;
    // Grab the memory requirement for the free list first.
    freelist_create(total_size, &freelist_requirement, 0, 0);

    *memory_requirement = freelist_requirement + sizeof(dynamic_allocator_state) + total_size;

    // If only obtaining requirement, boot out.
    if (!memory) {
        return true;
    }

    // Memory layout:
    // state
    // freelist block
    // memory block
    out_allocator->memory = memory;
    dynamic_allocator_state* state = out_allocator->memory;
    state->total_size = total_size;
    state->freelist_block = (void*)(out_allocator->memory + sizeof(dynamic_allocator_state));
    state->memory_block = (void*)(state->freelist_block + freelist_requirement);

    // Actually create the freelist
    freelist_create(total_size, &freelist_requirement, state->freelist_block, &state->list);

    kzero_memory(state->memory_block, total_size);
    return true;
}

b8 dynamic_allocator_destroy(dynamic_allocator* allocator) {
    if (allocator) {
        dynamic_allocator_state* state = allocator->memory;
        freelist_destroy(&state->list);
        kzero_memory(state->memory_block, state->total_size);
        state->total_size = 0;
        allocator->memory = 0;
        return true;
    }

    KWARN("dynamic_allocator_destroy requires a pointer to an allocator. Destroy failed.");
    return false;
}

void* dynamic_allocator_allocate(dynamic_allocator* allocator, u64 size) {
    if (allocator && size) {
        dynamic_allocator_state* state = allocator->memory;
        u64 offset = 0;
        // Attempt to allocate from the freelist.
        if (freelist_allocate_block(&state->list, size, &offset)) {
            // Use that offset against the base memory block to get the block.
            void* block = (void*)(state->memory_block + offset);
            return block;
        } else {
            KERROR("dynamic_allocator_allocate no blocks of memory large enough to allocate from.");
            u64 available = freelist_free_space(&state->list);
            KERROR("Requested size: %llu, total space available: %llu", size, available);
            // TODO: Report fragmentation?
            return 0;
        }
    }

    KERROR("dynamic_allocator_allocate requires a valid allocator and size.");
    return 0;
}

b8 dynamic_allocator_free(dynamic_allocator* allocator, void* block, u64 size) {
    if (!allocator || !block || !size) {
        KERROR("dynamic_allocator_free requires both a valid allocator (0x%p) and a block (0x%p) to be freed.", allocator, block);
        return false;
    }

    dynamic_allocator_state* state = allocator->memory;
    if (block < state->memory_block || block > state->memory_block + state->total_size) {
        void* end_of_block = (void*)(state->memory_block + state->total_size);
        KERROR("dynamic_allocator_free trying to release block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state->memory_block, end_of_block);
        return false;
    }
    u64 offset = (block - state->memory_block);
    if (!freelist_free_block(&state->list, size, offset)) {
        KERROR("dynamic_allocator_free failed.");
        return false;
    }

    return true;
}

u64 dynamic_allocator_free_space(dynamic_allocator* allocator) {
    dynamic_allocator_state* state = allocator->memory;
    return freelist_free_space(&state->list);
}
