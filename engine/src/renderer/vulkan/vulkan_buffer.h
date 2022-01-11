/**
 * @file vulkan_buffer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A Vulkan-specific data buffer.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief Creates a new Vulkan buffer.
 * 
 * @param context A pointer to the Vulkan context.
 * @param size The size of the buffer in bytes.
 * @param usage The buffer usage flags (VkBufferUsageFlagBits)
 * @param memory_property_flags The memory property flags.
 * @param bind_on_create Indicates if this buffer should bind on creation.
 * @param out_buffer A pointer to hold the newly-created buffer.
 * @return True on success; otherwise false.
 */
b8 vulkan_buffer_create(
    vulkan_context* context,
    u64 size,
    VkBufferUsageFlagBits usage,
    u32 memory_property_flags,
    b8 bind_on_create,
    vulkan_buffer* out_buffer);

/**
 * @brief Destroys the given buffer.
 * 
 * @param context A pointer to the Vulkan context.
 * @param buffer A pointer to the buffer to be destroyed.
 */
void vulkan_buffer_destroy(vulkan_context* context, vulkan_buffer* buffer);

/**
 * @brief Resizes the given buffer. In this case, a new internal buffer of the given
 * size is created, data from the old buffer is copied to it, then the old buffer is
 * destroyed. This means this operation must be done when the buffer is not in use.
 * 
 * @param context A pointer to the Vulkan context.
 * @param new_size The new size of the buffer.
 * @param buffer A pointer to the buffer to be resized.
 * @param queue The queue used for the buffer resize operations.
 * @param pool The command pool utilized for the internal temporary command buffer.
 * @return True on success; otherwise false.
 */
b8 vulkan_buffer_resize(
    vulkan_context* context,
    u64 new_size,
    vulkan_buffer* buffer,
    VkQueue queue,
    VkCommandPool pool);

/**
 * @brief Binds the given buffer for use.
 * 
 * @param context A pointer to the Vulkan context.
 * @param buffer A pointer to the buffer to be bound.
 * @param offset An offset in bytes to bind the buffer at.
 */
void vulkan_buffer_bind(vulkan_context* context, vulkan_buffer* buffer, u64 offset);

/**
 * @brief Locks (or maps) the buffer memory to a temporary location of host memory, which should be unlocked before 
 * shutdown or destruction.
 * 
 * @param context A pointer to the Vulkan context.
 * @param buffer A pointer to the buffer whose memory should be locked.
 * @param offset An offset in bytes to lock the memory at.
 * @param size The amount of memory to lock.
 * @param flags Flags to be used in the locking operation (VkMemoryMapFlags).
 * @return A pointer to a block of memory, mapped to the buffer's memory.
 */
void* vulkan_buffer_lock_memory(vulkan_context* context, vulkan_buffer* buffer, u64 offset, u64 size, u32 flags);

/**
 * @brief Unlocks (or unmaps) the buffer memory.
 * 
 * @param context A pointer to the Vulkan context.
 * @param buffer A pointer to the buffer whose memory should be unlocked.
 */
void vulkan_buffer_unlock_memory(vulkan_context* context, vulkan_buffer* buffer);

/**
 * @brief Loads a data range into the given buffer at a given offset. Internally performs a map,
 * copy and unmap.
 * 
 * @param context A pointer to the Vulkan context.
 * @param buffer A pointer to the buffer to load into.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param size The amount of data in bytes that will be loaded.
 * @param flags Flags to be used in the locking operation (VkMemoryMapFlags).
 * @param data A pointer to the data to be loaded.
 */
void vulkan_buffer_load_data(vulkan_context* context, vulkan_buffer* buffer, u64 offset, u64 size, u32 flags, const void* data);

/**
 * @brief Copies a range of data from one buffer to another.
 * 
 * @param context A pointer to the Vulkan context.
 * @param pool The command pool to be used.
 * @param @deprecated fence A fence to be used.
 * @param queue The queue to be used.
 * @param source The source buffer.
 * @param source_offset The source buffer offset.
 * @param dest The destination buffer.
 * @param dest_offset The destination buffer offset.
 * @param size The size of the data in bytes to be copied.
 */
void vulkan_buffer_copy_to(
    vulkan_context* context,
    VkCommandPool pool,
    VkFence fence,
    VkQueue queue,
    VkBuffer source,
    u64 source_offset,
    VkBuffer dest,
    u64 dest_offset,
    u64 size);
