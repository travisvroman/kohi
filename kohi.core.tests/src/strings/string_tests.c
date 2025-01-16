
#include "../expect.h"
#include "../test_manager.h"

#include <defines.h>
#include <memory/kmemory.h>
#include <strings/kstring.h>

static u8 kstr_ncmp_tests(void) {

    const char* str_a = "texture";
    const char* str_b = "text";

    // set max length of shorter string
    {
        // Same strings.
        i64 result = kstr_ncmp(str_a, str_a, 4);
        expect_should_be(0, result);

        // First string longer than the second.
        result = kstr_ncmp(str_a, str_b, 4);
        expect_should_be(0, result);

        // Second string longer than the first.
        result = kstr_ncmp(str_b, str_a, 4);
        expect_should_be(0, result);
    }

    // u32 max length
    {
        // Same strings.
        i64 result = kstr_ncmp(str_a, str_a, U32_MAX);
        expect_should_be(0, result);

        // First string longer than the second.
        result = kstr_ncmp(str_a, str_b, U32_MAX);
        expect_should_be(117, result);

        // Second string longer than the first.
        result = kstr_ncmp(str_b, str_a, U32_MAX);
        expect_should_be(-117, result);
    }

    

    return true;
}

void string_register_tests(void) {
    test_manager_register_test(kstr_ncmp_tests, "All kstr_ncmp tests pass");
}
