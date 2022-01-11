/**
 * @file
 * @brief This file contains the structures and functions of the memory system.
 * This is responsible for memory interaction with the platform layer, such as
 * allocations/frees and tagging of memory allocations.
 * 
 * Note that reliance on this will likely be by core systems only, as items using
 * allocations directly will use allocators as they are added to the system.
 */

#pragma once

#include "defines.h"

/** @brief Tags to indicate the usage of memory allocations made in this system. */
typedef enum memory_tag {
    // For temporary use. Should be assigned one of the below or have a new tag created.
    MEMORY_TAG_UNKNOWN,
    MEMORY_TAG_ARRAY,
    MEMORY_TAG_LINEAR_ALLOCATOR,
    MEMORY_TAG_DARRAY,
    MEMORY_TAG_DICT,
    MEMORY_TAG_RING_QUEUE,
    MEMORY_TAG_BST,
    MEMORY_TAG_STRING,
    MEMORY_TAG_APPLICATION,
    MEMORY_TAG_JOB,
    MEMORY_TAG_TEXTURE,
    MEMORY_TAG_MATERIAL_INSTANCE,
    MEMORY_TAG_RENDERER,
    MEMORY_TAG_GAME,
    MEMORY_TAG_TRANSFORM,
    MEMORY_TAG_ENTITY,
    MEMORY_TAG_ENTITY_NODE,
    MEMORY_TAG_SCENE,

    MEMORY_TAG_MAX_TAGS
} memory_tag;

/**
 * @brief Initializes the memory system. Should be called twice, once to obtain
 * the memory requirement (passing state=0), and a second time with state recieving 
 * an allocated block of memory.
 * @param memory_requirement A pointer to hold the memory requirement in bytes, used for allocation.
 * @param state A block of memory to hold the state for this system. Can be 0 when just obtaining requirement. 
 */
KAPI void memory_system_initialize(u64* memory_requirement, void* state);

/**
 * @brief Shuts down the memory system.
 * @param state A pointer to the state block of memory used by this system.
 */
KAPI void memory_system_shutdown(void* state);

/**
 * @brief Performs a memory allocation from the host of the given size. The allocation
 * is tracked for the provided tag.
 * @param size The size of the allocation.
 * @param tag Indicates the use of the allocated block.
 * @returns If successful, a pointer to a block of allocated memory; otherwise 0.
 */
KAPI void* kallocate(u64 size, memory_tag tag);

/**
 * @brief Frees the given block, and untracks its size from the given tag.
 * @param block A pointer to the block of memory to be freed.
 * @param size The size of the block to be freed.
 * @param tag The tag indicating the block's use.
 */
KAPI void kfree(void* block, u64 size, memory_tag tag);

/**
 * @brief Zeroes out the provided memory block.
 * @param block A pointer to the block of memory to be zeroed out.
 * @param size The size in bytes to zero out.
 * @param A pointer to the zeroed out block of memory.
 */
KAPI void* kzero_memory(void* block, u64 size);

/**
 * @brief Performs a copy of the memory at source to dest of the given size.
 * @param dest A pointer to the destination block of memory to copy to.
 * @param source A pointer to the source block of memory to copy from.
 * @param size The amount of memory in bytes to be copied over.
 * @returns A pointer to the block of memory copied to.
 */
KAPI void* kcopy_memory(void* dest, const void* source, u64 size);

/**
 * @brief Sets the bytes of memory located at dest to value over the given size.
 * @param dest A pointer to the destination block of memory to be set.
 * @param value The value to be set.
 * @param size The size in bytes to copy over to.
 * @returns A pointer to the destination block of memory.
 */
KAPI void* kset_memory(void* dest, i32 value, u64 size);

/**
 * @brief Obtains a string containing a "printout" of memory usage, categorized by
 * memory tag. The memory should be freed by the caller.
 * @deprecated This function should be discontinued in favour of something more robust in the future.
 * @returns A pointer to a character array containing the string representation of memory usage.
 */
KAPI char* get_memory_usage_str();

/**
 * @brief Obtains the number of times kallocate was called since the memory system was initialized.
 * @returns The total count of allocations since the system's initialization.
 */
KAPI u64 get_memory_alloc_count();
