#include <logger.h>
#include <memory/kmemory.h>

#include "core/sim_clock_tests.h"
#include "renderer/png_writer_tests.h"
#include "test_manager.h"
#include "world/kscene_tests.h"

int main (void) {
	// Anything under test may allocate through kallocate, so the memory system must exist first.
	memory_system_configuration memory_config = {.total_alloc_size = MEBIBYTES(256)};
	if (!memory_system_initialize(memory_config)) {
		KFATAL("Failed to initialize the memory system.");
		return 1;
	}

	test_manager_init();

	kscene_register_tests();
	sim_clock_register_tests();
	png_writer_register_tests();

	KDEBUG("Starting Kohi Runtime tests...");

	u32 failed = test_manager_run_tests();

	memory_system_shutdown();
	return failed ? 1 : 0;
}
