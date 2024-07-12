#pragma once

#include "defines.h"

/**
 * @brief Represents a ring queue of a particular size. Does not resize dynamically.
 * Naturally, this is a first in, first out structure.
 */
typedef struct ring_queue {
    /** @brief The current number of elements contained. */
    u32 length;
    /** @brief The size of each element in bytes. */
    u32 stride;
    /** @brief The total number of elements available. */
    u32 capacity;
    /** @brief The block of memory to hold the data. */
    void* block;
    /** @brief Indicates if the queue owns its memory block. */
    b8 owns_memory;
    /** @brief The index of the head of the list. */
    i32 head;
    /** @brief The index of the tail of the list. */
    i32 tail;
} ring_queue;

/**
 * @brief Creates a new ring queue of the given capacity and stride.
 *
 * @param stride The size of each element in bytes.
 * @param capacity The total number of elements to be available in the queue.
 * @param memory The memory block used to hold the data. Should be the size of
 * stride * capacity. If 0 is passed, a block is automatically allocated and
 * freed upon creation/destruction.
 * @param out_queue A pointer to hold the newly created queue.
 * @returns True on success; otherwise false.
 */
KAPI b8 ring_queue_create(u32 stride, u32 capacity, void* memory, ring_queue* out_queue);

/**
 * @brief Destroys the given queue. If memory was not passed in during creation,
 * it is freed here.
 *
 * @param queue A pointer to the queue to destroy.
 */
KAPI void ring_queue_destroy(ring_queue* queue);

/**
 * @brief Adds value to queue, if space is available.
 *
 * @param queue A pointer to the queue to add data to.
 * @param value The value to be added.
 * @return True if success; otherwise false.
 */
KAPI b8 ring_queue_enqueue(ring_queue* queue, void* value);

/**
 * @brief Attempts to retrieve the next value from the provided queue.
 *
 * @param queue A pointer to the queue to retrieve data from.
 * @param out_value A pointer to hold the retrieved value.
 * @return True if success; otherwise false.
 */
KAPI b8 ring_queue_dequeue(ring_queue* queue, void* out_value);

/**
 * @brief Attempts to retrieve, but not remove, the next value in the queue, if not empty.
 *
 * @param queue A constant pointer to the queue to retrieve data from.
 * @param out_value A pointer to hold the retrieved value.
 * @return True if success; otherwise false.
 */
KAPI b8 ring_queue_peek(const ring_queue* queue, void* out_value);
