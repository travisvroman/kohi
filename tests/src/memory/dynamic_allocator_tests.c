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

/*
u8 dynamic_allocator_multi_allocation_over_allocate() {
    u64 max_allocs = 3;
    dynamic_allocator alloc;
    dynamic_allocator_create(sizeof(u64) * max_allocs, 0, &alloc);

    // Multiple allocations - full.
    void* block;
    for (u64 i = 0; i < max_allocs; ++i) {
        block = dynamic_allocator_allocate(&alloc, sizeof(u64));
        // Validate it
        expect_should_not_be(0, block);
        expect_should_be(sizeof(u64) * (i + 1), alloc.allocated);
    }

    KDEBUG("Note: The following error is intentionally caused by this test.");

    // Ask for one more allocation. Should error and return 0.
    block = dynamic_allocator_allocate(&alloc, sizeof(u64));
    // Validate it - allocated should be unchanged.
    expect_should_be(0, block);
    expect_should_be(sizeof(u64) * (max_allocs), alloc.allocated);

    dynamic_allocator_destroy(&alloc);

    return true;
}

u8 dynamic_allocator_multi_allocation_all_space_then_free() {
    u64 max_allocs = 1024;
    dynamic_allocator alloc;
    dynamic_allocator_create(sizeof(u64) * max_allocs, 0, &alloc);

    // Multiple allocations - full.
    void* block;
    for (u64 i = 0; i < max_allocs; ++i) {
        block = dynamic_allocator_allocate(&alloc, sizeof(u64));
        // Validate it
        expect_should_not_be(0, block);
        expect_should_be(sizeof(u64) * (i + 1), alloc.allocated);
    }

    // Validate that pointer is reset.
    dynamic_allocator_free_all(&alloc);
    expect_should_be(0, alloc.allocated);

    dynamic_allocator_destroy(&alloc);

    return true;
}*/

void dynamic_allocator_register_tests() {
    test_manager_register_test(dynamic_allocator_should_create_and_destroy, "Dynamic allocator should create and destroy");
    test_manager_register_test(dynamic_allocator_single_allocation_all_space, "Dynamic allocator single alloc for all space");
    test_manager_register_test(dynamic_allocator_multi_allocation_all_space, "Dynamic allocator multi alloc for all space");
    //test_manager_register_test(dynamic_allocator_multi_allocation_over_allocate, "Dynamic allocator try over allocate");
    //test_manager_register_test(dynamic_allocator_multi_allocation_all_space_then_free, "Dynamic allocator allocated should be 0 after free_all");
}