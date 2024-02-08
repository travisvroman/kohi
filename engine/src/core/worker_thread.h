#pragma once

#include "containers/queue.h"
#include "core/kmutex.h"
#include "core/ksemaphore.h"
#include "core/kthread.h"
#include "defines.h"

typedef struct worker_thread {
    kthread thread;
    queue work_queue;
    kmutex queue_mutex;
} worker_thread;

KAPI b8 worker_thread_create(worker_thread* out_thread);

KAPI void worker_thread_destroy(worker_thread* thread);

KAPI b8 worker_thread_add(worker_thread* thread, pfn_thread_start work_fn, void* params);

KAPI b8 worker_thread_start(worker_thread* thread);

KAPI b8 worker_thread_wait(worker_thread* thread);
