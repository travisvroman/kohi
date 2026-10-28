/**
 * @file worker_thread.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Implementation of a worker thread.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "containers/queue.h"
#include "defines.h"
#include "threads/kmutex.h"
#include "threads/ksemaphore.h"
#include "threads/kthread.h"

typedef struct worker_thread {
	kthread thread;
	queue work_queue;
	kmutex queue_mutex;
} worker_thread;

KAPI b8 worker_thread_create (worker_thread *out_thread);

KAPI void worker_thread_destroy (worker_thread *thread);

KAPI b8 worker_thread_add (worker_thread *thread, pfn_thread_start work_fn, void *params);

KAPI b8 worker_thread_start (worker_thread *thread);

KAPI b8 worker_thread_wait (worker_thread *thread);
