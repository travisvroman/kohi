#include "dynamic_allocator.h"

#include "core/asserts.h"
#include "core/kmemory.h"
#include "core/logger.h"
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

        // NOTE: s
        /*
            Alright, so separating the block and the header (i.e. padding for alignment) won't work, because we need to know
            how far apart they are to extract the header info on a free, and the padding is in the header.

            Sooo we need to find a way, from nothing but a pointer, how to look up the header to get the size and padding so the
            whole thing can be freed.

            Might need to be something like this:
            - get unaligned allocation from free list, call it ptr, of size+alignment bytes.
            - get a new void*, call it res, by passing ptr to get_aligned((u64)ptr, alignment)
              - NOTE: might need to do this:
                - void* res = (void*)(((u64)ptr & ~((u64)(alignment-1))) + alignment);
              - Store the pointer of the unaligned allocation
                *((void**)res - 1) = ptr;
            - return res as the block.

            Freeing:
            - get original pointer by:
              - void* original = *(((void**)ptr) - 1);
              - cast orignal to u64, subtract from state block and pass diff as offset.

            NOTE: This still does not provide a way to store the alignment and size.
            x bytes padding
            8 bytes header
            x bytes/void* the aligned block.

            Getting the header on a free would start at block, go back sizeof(alloc_header) bytes and cast to alloc_header*
            take header->padding and move that amount back to get original pointer.

            alloc would be:

             alignment + sizeof(header) + size;
            void* whole_alloc = freelist_allocate(...)
            u64 block_offset = get_alignment((u64)whole_alloc + sizeof(alloc_header), alignment)// aligning the header will also align the block.
            u16 padding = block_offset - whole_alloc;
            void* block = (void*)block_offset;
            alloc_header* header = ((u64)block - sizeof(alloc_header));
            header->padding = padding;
            header->size = size;
            header->alignment = alignment;
            return block;
        */

        u64 required_size = alignment + sizeof(alloc_header) + sizeof(void*) + size;
        // NOTE: This cast will really only be an issue on allocations over ~4GiB, so... don't do that.
        KASSERT_MSG(required_size < 4294967295U, "dynamic_allocator_allocate_aligned called with required size > 4 GiB. Don't do that.");

        u64 base_offset = 0;
        if (freelist_allocate_block(&state->list, required_size, &base_offset)) {
            void* ptr = (void*)((u64)state->memory_block + base_offset);
            // within the whole allocation, find the aligned block offset based on the base offset and at least enough to hold the size of the allocation.
            u64 aligned_block_offset = get_aligned((u64)ptr + base_offset + sizeof(u32), alignment);
            // Store the size just before the user data block
            u32* block_size = (u32*)(aligned_block_offset - sizeof(u32));
            *block_size = (u32)size;
            // Store the header immediately after the user block.
            alloc_header* header = (alloc_header*)(aligned_block_offset + size);
            header->start = ptr;
            header->alignment = alignment;

            return (void*)aligned_block_offset;

            // u64 block_offset = get_aligned((u64)ptr + sizeof(alloc_header), alignment);
            // u16 padding = (u16)(block_offset - (u64)ptr);
            // void* block = (void*)block_offset;
            // alloc_header* header = (block_offset - sizeof(alloc_header));
            // header->padding = padding;
            // header->alignment = alignment;
            // header->size = size;
            // header->start = ptr;
            // return block;
        } else {
            KERROR("dynamic_allocator_allocate_aligned no blocks of memory large enough to allocate from.");
            u64 available = freelist_free_space(&state->list);
            KERROR("Requested size: %llu, total space available: %llu", size, available);
            // TODO: Report fragmentation?
            return 0;
        }

        // TODO: moving alignment logic all here.
        // u64 required_size = size + alignment - 1 + sizeof(alloc_header);
        // // NOTE: This cast will really only be an issue on allocations over ~4GiB, so... don't do that.
        // KASSERT_MSG(required_size < 4294967295U, "dynamic_allocator_allocate_aligned called with required size > 4 GiB. Don't do that.");

        // // Request a block big enough to hold the required size. Alignment - 1 accounts for worst-case scenario.
        // u64 unaligned_offset = 0;
        // if (freelist_allocate_block(&state->list, required_size, &unaligned_offset)) {
        //     alloc_header* header = ((alloc_header*)(u64)state->memory_block + unaligned_offset);
        //     header->padding = get_aligned((u64)header + sizeof(alloc_header), alignment);
        //     header->size = size;
        //     u64 actual_required_size = sizeof(alloc_header) + header->padding + header->size;

        //     i64 size_diff = actual_required_size - required_size;
        //     if (size_diff != 0) {
        //         // Attempt to adjust node size down to the actual required size.
        //         if (!freelist_adjust_allocation(&state->list, unaligned_offset, actual_required_size)) {
        //             KERROR("Failed to adjust freelist allocation. This likely indicated heap corruption.");
        //             return 0;
        //         }
        //     }

        //     void* start = (void*)((u64)header + sizeof(alloc_header) + header->padding);  // This is the pointer to be returned.
        //     return start;
        // }

        // u64 offset = 0;
        // // Account for space for the header.
        // u64 actual_size = size + sizeof(alloc_header);
        // u16 alignment_offset = 0;

        // // Attempt to allocate from the freelist.
        // void* block = 0;
        // if (freelist_allocate_block_aligned(&state->list, actual_size, alignment, &offset, &alignment_offset)) {
        //     // Set the header info.
        //     alloc_header* header = (alloc_header*)(((u8*)state->memory_block) + offset);
        //     header->alignment = alignment;
        //     header->alignment_offset = alignment_offset;
        //     header->size = size;  // Store the actual size here.
        //     // Block is state->memoryblock, then offset, then after the header.
        //     block = (void*)(((u8*)state->memory_block) + offset + sizeof(alloc_header));
        // } else {
        //     KERROR("dynamic_allocator_allocate_aligned no blocks of memory large enough to allocate from.");
        //     u64 available = freelist_free_space(&state->list);
        //     KERROR("Requested size: %llu, total space available: %llu", size, available);
        //     // TODO: Report fragmentation?
        //     block = 0;
        // }
        // return block;
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
        KERROR("dynamic_allocator_free_aligned trying to release block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state->memory_block, end_of_block);
        return false;
    }

    u32* block_size = (u32*)((u64)block - sizeof(u32));
    alloc_header* header = (alloc_header*)((u64)block + *block_size);
    u64 offset = (u64)header->start - (u64)state->memory_block;
    if (!freelist_free_block(&state->list, *block_size, offset)) {
        KERROR("dynamic_allocator_free_aligned failed.");
        return false;
    }

    // u64 offset = (block - state->memory_block);
    // // Get the header.
    // alloc_header* header = (alloc_header*)(((u8*)block) - sizeof(alloc_header));
    // u64 actual_size = header->size + sizeof(alloc_header);
    // if (!freelist_free_block_aligned(&state->list, actual_size, offset - sizeof(alloc_header), header->alignment_offset)) {
    //     KERROR("dynamic_allocator_free_aligned failed.");
    //     return false;
    // }

    return true;
}

b8 dynamic_allocator_get_size_alignment(void* block, u64* out_size, u16* out_alignment) {
    // Get the header.
    *out_size = *(u32*)((u64)block - sizeof(u32));
    alloc_header* header = (alloc_header*)((u64)block + *out_size);
    *out_alignment = header->alignment;
    // alloc_header* header = (alloc_header*)(block - sizeof(alloc_header));
    // *out_size = header->size;
    // *out_alignment = header->alignment;
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
