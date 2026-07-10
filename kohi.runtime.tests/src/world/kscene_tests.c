#include "kscene_tests.h"

#include <containers/darray.h>
#include <defines.h>
#include <world/kscene.h>
#include <world/world_utils.h>

#include "../expect.h"
#include "../test_manager.h"
#include "logger.h"

u8 kscene_entity_handle_should_pack_and_unpack (void) {
	// First test - handle type and index of 0 should create a zero-handle, which is technically valid.
	kentity_type original_entity_type = KENTITY_TYPE_NONE;
	u16 original_entity_type_index = 0;
	u16 original_entity_hierarchy_index = 0;
	u16 original_reserved = 0;

	kentity test_handle = kentity_pack(original_entity_type, original_entity_type_index, original_entity_hierarchy_index, original_reserved);
	expect_should_be(0, test_handle);

	kentity_type extracted_type;
	u16 extracted_index;
	u16 extracted_hierarchy_node_index;
	u16 extracted_reserved;

	kentity_unpack(test_handle, extracted_type, extracted_index, extracted_hierarchy_node_index, extracted_reserved);
	KINFO("kentity handle creation values - zero values test: handle/extracted type/index = %u/%u/%u", test_handle, extracted_type, extracted_index);

	expect_should_be(original_entity_type, extracted_type);
	expect_should_be(original_entity_type_index, extracted_index);
	expect_should_be(original_entity_hierarchy_index, extracted_hierarchy_node_index);
	expect_should_be(original_reserved, extracted_reserved);

	// Second test - test with nonzero values.
	original_entity_type = KENTITY_TYPE_POINT_LIGHT;
	original_entity_type_index = 13;
	original_entity_hierarchy_index = 69;
	original_reserved = 420;

	test_handle = kentity_pack(original_entity_type, original_entity_type_index, original_entity_hierarchy_index, original_reserved);

	kentity_unpack(test_handle, extracted_type, extracted_index, extracted_hierarchy_node_index, extracted_reserved);
	KINFO("kentity handle creation values - nonzero values test: handle/extracted type/index = %u/%u/%u", test_handle, extracted_type, extracted_index);

	expect_should_be(original_entity_type, extracted_type);
	expect_should_be(original_entity_type_index, extracted_index);
	expect_should_be(original_entity_hierarchy_index, extracted_hierarchy_node_index);
	expect_should_be(original_reserved, extracted_reserved);

	// Third test - test with u16 max values - should create 'invalid' handle.
	original_entity_type = U16_MAX;
	original_entity_type_index = U16_MAX;
	original_entity_hierarchy_index = U16_MAX;
	original_reserved = U16_MAX;

	test_handle = kentity_pack(original_entity_type, original_entity_type_index, original_entity_hierarchy_index, original_reserved);

	kentity_unpack(test_handle, extracted_type, extracted_index, extracted_hierarchy_node_index, extracted_reserved);
	KINFO("kentity handle creation values - max values test: handle/extracted type/index = %u/%u/%u", test_handle, extracted_type, extracted_index);

	expect_should_be(KENTITY_INVALID, test_handle);
	expect_should_be(original_entity_type, extracted_type);
	expect_should_be(original_entity_type_index, extracted_index);
	expect_should_be(original_entity_hierarchy_index, extracted_hierarchy_node_index);
	expect_should_be(original_reserved, extracted_reserved);

	return true;
}

void kscene_register_tests (void) {
	test_manager_register_test(kscene_entity_handle_should_pack_and_unpack, "kscene entity handle should pack and unpack successfully.");
}
