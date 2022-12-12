#include "shader_tools.h"

#include <core/kmemory.h>
#include <platform/filesystem.h>
#include <containers/darray.h>
#include <core/kstring.h>
#include <core/logger.h>

i32 process_source_file(const char* source_file) {
    char end_path[10];
    i32 length = string_length(source_file);
    string_ncopy(end_path, source_file + length - 9, 9);

    // Parse the stage from the file name.
    char stage[5];
    if (strings_equali(end_path, "frag.glsl")) {
        string_ncopy(stage, "frag", 4);
    } else if (strings_equali(end_path, "vert.glsl")) {
        string_ncopy(stage, "vert", 4);
    } else if (strings_equali(end_path, "geom.glsl")) {
        string_ncopy(stage, "geom", 4);
    } else if (strings_equali(end_path, "comp.glsl")) {
        string_ncopy(stage, "comp", 4);
    }
    stage[4] = 0;

    // Output filename, just has different extension of spv.
    char out_filename[255];
    string_ncopy(out_filename, source_file, length - 4);
    string_ncopy(out_filename + length - 4, "spv", 3);
    out_filename[length - 1] = 0;

    // Some output.
    KINFO("Processing %s -> %s...", source_file, out_filename);

    // TODO: search the file for includes. For each one, replace the include itself
    // with the code from the found file. Abort if not found.
    // Keep scanning until no #includes are found to catch recursive includes.
    // Write this text to a file (doubles nicely for debugging purposes)
    // Take the total combined text and feed that to glslc

    // Temp output filename, extension of isf (intermediate shader file)
    char intermediate_filename[255];
    string_ncopy(intermediate_filename, source_file, length - 4);
    string_ncopy(intermediate_filename + length - 4, "isf", 3);
    intermediate_filename[length - 1] = 0;

    // Open the source file.
    file_handle f;
    if (!filesystem_open(source_file, FILE_MODE_READ, false, &f)) {
        KERROR("Kohi shader compilation error - unable to open file for text reading: '%s'.", source_file);
        return -6;
    }

    u64 file_size = 0;
    if (!filesystem_size(&f, &file_size)) {
        KERROR("Kohi shader compilation error - Unable to text read file: %s.", source_file);
        filesystem_close(&f);
        return -7;
    }

    char* source_text = kallocate(sizeof(char) * file_size, MEMORY_TAG_ARRAY);
    u64 read_size = 0;
    if (!filesystem_read_all_text(&f, source_text, &read_size)) {
        KERROR("Kohi shader compilation error - Unable to text read file: %s.", source_file);
        filesystem_close(&f);
        return -8;
    }

    // Close the source file
    filesystem_close(&f);

    // Search for any includes.
    char** lines = darray_create(char*);
    u32 line_count = string_split(source_text, '\n', &lines, true, false);
    for(u32 i = 0;i<line_count;++i) {
        const char* line = lines[i];
        // NOTE: This is a fairly rigid way to search for file names, and doesn't
        // include handling quotes, extra spaces, etc.
        if(line[0] == '#') {
            // possible #include
            if(strings_nequal(line, "#include", 8)) {
                // char filename[256]; // Assume max length of 256.
                // string_mid(filename, line, 9, 0); // Get everything after "#include ".
                // char* trimmed_filename = string_trim(filename);
                // TODO: Attempt to open the file, parse for includes, etc.
                // TODO: Combine final recursive result with calling file.
                // TODO: Might need to pass str buffer recursively. Allocate a big buffer
                // to account for this?
            }
        }
    }
    return 0; // TODO: is this right?
}