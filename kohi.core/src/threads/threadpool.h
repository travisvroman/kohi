/**
 * @file threadpool.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Implementation of a thread pool.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "defines.h"

struct worker_thread;

typedef struct threadpool {
	u32 thread_count;
	struct worker_thread *threads;
} threadpool;

KAPI b8 threadpool_create (u32 thread_count, threadpool *out_pool);

KAPI void threadpool_destroy (threadpool *pool);

KAPI b8 threadpool_wait (threadpool *pool);

#endif
