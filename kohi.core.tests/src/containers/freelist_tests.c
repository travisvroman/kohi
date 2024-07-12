#include "../expect.h"
#include "../test_manager.h"

#include <containers/freelist.h>
#include <defines.h>
#include <memory/kmemory.h>

u8 freelist_should_create_and_destroy(void) {
    // NOTE: creating a small size list, which will trigger a warning.
    KDEBUG("The following warning message is intentional.");

    freelist list;

    // Get the memory requirement
    u64 memory_requirement = 0;
    u64 total_size = 40;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Verify that the memory was assigned.
    expect_should_not_be(0, list.memory);
    // Verify that the entire block is free.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(total_size, free_space);

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);

    return true;
}

u8 freelist_should_allocate_one_and_free_one(void) {
    freelist list;

    // Get the memory requirement
    u64 memory_requirement = 0;
    u64 total_size = 512;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Allocate some space.
    u64 offset = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    b8 result = freelist_allocate_block(&list, 64, &offset);
    // Verify that result is true, offset should be set to 0.
    expect_to_be_true(result);
    expect_should_be(0, offset);

    // Verify that the correct amount of space is free.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(total_size - 64, free_space);

    // Now free the block.
    result = freelist_free_block(&list, 64, offset);
    // Verify that result is true
    expect_to_be_true(result);

    // Verify the entire block is free.
    free_space = freelist_free_space(&list);
    expect_should_be(total_size, free_space);

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);

    return true;
}

u8 freelist_should_allocate_one_and_free_multi(void) {
    freelist list;

    // Get the memory requirement
    u64 memory_requirement = 0;
    u64 total_size = 512;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Allocate some space.
    u64 offset = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    b8 result = freelist_allocate_block(&list, 64, &offset);
    // Verify that result is true, offset should be set to 0.
    expect_to_be_true(result);
    expect_should_be(0, offset);

    // Allocate some more space.
    u64 offset2 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 64, &offset2);
    // Verify that result is true, offset should be set to the size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(64, offset2);

    // Allocate one more space.
    u64 offset3 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 64, &offset3);
    // Verify that result is true, offset should be set to the offset+size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(128, offset3);

    // Verify that the correct amount of space is free.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(total_size - 192, free_space);

    // Now free the middle block.
    result = freelist_free_block(&list, 64, offset2);
    // Verify that result is true
    expect_to_be_true(result);

    // Verify the correct amount is free.
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 128, free_space);

    // Allocate some more space, this should fill the middle block back in.
    u64 offset4 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 64, &offset4);
    // Verify that result is true, offset should be set to the size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(offset2, offset4); // Offset should be the same as 2 since it occupies the same space

    // Verify that the correct amount of space is free.
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 192, free_space);

    // Free the first block and verify space.
    result = freelist_free_block(&list, 64, offset);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 128, free_space);

    // Free the last block and verify space.
    result = freelist_free_block(&list, 64, offset3);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 64, free_space);

    // Free the middle block and verify space.
    result = freelist_free_block(&list, 64, offset4);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size, free_space);

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);

    return true;
}

u8 freelist_should_allocate_one_and_free_multi_varying_sizes(void) {
    freelist list;

    // Get the memory requirement
    u64 memory_requirement = 0;
    u64 total_size = 512;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Allocate some space.
    u64 offset = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    b8 result = freelist_allocate_block(&list, 64, &offset);
    // Verify that result is true, offset should be set to 0.
    expect_to_be_true(result);
    expect_should_be(0, offset);

    // Allocate some more space.
    u64 offset2 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 32, &offset2);
    // Verify that result is true, offset should be set to the size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(64, offset2);

    // Allocate one more space.
    u64 offset3 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 64, &offset3);
    // Verify that result is true, offset should be set to the offset+size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(96, offset3);

    // Verify that the correct amount of space is free.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(total_size - 160, free_space);

    // Now free the middle block.
    result = freelist_free_block(&list, 32, offset2);
    // Verify that result is true
    expect_to_be_true(result);

    // Verify the correct amount is free.
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 128, free_space);

    // Allocate some more space, this time larger than the old middle block. This should have a new offset
    // at the end of the list.
    u64 offset4 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    result = freelist_allocate_block(&list, 64, &offset4);
    // Verify that result is true, offset should be set to the size of the previous allocation.
    expect_to_be_true(result);
    expect_should_be(160, offset4); // Offset should be the end since it occupies more space than the old middle block.

    // Verify that the correct amount of space is free.
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 192, free_space);

    // Free the first block and verify space.
    result = freelist_free_block(&list, 64, offset);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 128, free_space);

    // Free the last block and verify space.
    result = freelist_free_block(&list, 64, offset3);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size - 64, free_space);

    // Free the middle (now end) block and verify space.
    result = freelist_free_block(&list, 64, offset4);
    expect_to_be_true(result);
    free_space = freelist_free_space(&list);
    expect_should_be(total_size, free_space);

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);

    return true;
}

u8 freelist_should_allocate_to_full_and_fail_to_allocate_more(void) {
    freelist list;

    // Get the memory requirement
    u64 memory_requirement = 0;
    u64 total_size = 512;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Allocate all space.
    u64 offset = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    b8 result = freelist_allocate_block(&list, 512, &offset);
    // Verify that result is true, offset should be set to 0.
    expect_to_be_true(result);
    expect_should_be(0, offset);

    // Verify that the correct amount of space is free.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(0, free_space);

    // Now try allocating some more
    u64 offset2 = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    KDEBUG("The following warning message is intentional.");
    result = freelist_allocate_block(&list, 64, &offset2);
    // Verify that result is false
    expect_to_be_false(result);

    // Verify that the correct amount of space is free.
    free_space = freelist_free_space(&list);
    expect_should_be(0, free_space);

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);

    return true;
}

typedef struct alloc_data {
    u64 size;
    u64 offset;
} alloc_data;

u8 util_freelist_allocate(freelist* list, alloc_data* data, u64* currently_allocated, u64 total_list_size) {
    // Allocate some space.
    data->offset = INVALID_ID; // Start with invalid id, which is a good default since it should never happen.
    b8 result = freelist_allocate_block(list, data->size, &data->offset);
    // Verify that result is true
    expect_to_be_true(result);
    expect_should_not_be(data->offset, INVALID_ID);

    // Track it.
    *currently_allocated += data->size;

    // Verify free space.
    u64 free_space = freelist_free_space(list);
    expect_should_be(free_space, total_list_size - *currently_allocated);

    return true;
}

u8 util_freelist_free(freelist* list, alloc_data* data, u64* currently_allocated, u64 total_list_size) {
    // Free the block and verify space.
    b8 result = freelist_free_block(list, data->size, data->offset);
    expect_to_be_true(result);

    // Track it.
    *currently_allocated -= data->size;

    // Verify free space.
    u64 free_space = freelist_free_space(list);
    expect_should_be(free_space, total_list_size - *currently_allocated);

    // Reset the data.
    data->offset = INVALID_ID;

    return true;
}

u8 freelist_multiple_alloc_and_free_random(void) {
    freelist list;

    // Pick random sizes.
    const u32 alloc_data_count = 65556;
    alloc_data alloc_datas[65556] = {0};
    for (u32 i = 0; i < alloc_data_count; ++i) {
        alloc_datas[i].size = (u64)krandom_in_range(1, 65536);
        // INVALID_ID means the data isn't allocated.
        alloc_datas[i].offset = INVALID_ID;
    }

    // Total size needed for the list.
    u64 total_size = 0;
    for (u32 i = 0; i < alloc_data_count; ++i) {
        total_size += alloc_datas[i].size;
    }

    // Get the memory requirement
    u64 memory_requirement = 0;
    freelist_create(total_size, &memory_requirement, 0, 0);

    // Allocate and create the freelist.
    void* block = kallocate(memory_requirement, MEMORY_TAG_ENGINE);
    freelist_create(total_size, &memory_requirement, block, &list);

    // Verify free space.
    u64 free_space = freelist_free_space(&list);
    expect_should_be(total_size, free_space);

    // Perform a bunch of random ops against the list, verifying each one along the way.
    u64 currently_allocated = 0;
    u32 op_count = 0;
    const u32 max_op_count = 100000;
    u32 alloc_count = 0;
    while (op_count < max_op_count) {
        // If there are no allocations, or we "roll" high, allocate. Otherwise deallocate.
        if (alloc_count == 0 || krandom_in_range(0, 99) > 50) {
            while (true) {
                u32 index = (u32)krandom_in_range(0, alloc_data_count - 1);
                // Look for an un-allocated block.
                if (alloc_datas[index].offset == INVALID_ID) {
                    if (!util_freelist_allocate(&list, &alloc_datas[index], &currently_allocated, total_size)) {
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
                // Look for an allocated block.
                if (alloc_datas[index].offset != INVALID_ID) {
                    if (!util_freelist_free(&list, &alloc_datas[index], &currently_allocated, total_size)) {
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
        if (alloc_datas[i].offset != INVALID_ID) {
            if (!util_freelist_free(&list, &alloc_datas[i], &currently_allocated, total_size)) {
                KERROR("util_free failed on index: %u.");
                return false;
            }
        }
    }

    // Destroy and verify that the memory was unassigned.
    freelist_destroy(&list);
    expect_should_be(0, list.memory);
    kfree(block, memory_requirement, MEMORY_TAG_ENGINE);
    return true;
}

void freelist_register_tests(void) {
    test_manager_register_test(freelist_should_create_and_destroy, "Freelist should create and destroy");
    test_manager_register_test(freelist_should_allocate_one_and_free_one, "Freelist allocate and free one entry.");
    test_manager_register_test(freelist_should_allocate_one_and_free_multi, "Freelist allocate and free multiple entries.");
    test_manager_register_test(freelist_should_allocate_one_and_free_multi_varying_sizes, "Freelist allocate and free multiple entries of varying sizes.");
    test_manager_register_test(freelist_should_allocate_to_full_and_fail_to_allocate_more, "Freelist allocate to full and fail when trying to allocate more.");
    test_manager_register_test(freelist_multiple_alloc_and_free_random, "Freelist should randomly allocate and free.");
}
