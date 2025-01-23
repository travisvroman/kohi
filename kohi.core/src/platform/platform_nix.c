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

#    include "threads/ksemaphore.h"
#    include <dlfcn.h>
#    include <fcntl.h> // For O_* constants

// NOTE: Apple's include is on a different path.
#    if defined(KPLATFORM_APPLE)
#        include <sys/semaphore.h>
#    endif

// NOTE: Linux has its own path, plus needs a few more headers.
#    if defined(KPLATFORM_LINUX)
#        include <semaphore.h> // sudo apt install linux-headers
/* #include <sys/stat.h>   // For mode constants */
#    endif

#    include <errno.h> // For error reporting
#    include <pthread.h>
#    include <sys/shm.h>

#    include "containers/darray.h"
#    include "logger.h"
#    include "memory/kmemory.h"
#    include "strings/kstring.h"
#    include "threads/kmutex.h"
#    include "threads/kthread.h"

typedef struct nix_semaphore_internal {
    sem_t* semaphore;
    char* name;
} nix_semaphore_internal;

static u32 semaphore_id = 0;

// NOTE: Begin threads.

b8 kthread_create(pfn_thread_start start_function_ptr, void* params, b8 auto_detach, kthread* out_thread) {
    if (!start_function_ptr) {
        return false;
    }

    // pthread_create uses a function pointer that returns void*, so cold-cast to this type.
    i32 result = pthread_create((pthread_t*)&out_thread->thread_id, 0, (void* (*)(void*))start_function_ptr, params);
    if (result != 0) {
        switch (result) {
        case EAGAIN:
            KERROR("Failed to create thread: insufficient resources to create another thread.");
            return false;
        case EINVAL:
            KERROR("Failed to create thread: invalid settings were passed in attributes..");
            return false;
        default:
            KERROR("Failed to create thread: an unhandled error has occurred. errno=%i", result);
            return false;
        }
    }
    KDEBUG("Starting process on thread id: %#x", out_thread->thread_id);

    // Only save off the handle if not auto-detaching.
    if (!auto_detach) {
        out_thread->internal_data = platform_allocate(sizeof(u64), false);
        *(u64*)out_thread->internal_data = out_thread->thread_id;
    } else {
        // If immediately detaching, make sure the operation is a success.
        result = pthread_detach((pthread_t)out_thread->thread_id);
        if (result != 0) {
            switch (result) {
            case EINVAL:
                KERROR("Failed to detach newly-created thread: thread is not a joinable thread.");
                return false;
            case ESRCH:
                KERROR("Failed to detach newly-created thread: no thread with the id %#x could be found.", out_thread->thread_id);
                return false;
            default:
                KERROR("Failed to detach newly-created thread: an unknown error has occurred. errno=%i", result);
                return false;
            }
        }
    }

    return true;
}

void kthread_destroy(kthread* thread) {
    if (thread->internal_data) {
        kthread_cancel(thread);
    }
}

void kthread_detach(kthread* thread) {
    if (thread->internal_data) {
        i32 result = pthread_detach(*(pthread_t*)thread->internal_data);
        if (result != 0) {
            switch (result) {
            case EINVAL:
                KERROR("Failed to detach thread: thread is not a joinable thread.");
                break;
            case ESRCH:
                KERROR("Failed to detach thread: no thread with the id %#x could be found.", thread->thread_id);
                break;
            default:
                KERROR("Failed to detach thread: an unknown error has occurred. errno=%i", result);
                break;
            }
        }
        platform_free(thread->internal_data, false);
        thread->thread_id = 0;
        thread->internal_data = 0;
    }
}

void kthread_cancel(kthread* thread) {
    if (thread->internal_data) {
        i32 result = pthread_cancel(*(pthread_t*)thread->internal_data);
        if (result != 0) {
            switch (result) {
            case ESRCH:
                KERROR("Failed to cancel thread: no thread with the id %#x could be found.", thread->thread_id);
                break;
            default:
                KERROR("Failed to cancel thread: an unknown error has occurred. errno=%i", result);
                break;
            }
        }
        platform_free(thread->internal_data, false);
        thread->internal_data = 0;
        thread->thread_id = 0;
    }
}

b8 kthread_is_active(kthread* thread) {
    // TODO: Find a better way to verify this.
    return thread->internal_data != 0;
}

void kthread_sleep(kthread* thread, u64 ms) {
    platform_sleep(ms);
}

b8 kthread_wait(kthread* thread) {
    if (thread && thread->internal_data) {
        i32 result = pthread_join(*(pthread_t*)thread->internal_data, 0);
        // When a thread is joined, its lifecycle ends.
        platform_free(thread->internal_data, false);
        thread->internal_data = 0;
        thread->thread_id = 0;
        if (result == 0) {
            return true;
        }
    }
    return false;
}

b8 kthread_wait_timeout(kthread* thread, u64 wait_ms) {
    if (thread && thread->internal_data) {
        KWARN("kthread_wait_timeout - timeout not supported on this platform.");
        // LEFTOFF: Need a wait/notify loop to support timeout.
        i32 result = pthread_join(*(pthread_t*)thread->internal_data, 0);
        // When a thread is joined, its lifecycle ends.
        platform_free(thread->internal_data, false);
        thread->internal_data = 0;
        thread->thread_id = 0;
        if (result == 0) {
            return true;
        }
    }
    return false;
}

u64 platform_current_thread_id(void) {
    return (u64)pthread_self();
}
// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create(kmutex* out_mutex) {
    if (!out_mutex) {
        return false;
    }

    // Initialize
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t mutex;
    i32 result = pthread_mutex_init(&mutex, &mutex_attr);
    if (result != 0) {
        KERROR("Mutex creation failure!");
        return false;
    }

    // Save off the mutex handle.
    out_mutex->internal_data = platform_allocate(sizeof(pthread_mutex_t), false);
    *(pthread_mutex_t*)out_mutex->internal_data = mutex;

    return true;
}

void kmutex_destroy(kmutex* mutex) {
    if (mutex) {
        i32 result = pthread_mutex_destroy((pthread_mutex_t*)mutex->internal_data);
        switch (result) {
        case 0:
            // KTRACE("Mutex destroyed.");
            break;
        case EBUSY:
            KERROR("Unable to destroy mutex: mutex is locked or referenced.");
            break;
        case EINVAL:
            KERROR("Unable to destroy mutex: the value specified by mutex is invalid.");
            break;
        default:
            KERROR("An handled error has occurred while destroy a mutex: errno=%i", result);
            break;
        }

        platform_free(mutex->internal_data, false);
        mutex->internal_data = 0;
    }
}

b8 kmutex_lock(kmutex* mutex) {
    if (!mutex) {
        return false;
    }
    // Lock
    i32 result = pthread_mutex_lock((pthread_mutex_t*)mutex->internal_data);
    switch (result) {
    case 0:
        // Success, everything else is a failure.
        // KTRACE("Obtained mutex lock.");
        return true;
    case EOWNERDEAD:
        KERROR("Owning thread terminated while mutex still active.");
        return false;
    case EAGAIN:
        KERROR("Unable to obtain mutex lock: the maximum number of recursive mutex locks has been reached.");
        return false;
    case EBUSY:
        KERROR("Unable to obtain mutex lock: a mutex lock already exists.");
        return false;
    case EDEADLK:
        KERROR("Unable to obtain mutex lock: a mutex deadlock was detected.");
        return false;
    default:
        KERROR("An handled error has occurred while obtaining a mutex lock: errno=%i", result);
        return false;
    }
}

b8 kmutex_unlock(kmutex* mutex) {
    if (!mutex) {
        return false;
    }
    if (mutex->internal_data) {
        i32 result = pthread_mutex_unlock((pthread_mutex_t*)mutex->internal_data);
        switch (result) {
        case 0:
            // KTRACE("Freed mutex lock.");
            return true;
        case EOWNERDEAD:
            KERROR("Unable to unlock mutex: owning thread terminated while mutex still active.");
            return false;
        case EPERM:
            KERROR("Unable to unlock mutex: mutex not owned by current thread.");
            return false;
        default:
            KERROR("An handled error has occurred while unlocking a mutex lock: errno=%i", result);
            return false;
        }
    }

    return false;
}
// NOTE: End mutexes

b8 ksemaphore_create(ksemaphore* out_semaphore, u32 max_count, u32 start_count) {
    if (!out_semaphore) {
        return false;
    }

    char name_buf[20] = {0};
    string_format_unsafe(name_buf, "/kohi_job_sem_%u", semaphore_id);
    semaphore_id++;

    out_semaphore->internal_data = kallocate(sizeof(nix_semaphore_internal), MEMORY_TAG_ENGINE);
    nix_semaphore_internal* internal = out_semaphore->internal_data;

    if ((internal->semaphore = sem_open(name_buf, O_CREAT, 0664, 1)) == SEM_FAILED) {
        KERROR("Failed to open semaphore");
        return false;
    }
    internal->name = string_duplicate(name_buf);

    return true;
}

void ksemaphore_destroy(ksemaphore* semaphore) {
    if (!semaphore) {
        return;
    }

    nix_semaphore_internal* internal = semaphore->internal_data;
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

b8 ksemaphore_signal(ksemaphore* semaphore) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }

    nix_semaphore_internal* internal = semaphore->internal_data;
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
b8 ksemaphore_wait(ksemaphore* semaphore, u64 timeout_ms) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }

    nix_semaphore_internal* internal = semaphore->internal_data;
    // TODO: handle timeout value using sem_timedwait()
    if (sem_wait(internal->semaphore) != 0) {
        KERROR("Semaphore failed to wait!");
        return false;
    }

    return true;
}

b8 platform_dynamic_library_load(const char* name, dynamic_library* out_library) {
    if (!out_library) {
        return false;
    }
    kzero_memory(out_library, sizeof(dynamic_library));
    if (!name) {
        return false;
    }


    const char* extension = platform_dynamic_library_extension();
    const char* prefix = platform_dynamic_library_prefix();

    const char* filename = string_format("%s%s%s", prefix, name, extension);
    KTRACE("Trying local path '%s' first.", filename);

    void* library = dlopen(filename, RTLD_NOW); // "libtestbed_lib_loaded. so/dylib"
    if (!library) {
        KTRACE("Trying fallback path '%s' because of error: '%s'", filename, dlerror());

        // Try the /lib/ folder next
        string_free(filename);
        filename = string_format("/lib/%s%s%s", prefix, name, extension);

        library = dlopen(filename, RTLD_NOW); // "libtestbed_lib_loaded. so/dylib"
        if (!library) {
            KTRACE("Trying second fallback path '%s' because of error: '%s'", filename, dlerror());

            // try a fallback to /usr/local/lib
            string_free(filename);
            filename = string_format("/usr/local/lib/%s%s%s", prefix, name, extension);

            library = dlopen(filename, RTLD_NOW); // "libtestbed_lib_loaded. so/dylib"
            if (!library) {
                KERROR("Error opening library: %s", dlerror());
                return false;
            }
        }
    }
    KTRACE("Found lib: '%s'", filename);

    out_library->name = string_duplicate(name);
    out_library->filename = filename;

    out_library->internal_data_size = 8;
    out_library->internal_data = library;

    out_library->functions = darray_create(dynamic_library_function);

    return true;
}

b8 platform_dynamic_library_unload(dynamic_library* library) {
    if (!library) {
        return false;
    }

    if (!library->internal_data) {
        return false;
    }

    i32 result = dlclose(library->internal_data);
    if (result != 0) { // Opposite of Windows, 0 means success.
        return false;
    }
    library->internal_data = 0;

    if (library->name) {
        u64 length = string_length(library->name);
        kfree((void*)library->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->filename) {
        u64 length = string_length(library->filename);
        kfree((void*)library->filename, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->functions) {
        u32 count = darray_length(library->functions);
        for (u32 i = 0; i < count; ++i) {
            dynamic_library_function* f = &library->functions[i];
            if (f->name) {
                u64 length = string_length(f->name);
                kfree((void*)f->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
            }
        }

        darray_destroy(library->functions);
        library->functions = 0;
    }

    kzero_memory(library, sizeof(dynamic_library));

    return true;
}

void* platform_dynamic_library_load_function(const char* name, dynamic_library* library) {
    if (!name || !library) {
        return false;
    }

    if (!library->internal_data) {
        return false;
    }

    void* f_addr = dlsym(library->internal_data, name);
    if (!f_addr) {
        return false;
    }

    dynamic_library_function f = {0};
    f.pfn = f_addr;
    f.name = string_duplicate(name);
    darray_push(library->functions, f);

    return f_addr;
}

#endif
