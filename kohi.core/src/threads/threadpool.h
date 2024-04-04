#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "defines.h"

struct worker_thread;

typedef struct threadpool {
    u32 thread_count;
    struct worker_thread* threads;
} threadpool;

KAPI b8 threadpool_create(u32 thread_count, threadpool* out_pool);

KAPI void threadpool_destroy(threadpool* pool);

KAPI b8 threadpool_wait(threadpool* pool);

#endif
