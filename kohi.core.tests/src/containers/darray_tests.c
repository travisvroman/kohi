
#include "../expect.h"
#include "../test_manager.h"
#include "logger.h"

#include <containers/darray.h>
#include <defines.h>
#include <memory/kmemory.h>

u8 all_darray_tests_in_one(void) {
    // Test a basic type first.

    u8_darray arr = u8_darray_create();
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.length);
    expect_should_be(1, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);

    // Push and validate content [69], length = 1, capacity = 1
    u8_darray* returned = u8_darray_push(&arr, 69);
    expect_should_not_be(arr.data, 0);
    expect_should_be(1, arr.length);
    expect_should_be(1, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(&arr, returned);
    expect_should_be(69, arr.data[0]);

    // Push and validate content [69, 42], length = 2, capacity = 2
    u8_darray_push(&arr, 42);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.length);
    expect_should_be(2, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);

    // Push and validate content [69, 42, 36], length = 3, capacity = 4
    u8_darray_push(&arr, 36);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);

    // Push and validate content [69, 42, 36, 19], length = 4, capacity = 4
    u8_darray_push(&arr, 19);
    expect_should_not_be(arr.data, 0);
    expect_should_be(4, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(42, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(19, arr.data[3]);

    // Pop '42' and validate content [69, 36, 19], length = 3, capacity = 4
    // popped = 42
    u8 popped_value = u8_darray_pop_at(&arr, 1);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, arr.data[2]);
    expect_should_be(42, popped_value);

    // Pop last value and validate content [69, 36], length = 2, capacity = 4
    // popped = 19
    popped_value = u8_darray_pop(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(2, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(36, arr.data[1]);
    expect_should_be(19, popped_value);

    // Insert at index 1 and validate content [69, 88, 36], length = 3, capacity = 4
    returned = u8_darray_insert_at(&arr, 1, 88);
    expect_should_not_be(arr.data, 0);
    expect_should_be(3, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(69, arr.data[0]);
    expect_should_be(88, arr.data[1]);
    expect_should_be(36, arr.data[2]);
    expect_should_be(&arr, returned);

    // Insert at index 1 and validate content [], length = 0, capacity = 4
    returned = u8_darray_clear(&arr);
    expect_should_not_be(arr.data, 0);
    expect_should_be(0, arr.length);
    expect_should_be(4, arr.capacity);
    expect_should_be(sizeof(u8), arr.stride);
    expect_should_be(0, arr.allocator);
    expect_should_be(&arr, returned);

    /** TODO: Edge cases/negative tests:
     *  - All above tests, but with a reserved array of size 3.
     *  - insert at outside of range
     *  - insert at index 0 - empty array
     *  - insert at index 0 - non-empty array
     *  - insert at last index - non-empty array (empty array case covered above already.)
     *  - pop from empty array
     *  - pop at 0 - empty array
     *  - pop at 0 - non-empty array
     *  - pop at last index - non-empty array
     *  - All of the above, but using a custom allocator.
     *
     *
     * TODO: Also iterator tests
     *  - forward iteration
     *  - backward iteration
     */

    // Verify that it has been destroyed.
    u8_darray_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.length);
    expect_should_be(0, arr.capacity);
    expect_should_be(0, arr.stride);
    expect_should_be(0, arr.allocator);

    KDEBUG("Darray tests passed.");

    return true;
}

void darray_register_tests(void) {
    test_manager_register_test(all_darray_tests_in_one, "All darray tests");
}
