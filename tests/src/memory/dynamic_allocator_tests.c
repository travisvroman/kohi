#include "dynamic_allocator_tests.h"
#include "../test_manager.h"
#include "../expect.h"

#include <defines.h>

#include <core/kmemory.h>
#include <memory/dynamic_allocator.h>

u8 dynamic_allocator_should_create_and_destroy() {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_APPLICATION);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_APPLICATION);
    return true;
}

u8 dynamic_allocator_single_allocation_all_space() {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_APPLICATION);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

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
    expect_should_be(1024, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_APPLICATION);
    return true;
}

u8 dynamic_allocator_multi_allocation_all_space() {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_APPLICATION);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768, free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256, free_space);

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
    expect_should_be(256, free_space);

    // Free the next allocation, out of order
    dynamic_allocator_free(&alloc, block, 256);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(512, free_space);

    // Free the final allocation, out of order
    dynamic_allocator_free(&alloc, block2, 512);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_APPLICATION);
    return true;
}

u8 dynamic_allocator_multi_allocation_over_allocate() {
    dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_APPLICATION);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768, free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256, free_space);

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
    kfree(memory, memory_requirement, MEMORY_TAG_APPLICATION);
    return true;
}


u8 dynamic_allocator_multi_allocation_most_space_request_too_big() {
dynamic_allocator alloc;
    u64 memory_requirement = 0;
    // Get the memory requirement
    b8 result = dynamic_allocator_create(1024, &memory_requirement, 0, 0);
    expect_to_be_true(result);

    // Actually create the allocator.
    void* memory = kallocate(memory_requirement, MEMORY_TAG_APPLICATION);
    result = dynamic_allocator_create(1024, &memory_requirement, memory, &alloc);
    expect_to_be_true(result);
    expect_should_not_be(0, alloc.memory);
    u64 free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(1024, free_space);

    // Allocate part of the block.
    void* block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_not_be(0, block);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(768, free_space);

    // Allocate another part of the block.
    void* block2 = dynamic_allocator_allocate(&alloc, 512);
    expect_should_not_be(0, block2);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(256, free_space);

    // Allocate the last part of the block.
    void* block3 = dynamic_allocator_allocate(&alloc, 128);
    expect_should_not_be(0, block3);

    // Verify free space
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(128, free_space);

    // Attempt one more allocation, deliberately trying to overflow
    KDEBUG("Note: The following warning and errors are intentionally caused by this test.");
    void* fail_block = dynamic_allocator_allocate(&alloc, 256);
    expect_should_be(0, fail_block);

    // Verify free space.
    free_space = dynamic_allocator_free_space(&alloc);
    expect_should_be(128, free_space);

    // Destroy the allocator.
    dynamic_allocator_destroy(&alloc);
    expect_should_be(0, alloc.memory);
    kfree(memory, memory_requirement, MEMORY_TAG_APPLICATION);
    return true;
}

void dynamic_allocator_register_tests() {
    test_manager_register_test(dynamic_allocator_should_create_and_destroy, "Dynamic allocator should create and destroy");
    test_manager_register_test(dynamic_allocator_single_allocation_all_space, "Dynamic allocator single alloc for all space");
    test_manager_register_test(dynamic_allocator_multi_allocation_all_space, "Dynamic allocator multi alloc for all space");
    test_manager_register_test(dynamic_allocator_multi_allocation_over_allocate, "Dynamic allocator try over allocate");
    test_manager_register_test(dynamic_allocator_multi_allocation_most_space_request_too_big, "Dynamic allocator should try to over allocate with not enough space, but not 0 space remaining.");
}