#include <defines.h>
#include <core/logger.h>
#include <core/kstring.h>

#include "shader_tools.h"

// For executing shell commands.
#include <stdlib.h>

void print_help();
i32 process_shaders(i32 argc, char** argv);

i32 main(i32 argc, char** argv) {
    // The first arg is always the program itself.
    if (argc < 2) {
        KERROR("kohi tools requires at least one argument.");
        print_help();
        return -1;
    }

    // The second argument tells us what mode to go into.
    if (strings_equali(argv[1], "buildshaders") || strings_equali(argv[1], "bshaders")) {
        return process_shaders(argc, argv);
    } else {
        KERROR("Unrecognized argument '%s'.", argv[1]);
        print_help();
        return -2;
    }

    return 0;
}

i32 process_shaders(i32 argc, char** argv) {
    if (argc < 3) {
        KERROR("Build shaders mode requires at least one additional argument.");
        return -3;
    }

    // Starting at third argument. One argument = 1 shader.
    for (u32 i = 2; i < argc; ++i) {
#if KPLATFORM_APPLE != 1
        char* sdk_path = getenv("VULKAN_SDK");
        if (!sdk_path) {
            KERROR("Environment variable VULKAN_SDK not found. Check your Vulkan installation.");
            return -4;
        }
        // const char* bin_folder = "/bin/";
#else
        // // Not needed on macos since it lives in /usr/local
        // const char* sdk_path = "";
        // const char* bin_folder = "";
#endif

        // const char* source_file = argv[i];

        // if (process_source_file(source_file) != 0) {
        //     KERROR("Failed processing shader file, aborting process.");
        //     return -6;
        // }

        // // Construct the command and execute it.
        // char command[4096];
        // string_format(command, "%s%sglslc --target-env=vulkan1.1 -fshader-stage=%s %s -o %s", sdk_path, bin_folder, stage, source_file, out_filename);
        // // Vulkan shader compilation
        // i32 retcode = system(command);
        // if (retcode != 0) {
        //     KERROR("Error compiling shader. See logs. Aborting process.");
        //     return -5;
        // }
    }

    KINFO("Successfully processed all shaders.");
    return 0;
}

void print_help() {
#ifdef KPLATFORM_WINDOWS
    const char* extension = ".exe";
#else
    const char* extension = "";
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
