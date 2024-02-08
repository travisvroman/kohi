
/**
 * @file queue.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A simple queue container. Elements are popped off the queue in the
 * same order they were pushed to it.
 * @version 1.0
 * @date 2024-02-04
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */
#pragma once

#include "defines.h"

/**
 * @brief A simple queue container. Elements are popped off the queue in the
 * same order they were pushed to it.
 */
typedef struct queue {
    /** @brief The element size in bytes.*/
    u32 element_size;
    /** @brief The current element count. */
    u32 element_count;
    /** @brief The total amount of currently-allocated memory.*/
    u32 allocated;
    /** @brief The allocated memory block. */
    void* memory;
} queue;

/**
 * @brief Creates a new queue.
 *
 * @param out_queue A pointer to hold the newly-created queue.
 * @param element_size The size of each element in the queue.
 * @return True on success; otherwise false.
 */
KAPI b8 queue_create(queue* out_queue, u32 element_size);
/**
 * @brief Destroys the given queue.
 *
 * @param s A pointer to the queue to be destroyed.
 */
KAPI void queue_destroy(queue* s);

/**
 * @brief Pushes an element (a copy of the element data) into the back of the queue.
 *
 * @param s A pointer to the queue to push to.
 * @param element_data The element data to be pushed. Required.
 * @return True on succcess; otherwise false.
 */
KAPI b8 queue_push(queue* s, void* element_data);

/**
 * @brief Attempts to peek an element (writing out a copy of the
 * element data on success) from the queue. If the queue is empty,
 * nothing is done and false is returned. The queue memory is not modified.
 *
 * @param s A pointer to the queue to peek from.
 * @param element_data A pointer to write the element data to. Required.
 * @return True on succcess; otherwise false.
 */
KAPI b8 queue_peek(const queue* s, void* out_element_data);

/**
 * @brief Attempts to pop an element (writing out a copy of the
 * element data on success) from the front of the queue. If the queue is empty,
 * nothing is done and false is returned.
 *
 * @param s A pointer to the queue to pop from.
 * @param element_data A pointer to write the element data to. Required.
 * @return True on succcess; otherwise false.
 */
KAPI b8 queue_pop(queue* s, void* out_element_data);
