
#include "kbuild.h"

// Just an example of how to make it run
int main (int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <source-directory>\n", argv[0]);
		return 1;
	}

	const char *dir_path = argv[1];

	kbuild_config config = {
		.assembly_name = "test_thingy",
		.is_shared = 0,
		.linux_options = {
			.defines = "",
			.compiler_flags = "",
			.include_flags = "",
			.linker_flags = ""}};

	build_project(dir_path, &config);
	return 0;
}
