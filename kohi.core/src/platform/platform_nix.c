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

// #ifdef _POSIX_C_SOURCE
// #undef _POSIX_C_SOURCE
// #endif
// #	define _POSIX_C_SOURCE 200809L
#	define _XOPEN_SOURCE 700 // Enables POSIX/XSI extensions for C99

#	include "threads/ksemaphore.h"
#	include <dlfcn.h>

#	include <errno.h> // For error reporting
#	include <pthread.h>
#	include <time.h>
#	include <sys/shm.h>

#	include "containers/darray.h"
#	include "logger.h"
#	include "memory/kmemory.h"
#	include "strings/kstring.h"
#	include "threads/kmutex.h"
#	include "threads/kthread.h"

// A process-local counting semaphore built on a mutex and a condition variable.
// POSIX named semaphores (sem_open) are system-wide, so two engine processes
// would share them, and unnamed ones (sem_init) are not supported on macOS.
typedef struct nix_semaphore_internal {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	u32 count;
	u32 max_count;
} nix_semaphore_internal;

// NOTE: Begin threads.

typedef struct kthread_work_info {
	pfn_thread_start start;
	void *params;
} kthread_work_info;

static void *kthread_do_work (void *params) {
	kthread_work_info *info = params;
	if (!info->start(info->params)) {
		KERROR("Failed to start kthread due to failure in user callback. Check logs for details");
	}

	// Ensure the info is freed.
	if (info) {
		kfree(info);
	}

	return KNULL;
}

b8 kthread_create (pfn_thread_start start_function_ptr, void *params, b8 auto_detach, kthread *out_thread) {
	if (!start_function_ptr) {
		return false;
	}

	// NOTE: this gets freed within kthread_do_work(), so unless the thread fails to create
	// there is no need to do it elsewhere.
	kthread_work_info *info = KALLOC_TYPE(kthread_work_info, MEMORY_TAG_PLATFORM);
	info->params = params;
	info->start = start_function_ptr;
	i32 result = pthread_create((pthread_t *)&out_thread->thread_id, 0, kthread_do_work, info);
	if (result != 0) {
		switch (result) {
		case EAGAIN:
			KERROR("Failed to create thread: insufficient resources to create another thread.");
			kfree(info);
			return false;
		case EINVAL:
			KERROR("Failed to create thread: invalid settings were passed in attributes..");
			kfree(info);
			return false;
		default:
			KERROR("Failed to create thread: an unhandled error has occurred. errno=%i", result);
			kfree(info);
			return false;
		}
	}
	/* KDEBUG("Starting process on thread id: %#x", out_thread->thread_id); */

	// Only save off the handle if not auto-detaching.
	if (!auto_detach) {
		out_thread->internal_data = info;
	} else {
		// If immediately detaching, make sure the operation is a success.
		result = pthread_detach((pthread_t)out_thread->thread_id);
		if (result != 0) {
			switch (result) {
			case EINVAL:
				KERROR("Failed to detach newly-created thread: thread is not a joinable thread.");
				kfree(info);
				return false;
			case ESRCH:
				KERROR("Failed to detach newly-created thread: no thread with the id %#x could be found.", out_thread->thread_id);
				kfree(info);
				return false;
			default:
				KERROR("Failed to detach newly-created thread: an unknown error has occurred. errno=%i", result);
				kfree(info);
				return false;
			}
		}
	}

	return true;
}

void kthread_destroy (kthread *thread) {
	if (thread->thread_id) {
		kthread_cancel(thread);
	}
}

void kthread_detach (kthread *thread) {
	if (thread->thread_id) {
		i32 result = pthread_detach((pthread_t)thread->thread_id);
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
		thread->internal_data = KNULL;
		thread->thread_id = 0;
	}
}

void kthread_cancel (kthread *thread) {
	if (thread->thread_id) {
		i32 result = pthread_cancel((pthread_t)thread->thread_id);
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
		thread->internal_data = KNULL;
		thread->thread_id = 0;
	}
}

b8 kthread_is_active (kthread *thread) {
	// TODO: Find a better way to verify this.
	return thread->thread_id != 0;
}

void kthread_sleep (kthread *thread, u64 ms) {
	platform_sleep(ms);
}

b8 kthread_wait (kthread *thread) {
	if (thread && thread->thread_id) {
		i32 result = pthread_join((pthread_t)thread->thread_id, 0);
		thread->internal_data = KNULL;
		thread->thread_id = 0;
		if (result == 0) {
			return true;
		}
	}
	return false;
}

b8 kthread_wait_timeout (kthread *thread, u64 wait_ms) {
	if (thread && thread->thread_id) {
		KWARN("kthread_wait_timeout - timeout not supported on this platform.");
		// TODO: Need a wait/notify loop to support timeout.
		i32 result = pthread_join((pthread_t)thread->thread_id, 0);
		thread->internal_data = KNULL;
		thread->thread_id = 0;
		if (result == 0) {
			return true;
		}
	}
	return false;
}

u64 platform_current_thread_id (void) {
	return (u64)pthread_self();
}
// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create (kmutex *out_mutex) {
	if (!out_mutex) {
		return false;
	}

	// Initialize
	pthread_mutexattr_t mutex_attr;
	pthread_mutexattr_init(&mutex_attr);
#	ifdef PTHREAD_MUTEX_RECURSIVE
	pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
#	else
	pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE_NP);
#	endif
	pthread_mutex_t mutex;
	i32 result = pthread_mutex_init(&mutex, &mutex_attr);
	if (result != 0) {
		KERROR("Mutex creation failure!");
		return false;
	}

	// Save off the mutex handle.
	out_mutex->internal_data = platform_allocate(sizeof(pthread_mutex_t), false);
	*(pthread_mutex_t *)out_mutex->internal_data = mutex;

	return true;
}

void kmutex_destroy (kmutex *mutex) {
	if (mutex) {
		i32 result = pthread_mutex_destroy((pthread_mutex_t *)mutex->internal_data);
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

		platform_free(mutex->internal_data, false, sizeof(pthread_mutex_t));
		mutex->internal_data = 0;
	}
}

b8 kmutex_lock (kmutex *mutex) {
	if (!mutex) {
		return false;
	}
	// Lock
	i32 result = pthread_mutex_lock((pthread_mutex_t *)mutex->internal_data);
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

b8 kmutex_unlock (kmutex *mutex) {
	if (!mutex) {
		return false;
	}
	if (mutex->internal_data) {
		i32 result = pthread_mutex_unlock((pthread_mutex_t *)mutex->internal_data);
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

b8 ksemaphore_create (ksemaphore *out_semaphore, u32 max_count, u32 start_count) {
	if (!out_semaphore || max_count == 0) {
		return false;
	}

	// Allocated from the host, not kallocate: futex-backed pthread objects need natural
	// alignment, and kallocate blocks are not guaranteed to be aligned.
	nix_semaphore_internal *internal = platform_allocate(sizeof(nix_semaphore_internal), false);
	if (pthread_mutex_init(&internal->mutex, 0) != 0) {
		KERROR("Failed to create semaphore mutex.");
		platform_free(internal, false, sizeof(nix_semaphore_internal));
		return false;
	}
	if (pthread_cond_init(&internal->condition, 0) != 0) {
		KERROR("Failed to create semaphore condition variable.");
		pthread_mutex_destroy(&internal->mutex);
		platform_free(internal, false, sizeof(nix_semaphore_internal));
		return false;
	}
	internal->max_count = max_count;
	internal->count = KMIN(start_count, max_count);

	out_semaphore->internal_data = internal;
	return true;
}

void ksemaphore_destroy (ksemaphore *semaphore) {
	if (!semaphore || !semaphore->internal_data) {
		return;
	}

	nix_semaphore_internal *internal = semaphore->internal_data;
	pthread_cond_destroy(&internal->condition);
	pthread_mutex_destroy(&internal->mutex);
	platform_free(internal, false, sizeof(nix_semaphore_internal));
	semaphore->internal_data = 0;
}

b8 ksemaphore_signal (ksemaphore *semaphore) {
	if (!semaphore || !semaphore->internal_data) {
		return false;
	}

	nix_semaphore_internal *internal = semaphore->internal_data;
	pthread_mutex_lock(&internal->mutex);
	if (internal->count < internal->max_count) {
		internal->count++;
	}
	pthread_cond_signal(&internal->condition);
	pthread_mutex_unlock(&internal->mutex);

	return true;
}

/**
 * Decreases the semaphore count by 1. If the count reaches 0, the
 * semaphore is considered unsignaled and this call blocks until the
 * semaphore is signaled by ksemaphore_signal, or until timeout_ms passes.
 * A timeout of 0xFFFFFFFF or more waits forever, matching INFINITE on Windows.
 */
b8 ksemaphore_wait (ksemaphore *semaphore, u64 timeout_ms) {
	if (!semaphore || !semaphore->internal_data) {
		return false;
	}

	nix_semaphore_internal *internal = semaphore->internal_data;
	b8 wait_forever = timeout_ms >= 0xFFFFFFFF;

	struct timespec deadline = {0};
	if (!wait_forever) {
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += (time_t)(timeout_ms / 1000);
		deadline.tv_nsec += (long)((timeout_ms % 1000) * 1000000);
		if (deadline.tv_nsec >= 1000000000L) {
			deadline.tv_sec += 1;
			deadline.tv_nsec -= 1000000000L;
		}
	}

	pthread_mutex_lock(&internal->mutex);
	while (internal->count == 0) {
		if (wait_forever) {
			pthread_cond_wait(&internal->condition, &internal->mutex);
		} else if (pthread_cond_timedwait(&internal->condition, &internal->mutex, &deadline) == ETIMEDOUT) {
			pthread_mutex_unlock(&internal->mutex);
			return false;
		}
	}
	internal->count--;
	pthread_mutex_unlock(&internal->mutex);

	return true;
}

b8 platform_dynamic_library_load (const char *name, dynamic_library *out_library) {
	if (!out_library) {
		return false;
	}
	kzero_memory(out_library, sizeof(dynamic_library));
	if (!name) {
		return false;
	}

	const char *extension = platform_dynamic_library_extension();
	const char *prefix = platform_dynamic_library_prefix();

	const char *filename = string_format("%s%s%s", prefix, name, extension);
	/* KTRACE("Trying local path '%s' first.", filename); */

	const char *formats[10] = {
		"./%s",
		"/lib/%s",
		"/usr/local/lib/%s",
		"/usr/lib64/%s",
		"/lib/x86_64-linux-gnu/%s",
		"./%s.1",
		"/lib/%s.1",
		"/usr/local/lib/%s.1",
		"/usr/lib64/%s.1",
		"/lib/x86_64-linux-gnu/%s.1",
	};

	void *library = KNULL;
	const char *cur_path = KNULL;
	for (u8 i = 0; i < 10; ++i) {
		string_free(cur_path);
		cur_path = string_format(formats[i], filename);
		library = dlopen(cur_path, RTLD_NOW);
		if (!library) {
			/* KTRACE("Dynamic lib path '%s' failed to load because of error: '%s'", cur_path, dlerror()); */
		} else {
			break;
		}
	}
	if (!library) {
		KERROR("Error opening library: %s", dlerror());
		return false;
	}

	/* KTRACE("Found lib: '%s'", cur_path); */
	string_free(cur_path);

	out_library->name = string_duplicate(name);
	out_library->filename = filename;

	out_library->internal_data_size = 8;
	out_library->internal_data = library;

	out_library->functions = darray_create(dynamic_library_function);

	return true;
}

b8 platform_dynamic_library_unload (dynamic_library *library) {
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
	library->internal_data = KNULL;

	string_free(library->name);
	string_free(library->filename);

	if (library->functions) {
		u32 count = darray_length(library->functions);
		for (u32 i = 0; i < count; ++i) {
			dynamic_library_function *f = &library->functions[i];
			string_free(f->name);
		}

		darray_destroy(library->functions);
		library->functions = KNULL;
	}

	kzero_memory(library, sizeof(dynamic_library));

	return true;
}

void *platform_dynamic_library_load_function (const char *name, dynamic_library *library) {
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
	darray_push(library->functions, &f);

	return f_addr;
}

#endif
