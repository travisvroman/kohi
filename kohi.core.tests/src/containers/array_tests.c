
#include "../expect.h"
#include "../test_manager.h"

#include <containers/array.h>
#include <defines.h>
#include <memory/kmemory.h>

static u8 all_array_tests_after_create(void) {
    // Test a basic type first.

    u8_array arr = u8_array_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.length);
    expect_should_be(sizeof(u8), arr.stride);

    // Set some values
    arr.data[0] = 69;
    arr.data[2] = 42;
    arr.data[4] = 36;

    expect_should_be(69, arr.data[0]);
    expect_should_be(0, arr.data[1]);
    expect_should_be(42, arr.data[2]);
    expect_should_be(0, arr.data[3]);
    expect_should_be(36, arr.data[4]);
    expect_should_be(0, arr.data[5]);

    // Verify that it has been destroyed.
    u8_array_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.length);
    expect_should_be(0, arr.stride);

    return true;
}

static u8 array_all_iterator_tests(void) {

    u8_array_it it;
    u32 loop_count = 0;
    u8_array arr = u8_array_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.length);
    expect_should_be(sizeof(u8), arr.stride);

    // Set some values
    arr.data[0] = 69;
    arr.data[2] = 42;
    arr.data[4] = 36;

    expect_should_be(69, arr.data[0]);
    expect_should_be(0, arr.data[1]);
    expect_should_be(42, arr.data[2]);
    expect_should_be(0, arr.data[3]);
    expect_should_be(36, arr.data[4]);
    expect_should_be(0, arr.data[5]);

    {
        // Try forwards iteration.
        it = u8_array_iterator_begin(&arr);
        expect_should_be(&arr, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !u8_array_iterator_end(&it); u8_array_iterator_next(&it)) {
            u8* val = u8_array_iterator_value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 2) {
                expect_should_be(42, *val);
            } else if (it.pos == 4) {
                expect_should_be(36, *val);
            } else {
                expect_should_be(0, *val);
            }

            loop_count++;
        }
        expect_should_be(6, loop_count);

        // Try reverse/backward iteration.
        it = u8_array_iterator_begin_reverse(&arr);
        expect_should_be(&arr, it.arr);
        expect_should_be(arr.length - 1, it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !u8_array_iterator_end(&it); u8_array_iterator_next(&it)) {
            u8* val = u8_array_iterator_value(&it);
            if (it.pos == 0) {
                expect_should_be(69, *val);
            } else if (it.pos == 2) {
                expect_should_be(42, *val);
            } else if (it.pos == 4) {
                expect_should_be(36, *val);
            } else {
                expect_should_be(0, *val);
            }

            loop_count++;
        }
        expect_should_be(6, loop_count);
    }

    u8_array_destroy(&arr);

    return true;
}

static u8 array_string_type_test(void) {

    string_array arr = string_array_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.length);
    expect_should_be(sizeof(const char*), arr.stride);

    // Set some data.
    arr.data[0] = "test";
    arr.data[2] = "something else";
    arr.data[4] = "ththth";

    // validate content
    expect_string_to_be("test", arr.data[0]);
    expect_string_to_be(0, arr.data[1]);
    expect_string_to_be("something else", arr.data[2]);
    expect_string_to_be(0, arr.data[3]);
    expect_string_to_be("ththth", arr.data[4]);
    expect_string_to_be(0, arr.data[5]);

    string_array_destroy(&arr);

    return true;
}

static u8 array_float_type_test(void) {

    f32_array arr = f32_array_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.length);
    expect_should_be(sizeof(f32), arr.stride);

    // Set some data.
    arr.data[0] = 0.1f;
    arr.data[2] = 0.2f;
    arr.data[4] = 0.3f;

    // validate content
    expect_float_to_be(0.1f, arr.data[0]);
    expect_float_to_be(0.0f, arr.data[1]);
    expect_float_to_be(0.2f, arr.data[2]);
    expect_float_to_be(0.0f, arr.data[3]);
    expect_float_to_be(0.3f, arr.data[4]);
    expect_float_to_be(0.0f, arr.data[5]);

    f32_array_destroy(&arr);

    return true;
}

void array_register_tests(void) {
    test_manager_register_test(all_array_tests_after_create, "All array tests after create");
    test_manager_register_test(array_all_iterator_tests, "All array iterator tests");
    test_manager_register_test(array_string_type_test, "array string type tests");
    test_manager_register_test(array_float_type_test, "array float type tests");
}
