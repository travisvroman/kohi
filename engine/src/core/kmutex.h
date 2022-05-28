#pragma once

#include "defines.h"

/**
 * A mutex to be used for synchronization purposes. A mutex (or
 * mutual exclusion) is used to limit access to a resource when
 * there are multiple threads of execution around that resource.
 */
typedef struct kmutex {
    void *internal_data;
} kmutex;

/**
 * Creates a mutex.
 * @param out_mutex A pointer to hold the created mutex.
 * @returns True if created successfully; otherwise false.
 */
b8 kmutex_create(kmutex* out_mutex);

/**
 * @brief Destroys the provided mutex.
 * 
 * @param mutex A pointer to the mutex to be destroyed.
 */
void kmutex_destroy(kmutex* mutex);

/**
 * Creates a mutex lock.
 * @param mutex A pointer to the mutex.
 * @returns True if locked successfully; otherwise false.
 */
b8 kmutex_lock(kmutex *mutex);

/**
 * Unlocks the given mutex.
 * @param mutex The mutex to unlock.
 * @returns True if unlocked successfully; otherwise false.
 */
b8 kmutex_unlock(kmutex *mutex);
