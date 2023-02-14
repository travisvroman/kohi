/**
 * @file platform_nix.c
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Code shared by unix-like platforms (i.e. Linux and macOS).
 * @version 1.0
 * @date 2023-02-04
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#include "platform.h"

#if defined(KPLATFORM_LINUX) || defined(KPLATFORM_APPLE)

#include "core/kmemory.h"
#include "core/kstring.h"
#include "containers/darray.h"

#include <dlfcn.h>

b8 platform_dynamic_library_load(const char *name, dynamic_library *out_library) {
    if (!out_library) {
        return false;
    }
    kzero_memory(out_library, sizeof(dynamic_library));
    if (!name) {
        return false;
    }

    char filename[260];  // NOTE: same as Windows, for now.
    kzero_memory(filename, sizeof(char) * 260);

    const char *extension = platform_dynamic_library_extension();
    const char *prefix = platform_dynamic_library_prefix();

    string_format(filename, "%s%s%s", prefix, name, extension);

    void *library = dlopen(filename, RTLD_NOW);  // "libtestbed_lib_loaded.dylib"
    if (!library) {
        return false;
    }

    out_library->name = string_duplicate(name);
    out_library->filename = string_duplicate(filename);

    out_library->internal_data_size = 8;
    out_library->internal_data = library;

    out_library->functions = darray_create(dynamic_library_function);

    return true;
}

b8 platform_dynamic_library_unload(dynamic_library *library) {
    if (!library) {
        return false;
    }

    if (!library->internal_data) {
        return false;
    }

    i32 result = dlclose(library->internal_data);
    if (result != 0) {  // Opposite of Windows, 0 means success.
        return false;
    }

    if (library->name) {
        u64 length = string_length(library->name);
        kfree((void *)library->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->filename) {
        u64 length = string_length(library->filename);
        kfree((void *)library->filename, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->functions) {
        u32 count = darray_length(library->functions);
        for (u32 i = 0; i < count; ++i) {
            dynamic_library_function *f = &library->functions[i];
            if (f->name) {
                u64 length = string_length(f->name);
                kfree((void *)f->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
            }
        }

        darray_destroy(library->functions);
        library->functions = 0;
    }

    kzero_memory(library, sizeof(dynamic_library));

    return true;
}

b8 platform_dynamic_library_load_function(const char *name, dynamic_library *library) {
    if (!name || !library) {
        return false;
    }

    if (!library->internal_data) {
        return false;
    }

    void *f_addr = dlsym(library->internal_data, name);
    if (!f_addr) {
        return false;
    }

    dynamic_library_function f = {0};
    f.pfn = f_addr;
    f.name = string_duplicate(name);
    darray_push(library->functions, f);

    return true;
}

const char *platform_dynamic_library_prefix() {
    return "lib";
}

#endif