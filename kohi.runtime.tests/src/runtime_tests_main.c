#include <logger.h>

#include "test_manager.h"
#include "core/sim_clock_tests.h"
#include "world/kscene_tests.h"

int main (void) {
	// Always initalize the test manager first.
	test_manager_init();

	// TODO: add test registrations here.
	kscene_register_tests();
	sim_clock_register_tests();

	KDEBUG("Starting Kohi Runtime tests...");

	// Execute tests
	test_manager_run_tests();

	return 0;
}
