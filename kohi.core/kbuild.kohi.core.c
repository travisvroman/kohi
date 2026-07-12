/* Build recipe for kohi.core. Build this from the base directory initially with:

clang -o kbuilder kbuild.kohi.core.c

To run, do the following:

./kbuilder . [D|R|C]

where d=debug, r=release, c=clean

*/

#include "../submodules/kbuild/src/kbuild.h"
#include <stdlib.h>
#include <string.h>

// Entry point to the builder.
int main (int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <source-directory> <build-type[D,R]>\n", argv[0]);
		return 1;
	}

	const char *dir_path = argv[1];
	const char *build_type = argv[2];

	// All compiler flags/rules used across the board.
	const char *base_cflags = "-std=gnu11 -Wall -Wextra -Werror -Wno-error=deprecated-declarations -Wno-error=unused-function \
-Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -Wstrict-prototypes -Wno-unused-parameter \
-Wno-missing-field-initializers -Wno-tautological-compare \
-fvisibility=hidden "; // NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say otherwise are exported (i.e. via KAPI).

	const char *base_defines = "-DKEXPORT ";
	const char *base_lflags = "-Lbin -Llib -shared ";
	const char *base_includeflags = "-Isrc ";

	char defines[16384];		// big, but should be enough to handle all flags.
	char compiler_flags[16384]; // big, but should be enough to handle all flags.
	char linker_flags[16384];	// big, but should be enough to handle all flags.
	char include_flags[16384];	// big, but should be enough to handle all flags.

	strcpy(compiler_flags, base_cflags);
	strcpy(defines, base_defines);
	strcpy(linker_flags, base_lflags);
	strcpy(include_flags, base_includeflags);

#if KPLATFORM_WINDOWS
	// strcat(compiler_flags, "");
	strcat(linker_flags, "-fdeclspec -Wno-cast-function-type-mismatch -luser32 -lgdi32 -lwinmm -Xlinker /INCREMENTAL");
	strcat(defines, "-D_CRT_SECURE_NO_WARNINGS -DUNICODE");

#elif KPLATFORM_LINUX
	strcat(compiler_flags, "-Wno-cast-function-type-mismatch -fPIC");
	strcat(linker_flags, "-lxcb -lX11 -lX11-xcb -lxkbcommon -lxcb-xkb -lm -ldl -L/usr/X11R6/lib -Wl,--no-undefined,--no-allow-shlib-undefined,-rpath='$$ORIGIN'");

#elif KPLATFORM_APPLE
	// strcat(compiler_flags, "");
	strcat(linker_flags, "-dynamiclib -install_name @rpath/lib$(ASSEMBLY_NAME).dylib -lobjc -framework AppKit -framework QuartzCore -framework DiskArbitration -framework CoreFoundation -framework UniformTypeIdentifiers");
	// strcat(defines, "");

#endif

	int clean = 0;

	if (!strcmp(build_type, "D") || !strcmp(build_type, "d")) {
		strcat(defines, " -D_DEBUG ");
		strcat(compiler_flags, " -g -MD -O0 -fno-omit-frame-pointer ");
		strcat(linker_flags, " -g ");
		build_type = "D";
	} else if (!strcmp(build_type, "R") || !strcmp(build_type, "r")) {
		strcat(defines, " -DKRELEASE ");
		strcat(compiler_flags, " -g -MD -O2 ");
		build_type = "R";
	} else if (!strcmp(build_type, "C") || !strcmp(build_type, "c")) {
		clean = 1;
	}

	kbuild_config config = {
		.assembly_name = "kohi.core",
		.is_shared = 1,
		.is_clean_build = clean,
		.platform_options = {
			.defines = defines,
			.compiler_flags = compiler_flags,
			.include_flags = include_flags,
			.linker_flags = linker_flags}};

	// Before building, run versiongen.
	{
		char vgcmd[16384];
		memset(vgcmd, 0, 16384);
		strcpy(vgcmd, "../misc/versiongen version.txt -outfile=src/");
		strcat(vgcmd, config.assembly_name);
		strcat(vgcmd, "_version.h -build_type=");
		strcat(vgcmd, build_type);
		system(vgcmd);
	}
	// Generate compiler flags before building too.
	{
		char cfcmd[16384];
		memset(cfcmd, 0, 16384);
		strcpy(cfcmd, "../misc/cfgen -outfile=compile_flags.txt -ferror-limit=0 ");
		strcat(cfcmd, include_flags);
		strcat(cfcmd, " ");
		strcat(cfcmd, defines);
		system(cfcmd);
	}
	build_project(dir_path, &config, __FILE_NAME__);
	return 0;
}
