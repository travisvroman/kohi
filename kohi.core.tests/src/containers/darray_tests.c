
#include "../expect.h"
#include "../test_manager.h"

#include <containers/darray.h>
#include <defines.h>
#include <memory/allocators/linear_allocator.h>
#include <memory/kmemory.h>

static linear_allocator alloc;
static frame_allocator_int frame_allocator;

static void* fn_alloc(u64 size) {
    return linear_allocator_allocate(&alloc, size);
}

static void fn_free(void* block, u64 size) {
    // NOTE: intentional No-op here.
}

static void fn_free_all(void) {
    linear_allocator_free_all(&alloc, true);
}

static void setup_frame_allocator(void) {
    // Setup a linear allocator. The allocator can own its memory.
    linear_allocator_create(1024 * 1024 * 20, 0, &alloc);

    // Set it up as a frame allocator.
    frame_allocator.allocate = fn_alloc;
    frame_allocator.free = fn_free;
    frame_allocator.free_all = fn_free_all;
}

static void destroy_frame_allocator(void) {
    linear_allocator_destroy(&alloc);
    frame_allocator.allocate = 0;
    frame_allocator.free = 0;
    frame_allocator.free_all = 0;
}

static u8 all_darray_tests_after_create(void) {
    // Test a basic type first.

    darray_u8 arr = darray_u8_create();
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Push and validate content [69], length = 1, capacity = 1
    darray_u8* returned = darray_u8_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    // Push and validate content [69, 42], length = 2, capacity = 2
    darray_u8_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(2, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    // Push and validate content [69, 42, 36], length = 3, capacity = 4
    darray_u8_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Push and validate content [69, 42, 36, 19], length = 4, capacity = 4
    darray_u8_push(&arr, 19);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(19, arr.data[3]);

    // Pop '42' and validate content [69, 36, 19], length = 3, capacity = 4
    // popped = 42
    u8 popped_value = 0;
    b8 result = darray_u8_pop_at(&arr, 1, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, arr.data[2]);
    expect_should_be(42, popped_value);

    // Pop last value and validate content [69, 36], length = 2, capacity = 4
    // popped = 19
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, popped_value);

    // Insert at index 1 and validate content [69, 88, 36], length = 3, capacity = 4
    result = darray_u8_insert_at(&arr, 1, 88);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(88, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Clear and validate content [], length = 0, capacity = 4
    returned = darray_u8_clear(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);

    // Pop from empty. This should fail.
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Pop at 0 from empty. This should fail.
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Insert at 0 - empty array. Should succeed. Content should be [69], length = 1.
    result = darray_u8_insert_at(&arr, 0, 69);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);

    // Insert at 0 - non-empty array. Should succeed. Content should be [42, 69], length = 2.
    result = darray_u8_insert_at(&arr, 0, 42);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);

    // Insert at last index (2) - non-empty array. Should succeed. Content should be [42, 69, 11], length = 3.
    result = darray_u8_insert_at(&arr, 2, 11);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);

    // Do it 2 more times, the second should force a realloc. Should succeed. Content should be [42, 69, 11, 13, 17], length = 3.
    result = darray_u8_insert_at(&arr, 3, 13);
    result = darray_u8_insert_at(&arr, 4, 17);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);
    expect_should_be(13, arr.data[3]);
    expect_should_be(17, arr.data[4]);

    // Pop at 0 from non-empty. This should succeed. Content should be [69, 11, 13, 17]. Length = 4
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(true, result);
    expect_should_be(42, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);

    // Pop at last index (3) from non-empty. This should succeed. Content should be [69, 11, 13]. Length = 3
    result = darray_u8_pop_at(&arr, 3, &popped_value);
    expect_should_be(true, result);
    expect_should_be(17, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);

    // Insert at outside of range. Should fail.
    result = darray_u8_insert_at(&arr, 4, 99);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);

    // Verify that it has been destroyed.
    darray_u8_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.base.length);
    expect_should_be(0, arr.base.capacity);
    expect_should_be(0, arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    return true;
}

static u8 all_darray_tests_after_reserve_3(void) {
    // Test a basic type first.

    darray_u8 arr = darray_u8_reserve(3);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Push and validate content [69], length = 1, capacity = 3
    darray_u8* returned = darray_u8_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    // Push and validate content [69, 42], length = 2, capacity = 3
    darray_u8_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    // Push and validate content [69, 42, 36], length = 3, capacity = 3
    darray_u8_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Push and validate content [69, 42, 36, 19], length = 4, capacity = 6
    darray_u8_push(&arr, 19);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(19, arr.data[3]);

    // Pop '42' and validate content [69, 36, 19], length = 3, capacity = 6
    // popped = 42
    u8 popped_value = 0;
    b8 result = darray_u8_pop_at(&arr, 1, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, arr.data[2]);
    expect_should_be(42, popped_value);

    // Pop last value and validate content [69, 36], length = 2, capacity = 6
    // popped = 19
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, popped_value);

    // Insert at index 1 and validate content [69, 88, 36], length = 3, capacity = 6
    result = darray_u8_insert_at(&arr, 1, 88);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(88, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Clear and validate content [], length = 0, capacity = 6
    returned = darray_u8_clear(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);

    // Pop from empty. This should fail.
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Pop at 0 from empty. This should fail.
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Insert at 0 - empty array. Should succeed. Content should be [69], length = 1.
    result = darray_u8_insert_at(&arr, 0, 69);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);

    // Insert at 0 - non-empty array. Should succeed. Content should be [42, 69], length = 2.
    result = darray_u8_insert_at(&arr, 0, 42);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);

    // Insert at last index (2) - non-empty array. Should succeed. Content should be [42, 69, 11], length = 3.
    result = darray_u8_insert_at(&arr, 2, 11);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);

    // Do it 4 more times, the second should force a realloc. Should succeed. Content should be [42, 69, 11, 13, 17, 21, 23], length = 7.
    result = darray_u8_insert_at(&arr, 3, 13);
    result = darray_u8_insert_at(&arr, 4, 17);
    result = darray_u8_insert_at(&arr, 5, 21);
    result = darray_u8_insert_at(&arr, 6, 23);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(7, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);
    expect_should_be(13, arr.data[3]);
    expect_should_be(17, arr.data[4]);
    expect_should_be(21, arr.data[5]);
    expect_should_be(23, arr.data[6]);

    // Pop at 0 from non-empty. This should succeed. Content should be [69, 11, 13, 17, 21, 23]. Length = 6
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(true, result);
    expect_should_be(42, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);
    expect_should_be(23, arr.data[5]);

    // Pop at last index (5) from non-empty. This should succeed. Content should be [69, 11, 13, 17, 21]. Length = 5
    result = darray_u8_pop_at(&arr, 5, &popped_value);
    expect_should_be(true, result);
    expect_should_be(23, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);

    // Insert at outside of range. Should fail.
    result = darray_u8_insert_at(&arr, 7, 99);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);

    // Verify that it has been destroyed.
    darray_u8_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.base.length);
    expect_should_be(0, arr.base.capacity);
    expect_should_be(0, arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    return true;
}

static u8 all_darray_tests_after_create_custom_allocator(void) {
    setup_frame_allocator();

    darray_u8 arr = darray_u8_create_with_allocator(&frame_allocator);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Push and validate content [69], length = 1, capacity = 1
    darray_u8* returned = darray_u8_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    // Push and validate content [69, 42], length = 2, capacity = 2
    darray_u8_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(2, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    // Push and validate content [69, 42, 36], length = 3, capacity = 4
    darray_u8_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Push and validate content [69, 42, 36, 19], length = 4, capacity = 4
    darray_u8_push(&arr, 19);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(19, arr.data[3]);

    // Pop '42' and validate content [69, 36, 19], length = 3, capacity = 4
    // popped = 42
    u8 popped_value = 0;
    b8 result = darray_u8_pop_at(&arr, 1, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, arr.data[2]);
    expect_should_be(42, popped_value);

    // Pop last value and validate content [69, 36], length = 2, capacity = 4
    // popped = 19
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, popped_value);

    // Insert at index 1 and validate content [69, 88, 36], length = 3, capacity = 4
    result = darray_u8_insert_at(&arr, 1, 88);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(88, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Clear and validate content [], length = 0, capacity = 4
    returned = darray_u8_clear(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(&arr, returned);

    // Pop from empty. This should fail.
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Pop at 0 from empty. This should fail.
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Insert at 0 - empty array. Should succeed. Content should be [69], length = 1.
    result = darray_u8_insert_at(&arr, 0, 69);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);

    // Insert at 0 - non-empty array. Should succeed. Content should be [42, 69], length = 2.
    result = darray_u8_insert_at(&arr, 0, 42);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);

    // Insert at last index (2) - non-empty array. Should succeed. Content should be [42, 69, 11], length = 3.
    result = darray_u8_insert_at(&arr, 2, 11);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);

    // Do it 2 more times, the second should force a realloc. Should succeed. Content should be [42, 69, 11, 13, 17], length = 3.
    result = darray_u8_insert_at(&arr, 3, 13);
    result = darray_u8_insert_at(&arr, 4, 17);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);
    expect_should_be(13, arr.data[3]);
    expect_should_be(17, arr.data[4]);

    // Pop at 0 from non-empty. This should succeed. Content should be [69, 11, 13, 17]. Length = 4
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(true, result);
    expect_should_be(42, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);

    // Pop at last index (3) from non-empty. This should succeed. Content should be [69, 11, 13]. Length = 3
    result = darray_u8_pop_at(&arr, 3, &popped_value);
    expect_should_be(true, result);
    expect_should_be(17, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);

    // Insert at outside of range. Should fail.
    result = darray_u8_insert_at(&arr, 4, 99);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(8, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);

    // Verify that it has been destroyed.
    darray_u8_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.base.length);
    expect_should_be(0, arr.base.capacity);
    expect_should_be(0, arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    destroy_frame_allocator();

    return true;
}

static u8 all_darray_tests_after_reserve_3_with_allocator(void) {
    setup_frame_allocator();

    darray_u8 arr = darray_u8_reserve_with_allocator(3, &frame_allocator);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Push and validate content [69], length = 1, capacity = 3
    darray_u8* returned = darray_u8_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    // Push and validate content [69, 42], length = 2, capacity = 3
    darray_u8_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    // Push and validate content [69, 42, 36], length = 3, capacity = 3
    darray_u8_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(3, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Push and validate content [69, 42, 36, 19], length = 4, capacity = 6
    darray_u8_push(&arr, 19);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(19, arr.data[3]);

    // Pop '42' and validate content [69, 36, 19], length = 3, capacity = 6
    // popped = 42
    u8 popped_value = 0;
    b8 result = darray_u8_pop_at(&arr, 1, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, arr.data[2]);
    expect_should_be(42, popped_value);

    // Pop last value and validate content [69, 36], length = 2, capacity = 6
    // popped = 19
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, popped_value);

    // Insert at index 1 and validate content [69, 88, 36], length = 3, capacity = 6
    result = darray_u8_insert_at(&arr, 1, 88);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(88, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Clear and validate content [], length = 0, capacity = 6
    returned = darray_u8_clear(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(&arr, returned);

    // Pop from empty. This should fail.
    result = darray_u8_pop(&arr, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Pop at 0 from empty. This should fail.
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);

    // Insert at 0 - empty array. Should succeed. Content should be [69], length = 1.
    result = darray_u8_insert_at(&arr, 0, 69);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);

    // Insert at 0 - non-empty array. Should succeed. Content should be [42, 69], length = 2.
    result = darray_u8_insert_at(&arr, 0, 42);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);

    // Insert at last index (2) - non-empty array. Should succeed. Content should be [42, 69, 11], length = 3.
    result = darray_u8_insert_at(&arr, 2, 11);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(6, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);

    // Do it 4 more times, the second should force a realloc. Should succeed. Content should be [42, 69, 11, 13, 17, 21, 23], length = 7.
    result = darray_u8_insert_at(&arr, 3, 13);
    result = darray_u8_insert_at(&arr, 4, 17);
    result = darray_u8_insert_at(&arr, 5, 21);
    result = darray_u8_insert_at(&arr, 6, 23);
    expect_should_be(true, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(7, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(42, arr.data[0]);
    expect_should_be(69, arr.data[1]);
    expect_should_be(11, arr.data[2]);
    expect_should_be(13, arr.data[3]);
    expect_should_be(17, arr.data[4]);
    expect_should_be(21, arr.data[5]);
    expect_should_be(23, arr.data[6]);

    // Pop at 0 from non-empty. This should succeed. Content should be [69, 11, 13, 17, 21, 23]. Length = 6
    result = darray_u8_pop_at(&arr, 0, &popped_value);
    expect_should_be(true, result);
    expect_should_be(42, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);
    expect_should_be(23, arr.data[5]);

    // Pop at last index (5) from non-empty. This should succeed. Content should be [69, 11, 13, 17, 21]. Length = 5
    result = darray_u8_pop_at(&arr, 5, &popped_value);
    expect_should_be(true, result);
    expect_should_be(23, popped_value);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);

    // Insert at outside of range. Should fail.
    result = darray_u8_insert_at(&arr, 7, 99);
    expect_should_be(false, result);
    expect_should_not_be(arr.data, 0);
    expect_should_be(5, arr.base.length);
    expect_should_be(12, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(&frame_allocator, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(11, arr.data[1]);
    expect_should_be(13, arr.data[2]);
    expect_should_be(17, arr.data[3]);
    expect_should_be(21, arr.data[4]);

    /**
     * TODO: Also iterator tests
     *  - forward iteration
     *  - backward iteration
     */

    // Verify that it has been destroyed.
    darray_u8_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.base.length);
    expect_should_be(0, arr.base.capacity);
    expect_should_be(0, arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    destroy_frame_allocator();

    return true;
}

static u8 darray_all_iterator_tests(void) {

    darray_iterator it;
    u32 loop_count = 0;
    darray_u8 arr = darray_u8_create();
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    {
        // Try forwards iteration on an empty array.
        it = arr.begin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            loop_count++;
        }
        expect_should_be(0, loop_count);

        // Try reverse/backward iteration on an empty array.
        it = arr.rbegin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(arr.base.length - 1, it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            loop_count++;
        }
        expect_should_be(0, loop_count);
    }

    // Push and validate content [69], length = 1, capacity = 1
    darray_u8* returned = darray_u8_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    {
        // Try forwards iteration.
        it = arr.begin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            }
            loop_count++;
        }
        expect_should_be(1, loop_count);

        // Try reverse/backward iteration.
        it = arr.rbegin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(arr.base.length - 1, it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            }
            loop_count++;
        }
        expect_should_be(1, loop_count);
    }

    // Push and validate content [69, 42], length = 2, capacity = 2
    darray_u8_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(2, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    {
        // Try forwards iteration.
        it = arr.begin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = (u8*)it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 1) {
                expect_should_be(42, *val);
            }
            loop_count++;
        }
        expect_should_be(2, loop_count);

        // Try reverse/backward iteration.
        it = arr.rbegin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(arr.base.length - 1, it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 1) {
                expect_should_be(42, *val);
            }
            loop_count++;
        }
        expect_should_be(2, loop_count);
    }

    // Push and validate content [69, 42, 36], length = 3, capacity = 4
    darray_u8_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(u8), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    {
        // Try forwards iteration.
        it = arr.begin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 1) {
                expect_should_be(42, *val);
            } else if (it.pos == 2) {
                expect_should_be(36, *val);
            }

            loop_count++;
        }
        expect_should_be(3, loop_count);

        // Try reverse/backward iteration.
        it = arr.rbegin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(arr.base.length - 1, it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 1) {
                expect_should_be(42, *val);
            } else if (it.pos == 2) {
                expect_should_be(36, *val);
            }

            loop_count++;
        }
        expect_should_be(3, loop_count);
    }

    darray_u8_destroy(&arr);

    return true;
}

static u8 darray_string_type_test(void) {

    darray_string arr = darray_string_create();
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(const char*), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Push and validate content ["test"], length = 1, capacity = 1
    darray_string* returned = darray_string_push(&arr, "test");
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(const char*), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_string_to_be("test", arr.data[0]);

    // Push and validate content ["test", "something else"], length = 2, capacity = 2
    darray_string_push(&arr, "something else");
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(2, arr.base.capacity);
    expect_should_be(sizeof(const char*), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_string_to_be("test", arr.data[0]);
    expect_string_to_be("something else", arr.data[1]);

    // Push and validate content ["test", "something else", "ththth"], length = 3, capacity = 4
    darray_string_push(&arr, "ththth");
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(const char*), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_string_to_be("test", arr.data[0]);
    expect_string_to_be("something else", arr.data[1]);
    expect_string_to_be("ththth", arr.data[2]);

    darray_string_destroy(&arr);

    return true;
}

static u8 darray_float_type_test(void) {

    darray_f32 arr = darray_f32_create();
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(f32), arr.base.stride);
    expect_should_be(0, arr.base.allocator);

    // Push and validate content [0.1f], length = 1, capacity = 1
    darray_f32* returned = darray_f32_push(&arr, 0.1f);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.base.length);
    expect_should_be(1, arr.base.capacity);
    expect_should_be(sizeof(f32), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_should_be(&arr, returned);
    expect_float_to_be(0.1f, arr.data[0]);

    // Push and validate content [0.1f, 0.2f], length = 2, capacity = 2
    darray_f32_push(&arr, 0.2f);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.base.length);
    expect_should_be(2, arr.base.capacity);
    expect_should_be(sizeof(f32), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_float_to_be(0.1f, arr.data[0]);
    expect_float_to_be(0.2f, arr.data[1]);

    // Push and validate content [0.1f, 0.2f, 0.3f], length = 3, capacity = 4
    darray_f32_push(&arr, 0.3f);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.base.length);
    expect_should_be(4, arr.base.capacity);
    expect_should_be(sizeof(f32), arr.base.stride);
    expect_should_be(0, arr.base.allocator);
    expect_float_to_be(0.1f, arr.data[0]);
    expect_float_to_be(0.2f, arr.data[1]);
    expect_float_to_be(0.3f, arr.data[2]);

    darray_f32_destroy(&arr);

    return true;
}

void darray_register_tests(void) {
    test_manager_register_test(all_darray_tests_after_create, "All darray tests after create");
    test_manager_register_test(all_darray_tests_after_reserve_3, "All darray tests after reserve(3)");
    test_manager_register_test(all_darray_tests_after_create_custom_allocator, "All darray tests after create with frame allocator");
    test_manager_register_test(all_darray_tests_after_reserve_3_with_allocator, "All darray tests after reserve(3) with frame allocator");
    test_manager_register_test(darray_all_iterator_tests, "All darray iterator tests");
    test_manager_register_test(darray_string_type_test, "darray string type tests");
    test_manager_register_test(darray_float_type_test, "darray float type tests");
}
