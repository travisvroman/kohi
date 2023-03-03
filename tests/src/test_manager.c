#include "test_manager.h"

#include <containers/darray.h>
#include <core/logger.h>
#include <core/kstring.h>
#include <core/clock.h>

typedef struct test_entry {
    PFN_test func;
    char* desc;
} test_entry;

static test_entry* tests;

void test_manager_init(void) {
    tests = darray_create(test_entry);
}

void test_manager_register_test(u8 (*PFN_test)(void), char* desc) {
    test_entry e;
    e.func = PFN_test;
    e.desc = desc;
    darray_push(tests, e);
}

void test_manager_run_tests(void) {
    u32 passed = 0;
    u32 failed = 0;
    u32 skipped = 0;

    u32 count = darray_length(tests);

    clock total_time;
    clock_start(&total_time);

    for (u32 i = 0; i < count; ++i) {
        clock test_time;
        clock_start(&test_time);
        u8 result = tests[i].func();
        clock_update(&test_time);

        if (result == true) {
            ++passed;
        } else if (result == BYPASS) {
            KWARN("[SKIPPED]: %s", tests[i].desc);
            ++skipped;
        } else {
            KERROR("[FAILED]: %s", tests[i].desc);
            ++failed;
        }
        char status[20];
        string_format(status, failed ? "*** %d FAILED ***" : "SUCCESS", failed);
        clock_update(&total_time);
        KINFO("Executed %d of %d (skipped %d) %s (%.6f sec / %.6f sec total", i + 1, count, skipped, status, test_time.elapsed, total_time.elapsed);
    }

    clock_stop(&total_time);

    KINFO("Results: %d passed, %d failed, %d skipped.", passed, failed, skipped);
}