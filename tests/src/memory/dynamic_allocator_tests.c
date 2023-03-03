#include "dynamic_allocator_tests.h"
#include "../test_manager.h"
#include "../expect.h"

#include <defines.h>

#include <core/kmemory.h>
#include <memory/dynamic_allocator.h>

u8 dynamic_allocator_should_create_and_destroy(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_single_allocation_all_space(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    const u64 allocator_size = 1024;
    const u64 alignment = 1;
    // Total size needed, including headers.
    const u64 total_allocator_size = allocator_size + (dynamic_allocator_header_size() + alignment);
    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate the whole thing.
    void* block = dynamic_allocator_allocate(&alloc, 1024);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(0, free_space);

    // Free the allocation
    dynamic_allocator_free(&alloc, block, 1024);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_multi_allocation_all_space(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;

    const u64 allocator_size = 1024;
    const u64 alignment = 1;
    u64 header_size = (dynamic_allocator_header_size() + alignment);
    // Total size needed, including headers.
    const u64 total_allocator_size = allocator_size + (header_size * 3);

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768 + (header_size * 2), free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256 + (header_size * 1), free_space);

    // Allocate the last part of the block.
    void* block3 = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block3);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(0, free_space);

    // Free the allocations, out of order, and verify free space
    dynamic_allocator_free(&alloc, block3, 256);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256 + (header_size * 1), free_space);

    // Free the next allocation, out of order
    dynamic_allocator_free(&alloc, block, 256);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(512 + (header_size * 2), free_space);

    // Free the final allocation, out of order
    dynamic_allocator_free(&alloc, block2, 512);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_multi_allocation_over_allocate(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;

    const u64 allocator_size = 1024;
    const u64 alignment = 1;
    u64 header_size = (dynamic_allocator_header_size() + alignment);
    // Total size needed, including headers.
    const u64 total_allocator_size = allocator_size + (header_size * 3);

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768 + (header_size * 2), free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256 + (header_size * 1), free_space);

    // Allocate the last part of the block.
    void* block3 = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block3);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(0, free_space);

    // Attempt one more allocation, deliberately trying to overflow
    KDEBUG("Note: The following warning and errors are intentionally caused by this test.");
    void* fail_block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_be(0, fail_block);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(0, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_multi_allocation_most_space_request_too_big(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;

    const u64 allocator_size = 1024;
    const u64 alignment = 1;
    u64 header_size = (dynamic_allocator_header_size() + alignment);
    // Total size needed, including headers.
    const u64 total_allocator_size = allocator_size + (header_size * 3);

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768 + (header_size * 2), free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256 + (header_size * 1), free_space);

    // Allocate the last part of the block.
    void* block3 = dynamic_allocator_allocate(&alloc, 128);
    expect_should_not_be(0, block3);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(128 + (header_size * 0), free_space);

    // Attempt one more allocation, deliberately trying to overflow
    KDEBUG("Note: The following warning and errors are intentionally caused by this test.");
    void* fail_block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_be(0, fail_block);

    // Verify free space. Should not have changed.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(128 + (header_size * 0), free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_single_alloc_aligned(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    const u64 allocator_size = 1024;
    const u64 alignment = 16;
    // Total size needed, including headers.
    const u64 total_allocator_size = allocator_size + (dynamic_allocator_header_size() + alignment);
    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate the whole thing.
    void* block = dynamic_allocator_allocate_aligned(&alloc, 1024, alignment);
    expect_should_not_be(0, block);

    // Verify size and alignment
    u64 block_size;
    u16 block_alignment;
    result = dynamic_allocator_get_size_alignment(block, &block_size, &block_alignment);
    expect_to_be_true(result);
    expect_should_be(alignment, block_alignment);
    expect_should_be(1024, block_size);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(0, free_space);

    // Free the allocation
    dynamic_allocator_free_aligned(&alloc, block);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

typedef struct alloc_data {
    void* block;
    u16 alignment;
    u64 size;
} alloc_data;

u8 dynamic_allocator_multiple_alloc_aligned_different_alignments(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    u64 currently_allocated = 0;
    const u64 header_size = dynamic_allocator_header_size();

    const u32 alloc_data_count = 4;
    alloc_data alloc_datas[4];
    alloc_datas[0] = (alloc_data){0, 1, 31};   // 1-byte alignment.
    alloc_datas[1] = (alloc_data){0, 16, 82};  // 16-byte alignment.
    alloc_datas[2] = (alloc_data){0, 1, 59};   // 1-byte alignment.
    alloc_datas[3] = (alloc_data){0, 8, 73};   // 1-byte alignment.
    // Total size needed, including headers.
    u64 total_allocator_size = 0;
    for (u32 i = 0; i < alloc_data_count; ++i) {
        total_allocator_size += alloc_datas[i].alignment + header_size + alloc_datas[i].size;
    }

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate the first one.
    {
        u32 idx = 0;
        alloc_datas[idx].block = dynamic_allocator_allocate_aligned(&alloc, alloc_datas[idx].size, alloc_datas[idx].alignment);
        expect_should_not_be(0, alloc_datas[idx].block);

        // Verify size and alignment
        u64 block_size;
        u16 block_alignment;
        result = dynamic_allocator_get_size_alignment(alloc_datas[idx].block, &block_size, &block_alignment);
        expect_to_be_true(result);
        expect_should_be(alloc_datas[idx].alignment, block_alignment);
        expect_should_be(alloc_datas[idx].size, block_size);

        // Track it.
        currently_allocated += (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Allocate the second one.
    {
        u32 idx = 1;
        alloc_datas[idx].block = dynamic_allocator_allocate_aligned(&alloc, alloc_datas[idx].size, alloc_datas[idx].alignment);
        expect_should_not_be(0, alloc_datas[idx].block);

        // Verify size and alignment
        u64 block_size;
        u16 block_alignment;
        result = dynamic_allocator_get_size_alignment(alloc_datas[idx].block, &block_size, &block_alignment);
        expect_to_be_true(result);
        expect_should_be(alloc_datas[idx].alignment, block_alignment);
        expect_should_be(alloc_datas[idx].size, block_size);

        // Track it.
        currently_allocated += (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Allocate the third one.
    {
        u32 idx = 2;
        alloc_datas[idx].block = dynamic_allocator_allocate_aligned(&alloc, alloc_datas[idx].size, alloc_datas[idx].alignment);
        expect_should_not_be(0, alloc_datas[idx].block);

        // Verify size and alignment
        u64 block_size;
        u16 block_alignment;
        result = dynamic_allocator_get_size_alignment(alloc_datas[idx].block, &block_size, &block_alignment);
        expect_to_be_true(result);
        expect_should_be(alloc_datas[idx].alignment, block_alignment);
        expect_should_be(alloc_datas[idx].size, block_size);

        // Track it.
        currently_allocated += (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Allocate the fourth one.
    {
        u32 idx = 3;
        alloc_datas[idx].block = dynamic_allocator_allocate_aligned(&alloc, alloc_datas[idx].size, alloc_datas[idx].alignment);
        expect_should_not_be(0, alloc_datas[idx].block);

        // Verify size and alignment
        u64 block_size;
        u16 block_alignment;
        result = dynamic_allocator_get_size_alignment(alloc_datas[idx].block, &block_size, &block_alignment);
        expect_to_be_true(result);
        expect_should_be(alloc_datas[idx].alignment, block_alignment);
        expect_should_be(alloc_datas[idx].size, block_size);

        // Track it.
        currently_allocated += (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Free the allocations out of order.

    // Free the second allocation
    {
        u32 idx = 1;
        dynamic_allocator_free_aligned(&alloc, alloc_datas[idx].block);
        alloc_datas[idx].block = 0;
        currently_allocated -= (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space.
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }
    // Free the fourth allocation
    {
        u32 idx = 3;
        dynamic_allocator_free_aligned(&alloc, alloc_datas[idx].block);
        alloc_datas[idx].block = 0;
        currently_allocated -= (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space.
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Free the third allocation
    {
        u32 idx = 2;
        dynamic_allocator_free_aligned(&alloc, alloc_datas[idx].block);
        alloc_datas[idx].block = 0;
        currently_allocated -= (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space.
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Free the first allocation
    {
        u32 idx = 0;
        dynamic_allocator_free_aligned(&alloc, alloc_datas[idx].block);
        alloc_datas[idx].block = 0;
        currently_allocated -= (alloc_datas[idx].size + header_size + alloc_datas[idx].alignment);

        // Verify free space.
        free_space = dynamic_allocator_free_space(&alloc);
        expect_should_be(total_allocator_size - currently_allocated, free_space);
    }

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 util_allocate(dynamic_allocator* allocator, alloc_data* data, u64* currently_allocated, u64 header_size, u64 total_allocator_size) {
    data->block = dynamic_allocator_allocate_aligned(allocator, data->size, data->alignment);
    expect_should_not_be(0, data->block);

    // Verify size and alignment
    u64 block_size;
    u16 block_alignment;
    b8 result = dynamic_allocator_get_size_alignment(data->block, &block_size, &block_alignment);
    expect_to_be_true(result);
    expect_should_be(data->alignment, block_alignment);
    expect_should_be(data->size, block_size);

    // Track it.
    *currently_allocated += (data->size + header_size + data->alignment);

    // Verify free space
    u64 free_space = dynamic_allocator_free_space(allocator);
    expect_should_be(total_allocator_size - *currently_allocated, free_space);

    return true;
}

u8 util_free(dynamic_allocator* allocator, alloc_data* data, u64* currently_allocated, u64 header_size, u64 total_allocator_size) {
    if (!dynamic_allocator_free_aligned(allocator, data->block)) {
        KERROR("util_free, dynamic_allocator_free_aligned failed");
        return false;
    }
    data->block = 0;
    currently_allocated -= (data->size + header_size + data->alignment);

    // Verify free space.
    u64 free_space = dynamic_allocator_free_space(allocator);
    expect_should_be(total_allocator_size - *currently_allocated, free_space);

    return true;
}

u8 dynamic_allocator_multiple_alloc_aligned_different_alignments_random(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    u64 currently_allocated = 0;
    const u64 header_size = dynamic_allocator_header_size();

    const u32 alloc_data_count = 65556;
    alloc_data alloc_datas[65556] = {0};
    u16 po2[8] = {1, 2, 4, 8, 16, 32, 64, 128};

    // Pick random sizes and alignments.
    for (u32 i = 0; i < alloc_data_count; ++i) {
        alloc_datas[i].alignment = po2[krandom_in_range(0, 7)];
        alloc_datas[i].size = (u64)krandom_in_range(1, 65536);
    }

    // Total size needed, including headers.
    u64 total_allocator_size = 0;
    for (u32 i = 0; i < alloc_data_count; ++i) {
        total_allocator_size += alloc_datas[i].alignment + header_size + alloc_datas[i].size;
    }

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    // Allocate randomly until all have been allocated.
    u32 alloc_count = 0;
    while (alloc_count != alloc_data_count) {
        u32 index = (u32)krandom_in_range(0, alloc_data_count);
        if (alloc_datas[index].block == 0) {
            if (!util_allocate(&alloc, &alloc_datas[index], &currently_allocated, header_size, total_allocator_size)) {
                KERROR("util_allocate failed on index: %u.");
                return false;
            }
            alloc_count++;
        }
    }

    KTRACE("Randomly allocated %u times. Freeing randomly...", alloc_count);

    // Should all be allocated at this point, free randomly.
    while (alloc_count != alloc_data_count) {
        u32 index = (u32)krandom_in_range(0, alloc_data_count);
        if (alloc_datas[index].block != 0) {
            if (!util_free(&alloc, &alloc_datas[index], &currently_allocated, header_size, total_allocator_size)) {
                KERROR("util_free failed on index: %u.");
                return false;
            }
            alloc_count--;
        }
    }

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

u8 dynamic_allocator_multiple_alloc_and_free_aligned_different_alignments_random(void) {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    u64 currently_allocated = 0;
    const u64 header_size = dynamic_allocator_header_size();

    const u32 alloc_data_count = 65556;
    alloc_data alloc_datas[65556] = {0};
    u16 po2[8] = {1, 2, 4, 8, 16, 32, 64, 128};

    // Pick random sizes and alignments.
    for (u32 i = 0; i < alloc_data_count; ++i) {
        alloc_datas[i].alignment = po2[krandom_in_range(0, 7)];
        alloc_datas[i].size = (u64)krandom_in_range(1, 65536);
    }

    // Total size needed, including headers.
    u64 total_allocator_size = 0;
    for (u32 i = 0; i < alloc_data_count; ++i) {
        total_allocator_size += alloc_datas[i].alignment + header_size + alloc_datas[i].size;
    }

    // Get the memory requirement
    b8 result = dynamic_allocator_create(total_allocator_size, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    result = dynamic_allocator_create(total_allocator_size, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(total_allocator_size, free_space);

    u32 op_count = 0;
    const u32 max_op_count = 10000000;
    u32 alloc_count = 0;
    while (op_count < max_op_count) {
        // If there are no allocations, or we "roll" high, allocate. Otherwise deallocate.
        if (alloc_count == 0 || krandom_in_range(0, 99) > 50) {
            while (true) {
                u32 index = (u32)krandom_in_range(0, alloc_data_count - 1);
                if (alloc_datas[index].block == 0) {
                    if (!util_allocate(&alloc, &alloc_datas[index], &currently_allocated, header_size, total_allocator_size)) {
                        KERROR("util_allocate failed on index: %u.", index);
                        return false;
                    }
                    alloc_count++;
                    break;
                }
            }
            op_count++;
        } else {
            while (true) {
                u32 index = (u32)krandom_in_range(0, alloc_data_count - 1);
                if (alloc_datas[index].block != 0) {
                    if (!util_free(&alloc, &alloc_datas[index], &currently_allocated, header_size, total_allocator_size)) {
                        KERROR("util_free failed on index: %u.", index);
                        return false;
                    }
                    alloc_count--;
                    break;
                }
            }
            op_count++;
        }
    }

    KTRACE("Max op count of %u reached. Freeing remaining allocations.", max_op_count);
    for (u32 i = 0; i < alloc_data_count; ++i) {
        if (alloc_datas[i].block != 0) {
            if (!util_free(&alloc, &alloc_datas[i], &currently_allocated, header_size, total_allocator_size)) {
                KERROR("util_free failed on index: %u.");
                return false;
            }
        }
    }

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

void dynamic_allocator_register_tests(void) {
    test_manager_register_test(dynamic_allocator_should_create_and_destroy, "Dynamic allocator should create and destroy");
    test_manager_register_test(dynamic_allocator_single_allocation_all_space, "Dynamic allocator single alloc for all space");
    test_manager_register_test(dynamic_allocator_multi_allocation_all_space, "Dynamic allocator multi alloc for all space");
    test_manager_register_test(dynamic_allocator_multi_allocation_over_allocate, "Dynamic allocator try over allocate");
    test_manager_register_test(dynamic_allocator_multi_allocation_most_space_request_too_big, "Dynamic allocator should try to over allocate with not enough space, but not 0 space remaining.");
    test_manager_register_test(dynamic_allocator_single_alloc_aligned, "Dynamic allocator single aligned allocation");
    test_manager_register_test(dynamic_allocator_multiple_alloc_aligned_different_alignments, "Dynamic allocator multiple aligned allocations with different alignments");
    test_manager_register_test(dynamic_allocator_multiple_alloc_aligned_different_alignments_random, "Dynamic allocator multiple aligned allocations with different alignments in random order.");
    test_manager_register_test(dynamic_allocator_multiple_alloc_and_free_aligned_different_alignments_random, "Dynamic allocator randomization test.");
}
