#include <containers/darray.h>
#include <defines.h>
#include <kasset_image_utils.h>
#include <logger.h>
#include <stdio.h>
#include <strings/kstring.h>
#include <utils/crc64.h>

// For executing shell commands.
#include <stdlib.h>

#include "kasset_importer.h"

void print_help (void);

// sed -E 's|(KNAME\(\")(.*?)(\"\))|echo "value of: \2"|g' file.c
// sed -E 's|(KNAME\(\")(.*?)(\"\))|../kohi.tools -crc "\1"|ge' ../kohi.runtime/src/core/metrics.h

i32 main (i32 argc, char **argv) {
	// The first arg is always the program itself.
	if (argc < 2) {
		KERROR("kohi tools requires at least one argument.");
		print_help();
		return -1;
	}

	if (argc == 3 && strings_equali(argv[1], "-crc")) {
		u64 length = string_length(argv[2]);
		u64 crc = crc64(0, (u8 *)argv[2], length);

		printf("%llu", crc);
		return 0;
	}

	// The second argument tells us what mode to go into.
	if (strings_equali(argv[1], "combine") || strings_equali(argv[1], "cmaps")) {
		return combine_texture_maps(argc, argv);
	} else if (strings_equali(argv[1], "importmanifest") || strings_equali(argv[1], "iman")) {
		if (argc < 3) {
			KERROR("importmanifest command requires an argument specifying the manifest path.");
			return -3;
		}
		const char *manifest_path = argv[2];

		// Parse additional flags.
		kimport_flag_bits flags = KIMPORT_FLAG_NONE;
		for (i32 i = 3; i < argc; ++i) {
			if (strings_equali(argv[i], "--updated-only") || strings_equali(argv[i], "--u")) {
				FLAG_SET(flags, KIMPORT_FLAG_UPDATED_ONLY_BIT, true);
				KINFO("Only processing updated assets.");
			}
		}

		if (!import_all_from_manifest(manifest_path, flags)) {
			KERROR("Manifest import error. See logs for details.");
			return -4;
		}

	} else {
		KERROR("Unrecognized argument '%s'.", argv[1]);
		print_help();
		return -2;
	}

	return 0;
}

void print_help (void) {
#ifdef KPLATFORM_WINDOWS
	const char *extension = ".exe";
#else
	const char *extension = "";
#endif
	KINFO(
		"Kohi Game Engine Tools, Copyright 2021-2022 Travis Vroman.\n\
  usage:  tools%s <mode> [arguments...]\n\
  \n\
  modes:\n\
    buildshaders -  Builds shaders provided in arguments. For example,\n\
                    to compile Vulkan shaders to .spv from GLSL, a list of filenames\n\
                    should be provided that all end in <stage>.glsl, where <stage> is\n\
                    replaced by one of the following supported stages:\n\
                        vert, frag, geom, comp\n\
                    The compiled .spv file is output to the same path as the input file.\n",
		extension);
}
