#pragma once

#include "defines.h"

/**
 * Represents a process thread in the system to be used for work.
 * Generally should not be created directly in user code.
 * This calls to the platform-specific thread implementation.
 */
typedef struct kthread {
    void *internal_data;
    u64 thread_id;
} kthread;

// A function pointer to be invoked when the thread starts.
typedef u32 (*pfn_thread_start)(void *);

/**
 * Creates a new thread, immediately calling the function pointed to.
 * @param start_function_ptr The pointer to the function to be invoked immediately. Required.
 * @param params A pointer to any data to be passed to the start_function_ptr. Optional. Pass 0/NULL if not used.
 * @param auto_detach Indicates if the thread should immediately release its resources when the work is complete. If true, out_thread is not set.
 * @param out_thread A pointer to hold the created thread, if auto_detach is false.
 * @returns true if successfully created; otherwise false.
 */
b8 kthread_create(pfn_thread_start start_function_ptr, void *params, b8 auto_detach, kthread *out_thread);

/**
 * Destroys the given thread.
 */
void kthread_destroy(kthread *thread);

/**
 * Detaches the thread, automatically releasing resources when work is complete.
 */
void kthread_detach(kthread *thread);

/**
 * Cancels work on the thread, if possible, and releases resources when possible.
 */
void kthread_cancel(kthread *thread);

/**
 * Indicates if the thread is currently active.
 * @returns True if active; otherwise false.
 */
b8 kthread_is_active(kthread* thread);

/**
 * Sleeps on the given thread for a given number of milliseconds. Should be called from the
 * thread requiring the sleep.
 */
void kthread_sleep(kthread* thread, u64 ms);

u64 get_thread_id();
