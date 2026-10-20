#include <logger.h>
#include <memory/kmemory.h>

#include "containers/array_tests.h"
#include "containers/binary_string_table_tests.h"
#include "containers/darray_tests.h"
#include "containers/freelist_tests.h"
#include "containers/hashtable_tests.h"
#include "containers/stackarray_tests.h"
#include "memory/dynamic_allocator_tests.h"
#include "memory/linear_allocator_tests.h"
#include "parsers/kson_parser_tests.h"
#include "strings/string_tests.h"
#include "test_manager.h"

int main (void) {
	// The containers under test allocate through kallocate, so the memory system must exist first.
	memory_system_configuration memory_config = {.total_alloc_size = MEBIBYTES(512)};
	if (!memory_system_initialize(memory_config)) {
		KFATAL("Failed to initialize the memory system.");
		return 1;
	}

	test_manager_init();

	// TODO: add test registrations here.
	binary_string_table_register_tests();
	string_register_tests();
	array_register_tests();
	darray_register_tests();
	stackarray_register_tests();
	kson_parser_register_tests();
	linear_allocator_register_tests();
	hashtable_register_tests();
	freelist_register_tests();
	dynamic_allocator_register_tests();

	KDEBUG("Starting tests...");

	u32 failed = test_manager_run_tests();

	memory_system_shutdown();
	return failed ? 1 : 0;
}
