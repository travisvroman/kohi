#include "hashtable_tests.h"
#include "../expect.h"
#include "../test_manager.h"

#include <containers/hashtable.h>
#include <defines.h>

u8 hashtable_should_create_and_destroy(void) {
	hashtable table;
	hashtable_create(sizeof(u64), 4, false, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_set_and_get_successfully(void) {
	hashtable table;
	hashtable_create(sizeof(u64), 4, false, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	const char* keys[4] = {
		"one",
		"two",
		"three",
		"four",
	};
	u64 values[4] = {
		1,
		2,
		3,
		4,
	};

	for (i32 i = 0; i < 4; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_should_be(4, table.filled);

	for (i32 i = 0; i < 4; i++) {
		u64 value;
		b8 get_result = hashtable_get(&table, keys[i], &value);
		expect_to_be_true(get_result);

		expect_should_be(value, values[i]);
	}

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_grow_successfully(void) {
	hashtable table;
	hashtable_create(sizeof(u64), 4, false, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	const char* keys[10] = {
		"one",
		"two",
		"three",
		"four",
		"five",
		"six",
		"seven",
		"eight",
		"nine",
		"ten",
	};
	u64 values[10] = {
		1,
		2,
		3,
		4,
		5,
		6,
		7,
		8,
		9,
		10,
	};

	for (i32 i = 0; i < 10; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_should_be(10, table.filled);
	expect_should_be(16, table.capacity);

	for (i32 i = 0; i < 10; i++) {
		u64 value;
		b8 get_result = hashtable_get(&table, keys[i], &value);
		expect_to_be_true(get_result);

		expect_should_be(value, values[i]);
	}

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_set_and_get_ptr_successfully(void) {
	hashtable table;
	hashtable_create(sizeof(u64*), 4, true, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64*), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_true(table.is_pointer_type);

	const char* keys[4] = {
		"one",
		"two",
		"three",
		"four",
	};
	u64 values[4] = {
		1,
		2,
		3,
		4,
	};

	for (i32 i = 0; i < 4; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_should_be(4, table.filled);

	for (i32 i = 0; i < 4; i++) {
		u64* got_ptr;
		b8 get_result = hashtable_get(&table, keys[i], &got_ptr);
		expect_to_be_true(get_result);

		expect_should_be(got_ptr, &values[i]);
		expect_should_be(*got_ptr, values[i]);
	}

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_get_nonexistant(void) {
	hashtable table;
	hashtable_create(sizeof(u64), 4, false, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	const char* keys[4] = {
		"one",
		"two",
		"three",
		"four",
	};
	u64 values[4] = {
		1,
		2,
		3,
		4,
	};

	for (i32 i = 0; i < 4; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_should_be(4, table.filled);

	u64 out;
	expect_to_be_false(hashtable_get(&table, "five", &out));

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_get_nonexistant_ptr(void) {
	hashtable table;
	hashtable_create(sizeof(u64*), 4, true, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64*), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_true(table.is_pointer_type);

	const char* keys[4] = {
		"one",
		"two",
		"three",
		"four",
	};
	u64 values[4] = {
		1,
		2,
		3,
		4,
	};

	for (i32 i = 0; i < 4; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_should_be(4, table.filled);

	u64* out;
	expect_to_be_false(hashtable_get(&table, "five", &out));

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

u8 hashtable_should_reset_ptr(void) {
	hashtable table;
	hashtable_create(sizeof(u64*), 4, true, &table);

	expect_should_not_be(0, table.memory);
	expect_should_be(sizeof(u64*), table.element_size);
	expect_should_be(4, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_true(table.is_pointer_type);

	const char* keys[4] = {
		"one",
		"two",
		"three",
		"four",
	};
	u64 values[4] = {
		1,
		2,
		3,
		4,
	};

	for (i32 i = 0; i < 4; i++) {
		hashtable_set(&table, keys[i], &values[i]);
	}

	expect_to_be_true(hashtable_set(&table, keys[1], 0));

	expect_should_be(4, table.filled);

	u64* out;
	expect_to_be_true(hashtable_get(&table, "two", &out));
	expect_should_be(out, 0);

	hashtable_destroy(&table);

	expect_should_be(0, table.memory);
	expect_should_be(0, table.element_size);
	expect_should_be(0, table.capacity);
	expect_should_be(0, table.filled);
	expect_to_be_false(table.is_pointer_type);

	return true;
}

void hashtable_register_tests(void) {
	test_manager_register_test(hashtable_should_create_and_destroy, "Hashtable should create and destroy");
	test_manager_register_test(hashtable_should_set_and_get_successfully, "Hashtable should get and set successfully");
	test_manager_register_test(hashtable_should_grow_successfully, "Hashtable should grow successfully");
	test_manager_register_test(hashtable_should_set_and_get_ptr_successfully, "Hashtable should get and set pointer successfully");
	test_manager_register_test(hashtable_should_get_nonexistant, "Hashtable should get nonexistant and fail");
	test_manager_register_test(hashtable_should_get_nonexistant_ptr, "Hashtable should get nonexistant pointer and fail");
	test_manager_register_test(hashtable_should_reset_ptr, "Hashtable should reset pointer");
}
