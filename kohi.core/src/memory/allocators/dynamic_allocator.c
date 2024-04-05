#include "memory/allocators/dynamic_allocator.h"

#include "debug/kassert.h"
#include "memory/kmemory.h"
#include "logger.h"
#include "containers/freelist.h"

typedef struct dynamic_allocator_state {
    u64 total_size;
    freelist list;
    void* freelist_block;
    void* memory_block;
} dynamic_allocator_state;

typedef struct alloc_header {
    void* start;
    u16 alignment;
} alloc_header;

// The storage size in bytes of a node's user memory block size
#define KSIZE_STORAGE sizeof(u32)

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
    return dynamic_allocator_allocate_aligned(allocator, size, 1);
}

void* dynamic_allocator_allocate_aligned(dynamic_allocator* allocator, u64 size, u16 alignment) {
    if (allocator && size && alignment) {
        dynamic_allocator_state* state = allocator->memory;

        // The size required is based on the requested size, plus the alignment, header and a u32 to hold
        // the size for quick/easy lookups.
        u64 header_size = sizeof(alloc_header);
        u64 storage_size = KSIZE_STORAGE;
        u64 required_size = alignment + header_size + storage_size + size;
        // NOTE: This cast will really only be an issue on allocations over ~4GiB, so... don't do that.
        KASSERT_MSG(required_size < 4294967295U, "dynamic_allocator_allocate_aligned called with required size > 4 GiB. Don't do that.");

        u64 base_offset = 0;
        if (freelist_allocate_block(&state->list, required_size, &base_offset)) {
            /*
            Memory layout:
            x bytes/void padding
            4 bytes/u32 user block size
            x bytes/void user memory block
            alloc_header

            */
            // Get the base pointer, or the unaligned memory block.
            void* ptr = (void*)((u64)state->memory_block + base_offset);
            // Start the alignment after enough space to hold a u32. This allows for the u32 to be stored
            // immediately before the user block, while maintaining alignment on said user block.
            u64 aligned_block_offset = get_aligned((u64)ptr + KSIZE_STORAGE, alignment);
            // Store the size just before the user data block
            u32* block_size = (u32*)(aligned_block_offset - KSIZE_STORAGE);
            *block_size = (u32)size;
            // Store the header immediately after the user block.
            alloc_header* header = (alloc_header*)(aligned_block_offset + size);
            header->start = ptr;
            header->alignment = alignment;

            return (void*)aligned_block_offset;
        } else {
            KERROR("dynamic_allocator_allocate_aligned no blocks of memory large enough to allocate from.");
            u64 available = freelist_free_space(&state->list);
            KERROR("Requested size: %llu, total space available: %llu", size, available);
            // TODO: Report fragmentation?
            return 0;
        }
    }

    KERROR("dynamic_allocator_allocate_aligned requires a valid allocator, size and alignment.");
    return 0;
}

b8 dynamic_allocator_free(dynamic_allocator* allocator, void* block, u64 size) {
    return dynamic_allocator_free_aligned(allocator, block);
}

b8 dynamic_allocator_free_aligned(dynamic_allocator* allocator, void* block) {
    if (!allocator || !block) {
        KERROR("dynamic_allocator_free_aligned requires both a valid allocator (0x%p) and a block (0x%p) to be freed.", allocator, block);
        return false;
    }

    dynamic_allocator_state* state = allocator->memory;
    if (block < state->memory_block || block > state->memory_block + state->total_size) {
        void* end_of_block = (void*)(state->memory_block + state->total_size);
        KWARN("dynamic_allocator_free_aligned trying to release block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state->memory_block, end_of_block);
        return false;
    }

    u32* block_size = (u32*)((u64)block - KSIZE_STORAGE);
    alloc_header* header = (alloc_header*)((u64)block + *block_size);
    u64 required_size = header->alignment + sizeof(alloc_header) + KSIZE_STORAGE + *block_size;
    u64 offset = (u64)header->start - (u64)state->memory_block;
    if (!freelist_free_block(&state->list, required_size, offset)) {
        KERROR("dynamic_allocator_free_aligned failed.");
        return false;
    }

    return true;
}

b8 dynamic_allocator_get_size_alignment(void* block, u64* out_size, u16* out_alignment) {
    // Get the header.
    *out_size = *(u32*)((u64)block - KSIZE_STORAGE);
    alloc_header* header = (alloc_header*)((u64)block + *out_size);
    *out_alignment = header->alignment;
    return true;
}

u64 dynamic_allocator_free_space(dynamic_allocator* allocator) {
    dynamic_allocator_state* state = allocator->memory;
    return freelist_free_space(&state->list);
}

u64 dynamic_allocator_total_space(dynamic_allocator* allocator) {
    dynamic_allocator_state* state = allocator->memory;
    return state->total_size;
}

u64 dynamic_allocator_header_size(void) {
    // Enough space for a header and size storage.
    return sizeof(alloc_header) + KSIZE_STORAGE;
}
