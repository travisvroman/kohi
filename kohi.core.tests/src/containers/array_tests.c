
#include "../expect.h"
#include "../test_manager.h"

#include <containers/array.h>
#include <defines.h>
#include <memory/kmemory.h>

static u8 all_array_tests_after_create(void) {
    // Test a basic type first.

    array_u8 arr = array_u8_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(sizeof(u8), arr.base.stride);

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
    array_u8_destroy(&arr);
    expect_should_be(0, arr.data);
    expect_should_be(0, arr.base.length);
    expect_should_be(0, arr.base.stride);

    return true;
}

static u8 array_all_iterator_tests(void) {

    array_iterator it;
    u32 loop_count = 0;
    array_u8 arr = array_u8_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(sizeof(u8), arr.base.stride);

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
        it = arr.begin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(0, it.pos);
        expect_should_be(1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
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
        it = arr.rbegin(&arr.base);
        expect_should_be(&arr.base, it.arr);
        expect_should_be(arr.base.length - 1, (u32)it.pos);
        expect_should_be(-1, it.dir);
        loop_count = 0;
        for (; !it.end(&it); it.next(&it)) {
            u8* val = it.value(&it);
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

    array_u8_destroy(&arr);

    return true;
}

static u8 array_string_type_test(void) {

    array_string arr = array_string_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(sizeof(const char*), arr.base.stride);

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

    array_string_destroy(&arr);

    return true;
}

static u8 array_float_type_test(void) {

    array_f32 arr = array_f32_create(6);
    // Verify that the memory was assigned.
    expect_should_not_be(arr.data, 0);
    expect_should_be(6, arr.base.length);
    expect_should_be(sizeof(f32), arr.base.stride);

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

    array_f32_destroy(&arr);

    return true;
}

void array_register_tests(void) {
    test_manager_register_test(all_array_tests_after_create, "All array tests after create");
    test_manager_register_test(array_all_iterator_tests, "All array iterator tests");
    test_manager_register_test(array_string_type_test, "array string type tests");
    test_manager_register_test(array_float_type_test, "array float type tests");
}
