#include "job_system.h"

#include "containers/ring_queue.h"
#include "core/asserts.h"
#include "core/frame_data.h"
#include "core/kmemory.h"
#include "core/kmutex.h"
#include "core/ksemaphore.h"
#include "core/kthread.h"
#include "core/logger.h"
#include "defines.h"

typedef struct job_thread {
    u8 index;
    kthread thread;
    job_info info;
    // A mutex to guard access to this thread's info.
    kmutex info_mutex;

    // Used to cause a thread to block until work is available.
    ksemaphore semaphore;

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

    u16 current_job_id;
    // TODO: This is a massive waste of memory - combine 8 at a time into each bool.
    b8* job_statuses;
    kmutex job_status_mutex;

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

static void store_result(pfn_job_on_complete callback, u32 param_size, void* params) {
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

static u32 job_thread_run(void* params) {
    u32 index = *(u32*)params;
    job_thread* thread = &state_ptr->job_threads[index];
    KTRACE("Starting job thread #%i (id=%#x, type=%#x).", thread->index, thread->thread.thread_id, thread->type_mask);

    // A mutex to lock info for this thread.
    if (!kmutex_create(&thread->info_mutex)) {
        KERROR("Failed to create job thread mutex! Aborting thread.");
        return 0;
    }

    // Create a semaphore for the thread which will block until there is work to do.
    if (!ksemaphore_create(&thread->semaphore, 1, 1)) {
        KERROR("Failed to create job thread semaphore! Aborting thread.");
        return 0;
    }

    // Run forever, waiting for jobs.
    while (true) {
        if (!state_ptr || !state_ptr->running || !thread) {
            break;
        }

        // Wait for the semaphore to be signaled.
        ksemaphore_wait(&thread->semaphore, 0xFFFFFFFF);

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

            // Update the job status for this job.
            if (!kmutex_lock(&state_ptr->job_status_mutex)) {
                KERROR("Failed to lock job status mutex!");
            }
            state_ptr->job_statuses[thread->info.id] = true;
            if (!kmutex_unlock(&state_ptr->job_status_mutex)) {
                KERROR("Failed to unlock job status mutex!");
            }

            // Lock and reset the thread's info object
            if (!kmutex_lock(&thread->info_mutex)) {
                KERROR("Failed to obtain lock on job thread mutex!");
            }
            if (thread->info.dependency_ids) {
                kfree(thread->info.dependency_ids, sizeof(u16) * thread->info.dependency_count, MEMORY_TAG_ARRAY);
            }
            kzero_memory(&thread->info, sizeof(job_info));
            if (!kmutex_unlock(&thread->info_mutex)) {
                KERROR("Failed to release lock on job thread mutex!");
            }
        }

        // If no longer running, shut down the thread.
        if (!state_ptr->running) {
            break;
        }
    }

    // Destroy the mutex for this thread.
    kmutex_destroy(&thread->info_mutex);

    // Destroy the semaphore.
    ksemaphore_destroy(&thread->semaphore);

    return 1;
}

b8 job_system_initialize(u64* job_system_memory_requirement, void* state, void* config) {
    job_system_config* typed_config = (job_system_config*)config;
    *job_system_memory_requirement = sizeof(job_system_state) + (sizeof(u8) * (INVALID_ID_U16 - 1));
    if (state == 0) {
        return true;
    }

    kzero_memory(state, sizeof(job_system_state));

    state_ptr = state;
    state_ptr->running = true;
    state_ptr->job_statuses = (void*)((u64)state_ptr + sizeof(job_system_state));

    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->low_priority_queue);
    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->normal_priority_queue);
    ring_queue_create(sizeof(job_info), 1024, 0, &state_ptr->high_priority_queue);
    state_ptr->thread_count = typed_config->max_job_thread_count;

    // Invalidate all result slots
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        state_ptr->pending_results[i].id = INVALID_ID_U16;
    }

    KDEBUG("Main thread id is: %#x", platform_current_thread_id());

    KDEBUG("Spawning %i job threads.", state_ptr->thread_count);

    for (u8 i = 0; i < state_ptr->thread_count; ++i) {
        state_ptr->job_threads[i].index = i;
        state_ptr->job_threads[i].type_mask = typed_config->type_masks[i];
        if (!kthread_create(job_thread_run, &state_ptr->job_threads[i].index, false, &state_ptr->job_threads[i].thread)) {
            KFATAL("OS Error in creating job thread. Application cannot continue.");
            return false;
        }
        kzero_memory(&state_ptr->job_threads[i].info, sizeof(job_info));
    }

    // Create needed mutexes
    if (!kmutex_create(&state_ptr->result_mutex)) {
        KERROR("Failed to create result mutex!");
        return false;
    }
    if (!kmutex_create(&state_ptr->low_pri_queue_mutex)) {
        KERROR("Failed to create low priority queue mutex!");
        return false;
    }
    if (!kmutex_create(&state_ptr->normal_pri_queue_mutex)) {
        KERROR("Failed to create normal priority queue mutex!");
        return false;
    }
    if (!kmutex_create(&state_ptr->high_pri_queue_mutex)) {
        KERROR("Failed to create high priority queue mutex!");
        return false;
    }
    if (!kmutex_create(&state_ptr->job_status_mutex)) {
        KERROR("Failed to create job status mutex!");
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
        kmutex_destroy(&state_ptr->job_status_mutex);

        state_ptr = 0;
    }
}

static void process_queue(ring_queue* queue, kmutex* queue_mutex) {
    u64 thread_count = state_ptr->thread_count;

    // Check for a free thread first.
    while (queue->length > 0) {
        job_info info;
        if (!ring_queue_peek(queue, &info)) {
            break;
        }

        // Verify dependencies are complete.
        b8 awaiting_dependency = false;
        if (info.dependency_count) {
            for (u32 i = 0; i < info.dependency_count; ++i) {
                if (!job_system_query_job_complete(info.dependency_ids[i])) {
                    KTRACE("Note: Not starting job id %u because it's dependency (job id=%u) is still running.", info.id, info.dependency_ids[i]);
                    awaiting_dependency = true;
                    break;
                }
            }
        }

        if (awaiting_dependency) {
            continue;
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
                // Signal the thread's semaphore since there is work to be done.
                ksemaphore_signal(&thread->semaphore);
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

b8 job_system_update(void* state, struct frame_data* p_frame_data) {
    if (!state_ptr || !state_ptr->running) {
        return false;
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

    return true;
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
    return job_create_with_dependencies(entry_point, on_success, on_fail, param_data, param_data_size, result_data_size, type, priority, 0, 0);
}

job_info job_create_with_dependencies(
    pfn_job_start entry_point,
    pfn_job_on_complete on_success,
    pfn_job_on_complete on_fail,
    void* param_data,
    u32 param_data_size,
    u32 result_data_size,
    job_type type,
    job_priority priority,
    u8 dependency_count,
    u16* dependencies) {
    job_info job;
    job.entry_point = entry_point;
    job.on_success = on_success;
    job.on_fail = on_fail;
    job.type = type;
    job.priority = priority;

    // Technically jobs can be created in the middle of other jobs (i.e. on a different thread)
    // so make sure to lock around these updates.
    if (!kmutex_lock(&state_ptr->job_status_mutex)) {
        KERROR("Failed to lock job status mutex!");
    }

    // TODO: Pack booleans.
    KASSERT_MSG(state_ptr->current_job_id < INVALID_ID_U16, "Job system identifier overflow - need to pack booleans.");
    job.id = state_ptr->current_job_id;
    state_ptr->current_job_id++;
    state_ptr->job_statuses[job.id] = false;
    if (!kmutex_unlock(&state_ptr->job_status_mutex)) {
        KERROR("Failed to unlock job status mutex!");
    }

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

    job.dependency_count = dependency_count;
    if (dependency_count) {
        job.dependency_ids = kallocate(sizeof(u16) * dependency_count, MEMORY_TAG_ARRAY);
        kcopy_memory(job.dependency_ids, dependencies, sizeof(u16) * dependency_count);
    } else {
        job.dependency_ids = 0;
    }

    return job;
}

b8 job_system_query_job_complete(u16 job_id) {
    b8 status = INVALID_ID;
    if (!kmutex_lock(&state_ptr->job_status_mutex)) {
        KERROR("Failed to lock job status mutex!");
    }
    status = state_ptr->job_statuses[job_id];
    if (!kmutex_unlock(&state_ptr->job_status_mutex)) {
        KERROR("Failed to unlock job status mutex!");
    }
    return status;
}

b8 job_system_wait_for_jobs(u8 job_count, u16 job_ids) {
    return true;
}
