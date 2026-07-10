#include "binary_string_table_tests.h"

#include "../expect.h"
#include "../test_manager.h"
#include "strings/kstring.h"

#include <containers/binary_string_table.h>

#include <defines.h>
#include <memory/kmemory.h>

static b8 create_and_verify (binary_string_table *out_table) {
	*out_table = binary_string_table_create();
	// The lookup should be created.
	expect_should_not_be(out_table->lookup, KNULL);
	// Verify that memory has not yet beed assigned within the table's data.
	expect_should_be(KNULL, out_table->header.data_block_size);
	expect_should_be(KNULL, out_table->header.entry_count);
	expect_should_be(KNULL, out_table->data);

	return true;
}

static b8 destroy_and_verify (binary_string_table *table) {
	binary_string_table_destroy(table);

	// Verify that memory has been cleared
	expect_should_be(table->lookup, KNULL);
	expect_should_be(KNULL, table->header.data_block_size);
	expect_should_be(KNULL, table->header.entry_count);
	expect_should_be(KNULL, table->data);

	return true;
}

static u8 binary_string_table_create_and_destroy (void) {
	binary_string_table string_table = {0};
	if (!create_and_verify(&string_table)) {
		return false;
	}

	if (!destroy_and_verify(&string_table)) {
		return false;
	}

	return true;
}

static u8 all_binary_string_table_tests (void) {
	binary_string_table string_table = {0};
	if (!create_and_verify(&string_table)) {
		return false;
	}

	// Push one string and verify state.
	const char *str0 = "some_string";
	expect_should_be(11, string_length(str0));
	u32 index0 = binary_string_table_add(&string_table, str0);
	expect_should_be(0, index0);
	expect_should_be(string_length(str0), string_table.header.data_block_size);
	expect_should_be(1, string_table.header.entry_count);
	expect_should_not_be(0, string_table.data);

	const char *str0_after = binary_string_table_get(&string_table, index0);
	expect_to_be_true(strings_equal(str0_after, str0));

	// Push a second string and verify state.
	const char *str1 = "some_string 2";
	u32 index1 = binary_string_table_add(&string_table, str1);
	expect_should_be(1, index1);
	expect_should_be(string_length(str0) + string_length(str1), string_table.header.data_block_size);
	expect_should_be(2, string_table.header.entry_count);
	expect_should_not_be(0, string_table.data);

	const char *str1_after = binary_string_table_get(&string_table, index1);
	expect_to_be_true(strings_equal(str1_after, str1));

	if (!destroy_and_verify(&string_table)) {
		return false;
	}

	return true;
}

void binary_string_table_register_tests (void) {
	test_manager_register_test(binary_string_table_create_and_destroy, "Binary string table creates and destroys");
	test_manager_register_test(all_binary_string_table_tests, "All binary string table tests");
}
