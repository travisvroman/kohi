#include <containers/darray.h>
#include <defines.h>
#include <logger.h>
#include <stdio.h>
#include <strings/kstring.h>
#include <utils/crc64.h>

// For executing shell commands.
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

void print_help(void);
i32 combine_texture_maps(i32 argc, char** argv);

// sed -E 's|(KNAME\(\")(.*?)(\"\))|echo "value of: \2"|g' file.c
// sed -E 's|(KNAME\(\")(.*?)(\"\))|../kohi.tools -crc "\1"|ge' ../kohi.runtime/src/core/metrics.h

i32 main(i32 argc, char** argv) {
    // The first arg is always the program itself.
    if (argc < 2) {
        KERROR("kohi tools requires at least one argument.");
        print_help();
        return -1;
    }

    if (argc == 3 && strings_equali(argv[1], "-crc")) {
        u64 length = string_length(argv[2]);
        u64 crc = crc64(0, (u8*)argv[2], length);

        printf("%llu", crc);
        return 0;
    }

    // The second argument tells us what mode to go into.
    if (strings_equali(argv[1], "combine") || strings_equali(argv[1], "cmaps")) {
        return combine_texture_maps(argc, argv);
    } else {
        KERROR("Unrecognized argument '%s'.", argv[1]);
        print_help();
        return -2;
    }

    return 0;
}

typedef enum map_type {
    MAP_TYPE_METALLIC,
    MAP_TYPE_ROUGHNESS,
    MAP_TYPE_AO,
    MAP_TYPE_MAX
} map_type;

typedef struct channel_map {
    char* file_path;
    i32 width, height;
    i32 channels_in_file;
    u8* data;
} channel_map;

i32 combine_texture_maps(i32 argc, char** argv) {
    if (argc < 3) {
        KERROR("Build shaders mode requires at least one additional argument.");
        return -3;
    }

    // tools.exe combine|cmaps outfile=[filename] ao=[filename] metallic=[filename] roughness=[filename]
    // Combine them into the following format:
    // output texture will be RGBA - channels:
    // - R: metallic
    // - G: roughness
    // - B: ao
    // - A: reserved/set to 1

    // Always flip y when loading in.
    stbi_set_flip_vertically_on_load_thread(true);

    channel_map maps[MAP_TYPE_MAX] = {0};
    char out_file_path[1024] = {0};

    // Starting at third argument. One argument = 1 shader.
    for (u32 i = 2; i < argc; ++i) {
        char** parts = darray_create(char*);
        string_split(argv[i], '=', &parts, true, false);

        if (strings_equali(parts[0], "metallic")) {
            maps[MAP_TYPE_METALLIC].file_path = string_duplicate(parts[1]);
        } else if (strings_equali(parts[0], "roughness")) {
            maps[MAP_TYPE_ROUGHNESS].file_path = string_duplicate(parts[1]);
        } else if (strings_equali(parts[0], "ao")) {
            maps[MAP_TYPE_AO].file_path = string_duplicate(parts[1]);
        } else if (strings_equali(parts[0], "outfile")) {
            string_ncopy(out_file_path, parts[1], 1024);
        } else {
            KERROR("Unrecognized map type '%s'", parts[0]);
            return -5;
        }
    }
    if (out_file_path[0] == 0) {
        KERROR("parameter outfile is required. Usage: outfile=[filename]");
        return -4;
    }

    for (u32 i = 0; i < MAP_TYPE_MAX; ++i) {
        if (!maps[i].file_path) {
            continue;
        }

        // Load the image data.
        const i32 channels_required = 4;
        maps[i].data = stbi_load(maps[i].file_path, &maps[i].width, &maps[i].height, &maps[i].channels_in_file, channels_required);
        if (!maps[i].data) {
            KFATAL("Failed to load file '%s'", maps[i].file_path);
            return -6;
        }
    }

    i32 width = -1;
    i32 height = -1;
    for (u32 i = 0; i < MAP_TYPE_MAX; ++i) {
        if (maps[i].file_path) {
            if (width == -1 || height == -1) {
                // Dimensions not set, use first dimension.
                width = maps[i].width;
                height = maps[i].height;
            } else {
                // Validate that the dimensions match.
                if (maps[i].width != width || maps[i].height != height) {
                    KERROR("All texture maps must be the same width and height.");
                    return -7;
                }
            }
        }
    }
    if (width == -1 || height == -1) {
        KERROR("Unable to obtain width and height - no textures set?");
        return -8;
    }
    for (u32 i = 0; i < MAP_TYPE_MAX; ++i) {
        if (!maps[i].file_path) {
            maps[i].data = malloc(sizeof(u8) * width * height * 4);
            if (i == MAP_TYPE_AO) {
                // white
                u32 count = width * height * 4;
                for (u32 j = 0; j < count; ++j) {
                    maps[i].data[j] = 255;
                }
            } else if (i == MAP_TYPE_ROUGHNESS) {
                // medium grey
                for (u64 row = 0; row < width; ++row) {
                    for (u64 col = 0; col < height; ++col) {
                        u64 index = (row * width) + col;
                        u64 index_bpp = index * 4;
                        // Set to a medium gray.
                        maps[i].data[index_bpp + 0] = 128;
                        maps[i].data[index_bpp + 1] = 128;
                        maps[i].data[index_bpp + 2] = 128;
                        maps[i].data[index_bpp + 3] = 255;
                    }
                }
            } else if (i == MAP_TYPE_METALLIC) {
                // white
                u32 count = width * height * 4;
                for (u32 j = 0; j < count; ++j) {
                    maps[i].data[j] = 0;
                }
            }
        }
    }

    // combine the data.
    u8* target_buffer = malloc(sizeof(u8) * width * height * 4);
    for (u64 row = 0; row < width; ++row) {
        for (u64 col = 0; col < height; ++col) {
            u64 index = (row * width) + col;
            u64 index_bpp = index * 4;
            // Set to a medium gray.
            target_buffer[index_bpp + 0] = maps[MAP_TYPE_METALLIC].data[index_bpp + 0];
            target_buffer[index_bpp + 1] = maps[MAP_TYPE_ROUGHNESS].data[index_bpp + 1];
            target_buffer[index_bpp + 2] = maps[MAP_TYPE_AO].data[index_bpp + 2];
            target_buffer[index_bpp + 3] = 255; // reserved
        }
    }

    if (!stbi_write_png(out_file_path, width, height, 4, target_buffer, 4 * width)) {
        KERROR("Error writing outfile.");
        return -9;
    }

    KINFO("Successfully processed all maps.");
    return 0;
}

void print_help(void) {
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
