/**
 * @file freelist.h
 * @author your name (you@domain.com)
 * @brief This file contains a free list, used for custom memory allocation tracking.
 * @version 0.1
 * @date 2022-01-12
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#pragma once

#include "defines.h"

/**
 * @brief A data structure to be used alongside an allocator for dynamic memory
 * allocation. Tracks free ranges of memory.
 */
typedef struct freelist {
    /** @breif The internal state of the freelist. */
    void* memory;
} freelist;

/**
 * @brief Creates a new freelist or obtains the memory requirement for one. Call
 * twice; once passing 0 to memory to obtain memory requirement, and a second
 * time passing an allocated block to memory.
 * 
 * @param total_size The total size in bytes that the free list should track.
 * @param memory_requirement A pointer to hold memory requirement for the free list itself.
 * @param memory 0, or a pre-allocated block of memory for the free list to use.
 * @param out_list A pointer to hold the created free list.
 */
KAPI void freelist_create(u32 total_size, u64* memory_requirement, void* memory, freelist* out_list);

/**
 * @brief Destroys the provided list.
 * 
 * @param list The list to be destroyed.
 */
KAPI void freelist_destroy(freelist* list);

/**
 * @brief Attempts to find a free block of memory of the given size.
 * 
 * @param list A pointer to the list to search.
 * @param size The size to allocate.
 * @param out_offset A pointer to hold the offset to the allocated memory.
 * @return b8 True if a block of memory was found and allocated; otherwise false.
 */
KAPI b8 freelist_allocate_block(freelist* list, u32 size, u32* out_offset);

/**
 * @brief Attempts to free a block of memory at the given offset, and of the given
 * size. Can fail if invalid data is passed.
 * 
 * @param list A pointer to the list to free from.
 * @param size The size to be freed.
 * @param offset The offset to free at.
 * @return b8 True if successful; otherwise false. False should be treated as an error.
 */
KAPI b8 freelist_free_block(freelist* list, u32 size, u32 offset);

/**
 * @brief Clears the free list.
 * 
 * @param list The list to be cleared.
 */
KAPI void freelist_clear(freelist* list);

/**
 * @brief Returns the amount of free space in this list. NOTE: Since this has
 * to iterate the entire internal list, this can be an expensive operation.
 * Use sparingly.
 * 
 * @param list A pointer to the list to obtain from.
 * @return The amount of free space in bytes.
 */
KAPI u64 freelist_free_space(freelist* list);
