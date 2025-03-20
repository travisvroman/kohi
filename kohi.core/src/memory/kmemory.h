/**
 * @file kmemory.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the structures and functions of the memory system.
 * This is responsible for memory interaction with the platform layer, such as
 * allocations/frees and tagging of memory allocations.
 * @note Note that reliance on this will likely be by core systems only, as items using
 * allocations directly will use allocators as they are added to the system.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"

// Interface for a frame allocator.
typedef struct frame_allocator_int {
    void* (*allocate)(u64 size);
    void (*free)(void* block, u64 size);
    void (*free_all)(void);
} frame_allocator_int;

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
    MEMORY_TAG_ENGINE,
    MEMORY_TAG_JOB,
    MEMORY_TAG_TEXTURE,
    MEMORY_TAG_MATERIAL_INSTANCE,
    MEMORY_TAG_RENDERER,
    MEMORY_TAG_GAME,
    MEMORY_TAG_TRANSFORM,
    MEMORY_TAG_ENTITY,
    MEMORY_TAG_ENTITY_NODE,
    MEMORY_TAG_SCENE,
    MEMORY_TAG_RESOURCE,
    MEMORY_TAG_VULKAN,
    // "External" vulkan allocations, for reporting purposes only.
    MEMORY_TAG_VULKAN_EXT,
    MEMORY_TAG_DIRECT3D,
    MEMORY_TAG_OPENGL,
    // Representation of GPU-local/vram
    MEMORY_TAG_GPU_LOCAL,
    MEMORY_TAG_BITMAP_FONT,
    MEMORY_TAG_SYSTEM_FONT,
    MEMORY_TAG_KEYMAP,
    MEMORY_TAG_HASHTABLE,
    MEMORY_TAG_UI,
    MEMORY_TAG_AUDIO,
    MEMORY_TAG_REGISTRY,
    MEMORY_TAG_PLUGIN,
    MEMORY_TAG_PLATFORM,
    MEMORY_TAG_SERIALIZER,
    MEMORY_TAG_ASSET,

    MEMORY_TAG_MAX_TAGS
} memory_tag;

/** @brief The configuration for the memory system. */
typedef struct memory_system_configuration {
    /** @brief The total memory size in byes used by the internal allocator for this system. */
    u64 total_alloc_size;
} memory_system_configuration;

/**
 * @brief Initializes the memory system.
 * @param config The configuration for this system.
 */
KAPI b8 memory_system_initialize(memory_system_configuration config);

/**
 * @brief Shuts down the memory system.
 */
KAPI void memory_system_shutdown(void);

/**
 * @brief Performs a memory allocation from the host of the given size. The allocation
 * is tracked for the provided tag.
 * @param size The size of the allocation.
 * @param tag Indicates the use of the allocated block.
 * @returns If successful, a pointer to a block of allocated memory; otherwise 0.
 */
KAPI void* kallocate(u64 size, memory_tag tag);

/**
 * @brief Dynamically allocates memory for the given type. Also casts to type*.
 *
 * @param type The type to be used when determining allocation size.
 * @param mem_tag The memory tag to be used for the allocation.
 */
#define KALLOC_TYPE(type, mem_tag) (type*)kallocate(sizeof(type), mem_tag)

/**
 * @brief Frees the given dynamically-allocated memory of the provided type,
 * using the given tag.
 *
 * @param block The block of memory to be freed.
 * @param type The type to be used when determining allocation size.
 * @param mem_tag The memory tag to be used for the deallocation.
 */
#define KFREE_TYPE(block, type, mem_tag) kfree(block, sizeof(type), mem_tag)

/**
 * @brief Dynamically allocates memory for a standard C array of the given type.
 * Also casts to type*. Memory is tagged as MEMORY_TAG_ARRAY.
 *
 * @param type The type to be used when determining allocation size.
 * @param count The number of elements existing in the array.
 */
#define KALLOC_TYPE_CARRAY(type, count) (type*)kallocate(sizeof(type) * count, MEMORY_TAG_ARRAY)

/**
 * @brief Frees the given dynamically-allocated array of the provided type,
 * using MEMORY_TAG_ARRAY.
 *
 * @param block The block of memory to be freed.
 * @param type The type to be used when determining allocation size.
 * @param count The number of elements in the array to be freed.
 */
#define KFREE_TYPE_CARRAY(block, type, count) kfree(block, sizeof(type) * count, MEMORY_TAG_ARRAY)

/**
 * @brief Performs an aligned memory allocation from the host of the given size and alignment.
 * The allocation is tracked for the provided tag. NOTE: Memory allocated this way must be freed
 * using kfree_aligned.
 * @param size The size of the allocation.
 * @param alignment The alignment in bytes.
 * @param tag Indicates the use of the allocated block.
 * @returns If successful, a pointer to a block of allocated memory; otherwise 0.
 */
KAPI void* kallocate_aligned(u64 size, u16 alignment, memory_tag tag);

/**
 * @brief Reports an allocation associated with the application, but made externally.
 * This can be done for items allocated within 3rd party libraries, for example, to
 * track allocations but not perform them.
 *
 * @param size The size of the allocation.
 * @param tag Indicates the use of the allocated block.
 */
KAPI void kallocate_report(u64 size, memory_tag tag);

/**
 * @brief Performs a memory reallocation from the host of the given size, and also frees the
 * block of memory given. The reallocation is tracked for the provided tag.
 * @param block The block of memory to reallocate.
 * @param old_size The size of the old allocation (that gets freed).
 * @param new_size The size of the new allocation (that get allocated).
 * @param tag Indicates the use of the allocated block.
 * @returns If successful, a pointer to a block of allocated memory; otherwise 0.
 */
KAPI void* kreallocate(void* block, u64 old_size, u64 new_size, memory_tag tag);

/**
 * @brief Dynamically reallocates memory for a standard C array of the given type.
 * Also casts to type*. Memory is tagged as MEMORY_TAG_ARRAY.
 *
 * @param block The block of memory to reallocate.
 * @param type The type to be used when determining allocation size.
 * @param old_count The number of elements existing in the array.
 * @param new_count The number of elements in the resized array.
 */
#define KREALLOC_TYPE_CARRAY(block, type, old_count, new_count) (type*)kreallocate(block, sizeof(type) * old_count, sizeof(type) * new_count, MEMORY_TAG_ARRAY)

/**
 * @brief Performs a memory reallocation from the host of the given size and alignment, and also frees the
 * block of memory given. The reallocation is tracked for the provided tag.
 * NOTE: Memory allocated this way must be freed using kfree_aligned.

 * @param block The block of memory to reallocate.
 * @param old_size The size of the old allocation (that gets freed).
 * @param new_size The size of the new allocation (that get allocated).
 * @param alignment The byte alignment to be used for the reallocation.
 * @param tag Indicates the use of the allocated block.
 * @returns If successful, a pointer to a block of allocated memory; otherwise 0.
 */
KAPI void* kreallocate_aligned(void* block, u64 old_size, u64 new_size, u16 alignment, memory_tag tag);

/**
 * @brief Reports an allocation associated with the application, but made externally.
 * This can be done for items allocated within 3rd party libraries, for example, to
 * track allocations but not perform them.
 *
 * @param old_size The size of the old allocation.
 * @param new_size The size of the new allocation.
 * @param tag Indicates the use of the allocated block.
 */
KAPI void kreallocate_report(u64 old_size, u64 new_size, memory_tag tag);

/**
 * @brief Frees the given block, and untracks its size from the given tag.
 * @param block A pointer to the block of memory to be freed.
 * @param size The size of the block to be freed.
 * @param tag The tag indicating the block's use.
 */
KAPI void kfree(void* block, u64 size, memory_tag tag);

/**
 * @brief Frees the given block, and untracks its size from the given tag.
 * @param block A pointer to the block of memory to be freed.
 * @param size The size of the block to be freed.
 * @param tag The tag indicating the block's use.
 */
KAPI void kfree_aligned(void* block, u64 size, u16 alignment, memory_tag tag);

/**
 * @brief Reports a free associated with the application, but made externally.
 * This can be done for items allocated within 3rd party libraries, for example, to
 * track frees but not perform them.
 *
 * @param size The size in bytes.
 * @param tag The tag indicating the block's use.
 */
KAPI void kfree_report(u64 size, memory_tag tag);

/**
 * @brief Returns the size and alignment of the given block of memory.
 * NOTE: A failure result from this method most likely indicates heap corruption.
 *
 * @param block The memory block.
 * @param out_size A pointer to hold the size of the block.
 * @param out_alignment A pointer to hold the alignment of the block.
 * @return True on success; otherwise false.
 */
KAPI b8 kmemory_get_size_alignment(void* block, u64* out_size, u16* out_alignment);

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

#define KCOPY_TYPE(dest, source, type) kcopy_memory(dest, source, sizeof(type))
#define KCOPY_TYPE_CARRAY(dest, source, type, count) kcopy_memory(dest, source, sizeof(type) * count)

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
KAPI char* get_memory_usage_str(void);

/**
 * @brief Obtains the number of times kallocate was called since the memory system was initialized.
 * @returns The total count of allocations since the system's initialization.
 */
KAPI u64 get_memory_alloc_count(void);

/**
 * @brief Packs the values of 4 u8s into a single u32.
 *
 * @param x The first u8 to pack.
 * @param y The second u8 to pack.
 * @param z The third u8 to pack.
 * @param w The fourth u8 to pack.
 * @returns The packed u32.
 */
KAPI u32 pack_u8_into_u32(u8 x, u8 y, u8 z, u8 w);

/**
 * @brief Attempts to unpack 4 u8s from a u32.
 *
 * @param n The u32 to extract from.
 * @param x The first u8 to extract to. Required.
 * @param y The second u8 to extract to. Required.
 * @param z The third u8 to extract to. Required.
 * @param w The fourth u8 to extract to. Required.
 * @returns True if success, otherwise false.
 */
KAPI b8 unpack_u8_from_u32(u32 n, u8* x, u8* y, u8* z, u8* w);
