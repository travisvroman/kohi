#include "job_system.h"

#include "core/kthread.h"
#include "core/kmutex.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "containers/ring_queue.h"

typedef struct job_thread {
    u8 index;
    kthread thread;
    job_info info;
    // A mutex to guard access to this thread's info.
    kmutex info_mutex;

    // The types of jobs this thread can handle.
    u32 type_mask;
} job_thread;

typedef struct job_result_entry {
    u16 id;
    pfn_job_on_complete callback;
    u32 param_size;
    void* params;
} job_result_entry;

// The max number of job results that can be stored at once.
#define MAX_JOB_RESULTS 512

typedef struct job_system_state {
    b8 running;
    u8 thread_count;
    job_thread job_threads[32];

    ring_queue low_priority_queue;
    ring_queue normal_priority_queue;
    ring_queue high_priority_queue;

    // Mutexes for each queue, since a job could be kicked off from another job (thread).
    kmutex low_pri_queue_mutex;
    kmutex normal_pri_queue_mutex;
    kmutex high_pri_queue_mutex;

    job_result_entry pending_results[MAX_JOB_RESULTS];
    kmutex result_mutex;
    // A mutex for the result array
} job_system_state;

static job_system_state* state_ptr;

void store_result(pfn_job_on_complete callback, u32 param_size, void* params) {
    // Create the new entry.
    job_result_entry entry;
    entry.id = INVALID_ID_U16;
    entry.param_size = param_size;
    entry.callback = callback;
    if (entry.param_size > 0) {
        // Take a copy, as the job is destroyed after this.
        entry.params = kallocate(param_size, MEMORY_TAG_JOB);
        kcopy_memory(entry.params, params, param_size);
    } else {
        entry.params = 0;
    }

    // Lock, find a free space, store, unlock.
    if (!kmutex_lock(&state_ptr->result_mutex)) {
        KERROR("Failed to obtain mutex lock for storing a result! Result storage may be corrupted.");
    }
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        if (state_ptr->pending_results[i].id == INVALID_ID_U16) {
            state_ptr->pending_results[i] = entry;
            state_ptr->pending_results[i].id = i;
            break;
        }
    }
    if (!kmutex_unlock(&state_ptr->result_mutex)) {
        KERROR("Failed to release mutex lock for result storage, storage may be corrupted.");
    }
}

u32 job_thread_run(void* params) {
    u32 index = *(u32*)params;
    job_thread* thread = &state_ptr->job_threads[index];
    u64 thread_id = thread->thread.thread_id;
    KTRACE("Starting job thread #%i (id=%#x, type=%#x).", thread->index, thread_id, thread->type_mask);

    // A mutex to lock info for this thread.
    if (!kmutex_create(&thread->info_mutex)) {
        KERROR("Failed to create job thread mutex! Aborting thread.");
        return 0;
    }

    // Run forever, waiting for jobs.
    while (true) {
        if (!state_ptr || !state_ptr->running || !thread) {
            break;
        }

        // Lock and grab a copy of the info
        if (!kmutex_lock(&thread->info_mutex)) {
            KERROR("Failed to obtain lock on job thread mutex!");
        }
        job_info info = thread->info;
        if (!kmutex_unlock(&thread->info_mutex)) {
            KERROR("Failed to release lock on job thread mutex!");
        }

        if (info.entry_point) {
            b8 result = info.entry_point(info.param_data, info.result_data);

            // Store the result to be executed on the main thread later.
            // Note that store_result takes a copy of the result_data
            // so it does not have to be held onto by this thread any longer.
            if (result && info.on_success) {
                store_result(info.on_success, info.result_data_size, info.result_data);
            } else if (!result && info.on_fail) {
                store_result(info.on_fail, info.result_data_size, info.result_data);
            }

            // Clear the param data and result data.
            if (info.param_data) {
                kfree(info.param_data, info.param_data_size, MEMORY_TAG_JOB);
            }
            if (info.result_data) {
                kfree(info.result_data, info.result_data_size, MEMORY_TAG_JOB);
            }

            // Lock and reset the thread's info object
            if (!kmutex_lock(&thread->info_mutex)) {
                KERROR("Failed to obtain lock on job thread mutex!");
            }
            kzero_memory(&thread->info, sizeof(job_info));
            if (!kmutex_unlock(&thread->info_mutex)) {
                KERROR("Failed to release lock on job thread mutex!");
            }
        }

        if (state_ptr->running) {
            // TODO: Should probably find a better way to do this, such as sleeping until
            // a request comes through for a new job.
            kthread_sleep(&thread->thread, 10);
        } else {
            break;
        }
    }

    // Destroy the mutex for this thread.
    kmutex_destroy(&thread->info_mutex);
    return 1;
}

b8 job_system_initialize(u64* job_system_memory_requirement, void* state, u8 job_thread_count, u32 type_masks[]) {
    *job_system_memory_requirement = sizeof(job_system_state);
    if (state == 0) {
        return true;
    }

    kzero_memory(state, sizeof(job_system_state));

    state_ptr = state;
    state_ptr->running = true;

    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->low_priority_queue);
    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->normal_priority_queue);
    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->high_priority_queue);
    state_ptr->thread_count = job_thread_count;

    // Invalidate all result slots
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        state_ptr->pending_results[i].id = INVALID_ID_U16;
    }

    KDEBUG("Main thread id is: %#x", get_thread_id());

    KDEBUG("Spawning %i job threads.", state_ptr->thread_count);

    for (u8 i = 0; i < state_ptr->thread_count; ++i) {
        state_ptr->job_threads[i].index = i;
        state_ptr->job_threads[i].type_mask = type_masks[i];
        if (!kthread_create(job_thread_run, &state_ptr->job_threads[i].index, false, &state_ptr->job_threads[i].thread)) {
            KFATAL("OS Error in creating job thread. Application cannot continue.");
            return false;
        }
        kzero_memory(&state_ptr->job_threads[i].info, sizeof(job_info));
    }

    // Create needed mutexes
    if (!kmutex_create(&state_ptr->result_mutex)) {
        KERROR("Failed to create result mutex!.");
        return false;
    }
    if (!kmutex_create(&state_ptr->low_pri_queue_mutex)) {
        KERROR("Failed to create low priority queue mutex!.");
        return false;
    }
    if (!kmutex_create(&state_ptr->normal_pri_queue_mutex)) {
        KERROR("Failed to create normal priority queue mutex!.");
        return false;
    }
    if (!kmutex_create(&state_ptr->high_pri_queue_mutex)) {
        KERROR("Failed to create high priority queue mutex!.");
        return false;
    }

    return true;
}

void job_system_shutdown(void* state) {
    if (state_ptr) {
        state_ptr->running = false;

        u64 thread_count = state_ptr->thread_count;

        // Check for a free thread first.
        for (u8 i = 0; i < thread_count; ++i) {
            kthread_destroy(&state_ptr->job_threads[i].thread);
        }
        ring_queue_destroy(&state_ptr->low_priority_queue);
        ring_queue_destroy(&state_ptr->normal_priority_queue);
        ring_queue_destroy(&state_ptr->high_priority_queue);

        // Destroy mutexes
        kmutex_destroy(&state_ptr->result_mutex);
        kmutex_destroy(&state_ptr->low_pri_queue_mutex);
        kmutex_destroy(&state_ptr->normal_pri_queue_mutex);
        kmutex_destroy(&state_ptr->high_pri_queue_mutex);

        state_ptr = 0;
    }
}

void process_queue(ring_queue* queue, kmutex* queue_mutex) {
    u64 thread_count = state_ptr->thread_count;

    // Check for a free thread first.
    while (queue->length > 0) {
        job_info info;
        if (!ring_queue_peek(queue, &info)) {
            break;
        }

        b8 thread_found = false;
        for (u8 i = 0; i < thread_count; ++i) {
            job_thread* thread = &state_ptr->job_threads[i];
            if ((thread->type_mask & info.type) == 0) {
                continue;
            }

            // Check that the job thread can handle the job type.
            if (!kmutex_lock(&thread->info_mutex)) {
                KERROR("Failed to obtain lock on job thread mutex!");
            }
            if (!thread->info.entry_point) {
                // Make sure to remove the entry from the queue.
                if (!kmutex_lock(queue_mutex)) {
                    KERROR("Failed to obtain lock on queue mutex!");
                }
                ring_queue_dequeue(queue, &info);
                if (!kmutex_unlock(queue_mutex)) {
                    KERROR("Failed to release lock on queue mutex!");
                }
                thread->info = info;
                KTRACE("Assigning job to thread: %u", thread->index);
                thread_found = true;
            }
            if (!kmutex_unlock(&thread->info_mutex)) {
                KERROR("Failed to release lock on job thread mutex!");
            }

            // Break after unlocking if an available thread was found.
            if (thread_found) {
                break;
            }
        }

        // This means all of the threads are currently handling a job,
        // So wait until the next update and try again.
        if (!thread_found) {
            break;
        }
    }
}

void job_system_update() {
    if (!state_ptr || !state_ptr->running) {
        return;
    }

    process_queue(&state_ptr->high_priority_queue, &state_ptr->high_pri_queue_mutex);
    process_queue(&state_ptr->normal_priority_queue, &state_ptr->normal_pri_queue_mutex);
    process_queue(&state_ptr->low_priority_queue, &state_ptr->low_pri_queue_mutex);

    // Process pending results.
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        // Lock and take a copy, unlock.
        if (!kmutex_lock(&state_ptr->result_mutex)) {
            KERROR("Failed to obtain lock on result mutex!");
        }
        job_result_entry entry = state_ptr->pending_results[i];
        if (!kmutex_unlock(&state_ptr->result_mutex)) {
            KERROR("Failed to release lock on result mutex!");
        }

        if (entry.id != INVALID_ID_U16) {
            // Execute the callback.
            entry.callback(entry.params);

            if (entry.params) {
                kfree(entry.params, entry.param_size, MEMORY_TAG_JOB);
            }

            // Lock actual entry, invalidate and clear it
            if (!kmutex_lock(&state_ptr->result_mutex)) {
                KERROR("Failed to obtain lock on result mutex!");
            }
            kzero_memory(&state_ptr->pending_results[i], sizeof(job_result_entry));
            state_ptr->pending_results[i].id = INVALID_ID_U16;
            if (!kmutex_unlock(&state_ptr->result_mutex)) {
                KERROR("Failed to release lock on result mutex!");
            }
        }
    }
}

void job_system_submit(job_info info) {
    u64 thread_count = state_ptr->thread_count;
    ring_queue* queue = &state_ptr->normal_priority_queue;
    kmutex* queue_mutex = &state_ptr->normal_pri_queue_mutex;

    // If the job is high priority, try to kick it off immediately.
    if (info.priority == JOB_PRIORITY_HIGH) {
        queue = &state_ptr->high_priority_queue;
        queue_mutex = &state_ptr->high_pri_queue_mutex;

        // Check for a free thread that supports the job type first.
        for (u8 i = 0; i < thread_count; ++i) {
            job_thread* thread = &state_ptr->job_threads[i];
            if (state_ptr->job_threads[i].type_mask & info.type) {
                b8 found = false;
                if (!kmutex_lock(&thread->info_mutex)) {
                    KERROR("Failed to obtain lock on job thread mutex!");
                }
                if (!state_ptr->job_threads[i].info.entry_point) {
                    KTRACE("Job immediately submitted on thread %i", state_ptr->job_threads[i].index);
                    state_ptr->job_threads[i].info = info;
                    found = true;
                }
                if (!kmutex_unlock(&thread->info_mutex)) {
                    KERROR("Failed to release lock on job thread mutex!");
                }
                if (found) {
                    return;
                }
            }
        }
    }

    // If this point is reached, all threads are busy (if high) or it can wait a frame.
    // Add to the queue and try again next cycle.
    if (info.priority == JOB_PRIORITY_LOW) {
        queue = &state_ptr->low_priority_queue;
        queue_mutex = &state_ptr->low_pri_queue_mutex;
    }

    // NOTE: Locking here in case the job is submitted from another job/thread.
    if (!kmutex_lock(queue_mutex)) {
        KERROR("Failed to obtain lock on queue mutex!");
    }
    ring_queue_enqueue(queue, &info);
    if (!kmutex_unlock(queue_mutex)) {
        KERROR("Failed to release lock on queue mutex!");
    }
    KTRACE("Job queued.");
}

job_info job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size) {
    return job_create_priority(entry_point, on_success, on_fail, param_data, param_data_size, result_data_size, JOB_TYPE_GENERAL, JOB_PRIORITY_NORMAL);
}

job_info job_create_type(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type) {
    return job_create_priority(entry_point, on_success, on_fail, param_data, param_data_size, result_data_size, type, JOB_PRIORITY_NORMAL);
}

job_info job_create_priority(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type, job_priority priority) {
    job_info job;
    job.entry_point = entry_point;
    job.on_success = on_success;
    job.on_fail = on_fail;
    job.type = type;
    job.priority = priority;

    job.param_data_size = param_data_size;
    if (param_data_size) {
        job.param_data = kallocate(param_data_size, MEMORY_TAG_JOB);
        kcopy_memory(job.param_data, param_data, param_data_size);
    } else {
        job.param_data = 0;
    }

    job.result_data_size = result_data_size;
    if (result_data_size) {
        job.result_data = kallocate(result_data_size, MEMORY_TAG_JOB);
    } else {
        job.result_data = 0;
    }

    return job;
}
