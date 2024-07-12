#include "threadpool.h"

#include "logger.h"
#include "memory/kmemory.h"
#include "worker_thread.h"

b8 threadpool_create(u32 thread_count, threadpool* out_pool) {
    if (!thread_count || !out_pool) {
        KERROR("threadpool_create requires at least 1 thread and a valid pointer to hold the created pool.");
        return false;
    }

    out_pool->thread_count = thread_count;
    out_pool->threads = kallocate(sizeof(worker_thread) * thread_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < thread_count; ++i) {
        if (!worker_thread_create(&out_pool->threads[i])) {
            KERROR("Error creating worker thread. threadpool_create failed.");
            return false;
        }
    }

    return true;
}

void threadpool_destroy(threadpool* pool) {
    if (pool) {
        if (pool->threads) {
            for (u32 i = 0; i < pool->thread_count; ++i) {
                worker_thread_destroy(&pool->threads[i]);
            }

            kfree(pool->threads, sizeof(worker_thread) * pool->thread_count, MEMORY_TAG_ARRAY);
            pool->threads = 0;
        }
        pool->thread_count = 0;
    }
}

b8 threadpool_wait(threadpool* pool) {
    if (!pool) {
        KERROR("threadpool_wait requires a valid pointer to a thread pool.");
        return false;
    }

    b8 success = true;
    for (u32 i = 0; i < pool->thread_count; ++i) {
        if (!worker_thread_wait(&pool->threads[i])) {
            KERROR("Failed to wait for worker thread in thread pool. See logs for details.");
            success = false;
        }
        KTRACE("Worker thread wait complete.");
    }

    if (!success) {
        KERROR("There was an error waiting for the threadpool. See logs for details.");
    }

    KTRACE("Done waiting on all threads");

    return success;
}
