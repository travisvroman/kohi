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

#include "core/ksemaphore.h"
#include "platform.h"

#if defined(KPLATFORM_LINUX) || defined(KPLATFORM_APPLE)

#include <dlfcn.h>
#include <semaphore.h>
#include <sys/semaphore.h>
#include <sys/shm.h>

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"

typedef struct nix_semaphore_internal {
    sem_t *semaphore;
    char *name;
} nix_semaphore_internal;

static u32 semaphore_id = 0;

b8 ksemaphore_create(ksemaphore *out_semaphore, u32 max_count, u32 start_count) {
    if (!out_semaphore) {
        return false;
    }

    char name_buf[20] = {0};
    string_format(name_buf, "/kohi_job_sem_%u", semaphore_id);
    semaphore_id++;

    out_semaphore->internal_data = kallocate(sizeof(nix_semaphore_internal), MEMORY_TAG_ENGINE);
    nix_semaphore_internal *internal = out_semaphore->internal_data;

    if ((internal->semaphore = sem_open(name_buf, O_CREAT, 0664, 1)) == SEM_FAILED) {
        KERROR("Failed to open semaphore");
        return false;
    }
    internal->name = string_duplicate(name_buf);

    return true;
}

void ksemaphore_destroy(ksemaphore *semaphore) {
    if (!semaphore) {
        return;
    }

    nix_semaphore_internal *internal = semaphore->internal_data;
    if (sem_close(internal->semaphore) == -1) {
        KERROR("Failed to close semaphore.");
    }

    if (sem_unlink(internal->name) == -1) {
        KERROR("Failed to unlink semaphore");
    }

    string_free(internal->name);
    kfree(semaphore->internal_data, sizeof(nix_semaphore_internal), MEMORY_TAG_ENGINE);
    semaphore->internal_data = 0;
}

b8 ksemaphore_signal(ksemaphore *semaphore) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }

    nix_semaphore_internal *internal = semaphore->internal_data;
    if (sem_post(internal->semaphore) != 0) {
        KERROR("Semaphore failed to post!");
        return false;
    }

    return true;
}

/**
 * Decreases the semaphore count by 1. If the count reaches 0, the
 * semaphore is considered unsignaled and this call blocks until the
 * semaphore is signaled by ksemaphore_signal.
 */
b8 ksemaphore_wait(ksemaphore *semaphore, u64 timeout_ms) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }

    nix_semaphore_internal *internal = semaphore->internal_data;
    // TODO: handle timeout value using sem_timedwait()
    if (sem_wait(internal->semaphore) != 0) {
        KERROR("Semaphore failed to wait!");
        return false;
    }

    return true;
}

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
        KERROR("Error opening library: %s", dlerror());
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
    library->internal_data = 0;

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

#endif
