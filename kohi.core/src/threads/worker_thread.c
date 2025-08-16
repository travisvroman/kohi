#include "worker_thread.h"

#include "containers/queue.h"
#include "memory/kmemory.h"
#include "threads/kmutex.h"
#include "threads/kthread.h"
#include "logger.h"

typedef struct work {
    pfn_thread_start work_fn;
    void* params;
} work;

static u32 worker_thread_loop(void* params) {
    worker_thread* thread = params;

    while (true) {
        kmutex_lock(&thread->queue_mutex);
        if (thread->work_queue.element_count == 0) {
            kmutex_unlock(&thread->queue_mutex);
            break;
        }
        work w;
        if (!queue_pop(&thread->work_queue, &w)) {
            KERROR("Failed to pop work from work queue.");
            kmutex_unlock(&thread->queue_mutex);
            return 0;
        }
        kmutex_unlock(&thread->queue_mutex);
        w.work_fn(w.params);
    }

    KTRACE("Worker thread work complete.");

    return 1;
}

b8 worker_thread_create(worker_thread* out_thread) {
    if (!out_thread) {
        return false;
    }

    if (!queue_create(&out_thread->work_queue, sizeof(work))) {
        KERROR("Failed to create internal work queue for worker thread.");
        return false;
    }
    if (!kmutex_create(&out_thread->queue_mutex)) {
        KERROR("Failed to create internal work queue mutex for worker thread.");
        return false;
    }

    return true;
}

void worker_thread_destroy(worker_thread* thread) {
    if (!thread) {
        return;
    }

    queue_destroy(&thread->work_queue);
    kmutex_destroy(&thread->queue_mutex);
    kthread_destroy(&thread->thread);
    kzero_memory(thread, sizeof(worker_thread));
}

b8 worker_thread_add(worker_thread* thread, pfn_thread_start work_fn, void* work_params) {
    if (!thread || !work_fn) {
        KERROR("worker_thread_add requires valid pointers to a worker_thread and a work function pointer.");
        return false;
    }

    kmutex_lock(&thread->queue_mutex);
    work w;
    w.work_fn = work_fn;
    w.params = work_params;
    if (!queue_push(&thread->work_queue, &w)) {
        KERROR("Failed to push work into queue.");
        kmutex_unlock(&thread->queue_mutex);
        return false;
    }
    kmutex_unlock(&thread->queue_mutex);

    return true;
}

b8 worker_thread_start(worker_thread* thread) {
    if (!thread) {
        return false;
    }

    // Create the internal thread if need be.
    if (!kthread_create(worker_thread_loop, "KohiWorker", thread, false, &thread->thread)) {
        KERROR("Worker thread internal thread creation failed.");
        return false;
    }

    return true;
}

b8 worker_thread_wait(worker_thread* thread) {
    return kthread_wait(&thread->thread);
}
