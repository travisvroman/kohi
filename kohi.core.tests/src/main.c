#include <logger.h>

#include "containers/array_tests.h"
#include "containers/darray_tests.h"
#include "containers/freelist_tests.h"
#include "containers/hashtable_tests.h"
#include "containers/stackarray_tests.h"
#include "memory/dynamic_allocator_tests.h"
#include "memory/linear_allocator_tests.h"
#include "parsers/kson_parser_tests.h"
#include "parsers/xml_parser_tests.h"
#include "strings/string_tests.h"
#include "test_manager.h"

int main(void) {
    // Always initalize the test manager first.
    test_manager_init();

    // TODO: add test registrations here.
    string_register_tests();
    array_register_tests();
    darray_register_tests();
    stackarray_register_tests();
    kson_parser_register_tests();
    xml_parser_register_tests();
    linear_allocator_register_tests();
    hashtable_register_tests();
    freelist_register_tests();
    dynamic_allocator_register_tests();
    string_register_tests();

    KDEBUG("Starting tests...");

    // Execute tests
    test_manager_run_tests();

    return 0;
}
