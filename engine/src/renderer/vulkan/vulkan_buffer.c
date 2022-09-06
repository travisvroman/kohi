#include "vulkan_buffer.h"

#include "vulkan_device.h"
#include "vulkan_command_buffer.h"
#include "vulkan_utils.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "containers/freelist.h"

void cleanup_freelist(vulkan_buffer* buffer) {
    if (buffer->has_freelist) {
        freelist_destroy(&buffer->buffer_freelist);
        kfree(buffer->freelist_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        buffer->freelist_memory_requirement = 0;
        buffer->freelist_block = 0;
    }
}

b8 vulkan_buffer_create(
    vulkan_context* context,
    u64 size,
    VkBufferUsageFlagBits usage,
    u32 memory_property_flags,
    b8 bind_on_create,
    b8 use_freelist,
    vulkan_buffer* out_buffer) {
    kzero_memory(out_buffer, sizeof(vulkan_buffer));
    out_buffer->has_freelist = use_freelist;
    out_buffer->total_size = size;
    out_buffer->usage = usage;
    out_buffer->memory_property_flags = memory_property_flags;

    if (use_freelist) {
        // Create a new freelist, if used.
        out_buffer->freelist_memory_requirement = 0;
        freelist_create(size, &out_buffer->freelist_memory_requirement, 0, 0);
        out_buffer->freelist_block = kallocate(out_buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        freelist_create(size, &out_buffer->freelist_memory_requirement, out_buffer->freelist_block, &out_buffer->buffer_freelist);
    }

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // NOTE: Only used in one queue.

    VK_CHECK(vkCreateBuffer(context->device.logical_device, &buffer_info, context->allocator, &out_buffer->handle));

    // Gather memory requirements.
    vkGetBufferMemoryRequirements(context->device.logical_device, out_buffer->handle, &out_buffer->memory_requirements);
    out_buffer->memory_index = context->find_memory_index(out_buffer->memory_requirements.memoryTypeBits, out_buffer->memory_property_flags);
    if (out_buffer->memory_index == -1) {
        KERROR("Unable to create vulkan buffer because the required memory type index was not found.");

        // Make sure to destroy the freelist.
        cleanup_freelist(out_buffer);
        return false;
    }

    // Allocate memory info
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = out_buffer->memory_requirements.size;
    allocate_info.memoryTypeIndex = (u32)out_buffer->memory_index;

    // Allocate the memory.
    VkResult result = vkAllocateMemory(
        context->device.logical_device,
        &allocate_info,
        context->allocator,
        &out_buffer->memory);

    // Determine if memory is on a device heap.
    b8 is_device_memory = (out_buffer->memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Report memory as in-use.
    kallocate_report(out_buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

    if (result != VK_SUCCESS) {
        KERROR("Unable to create vulkan buffer because the required memory allocation failed. Error: %i", result);

        // Make sure to destroy the freelist.
        cleanup_freelist(out_buffer);
        return false;
    }

    if (bind_on_create) {
        vulkan_buffer_bind(context, out_buffer, 0);
    }

    return true;
}

void vulkan_buffer_destroy(vulkan_context* context, vulkan_buffer* buffer) {
    if (buffer->freelist_block) {
        // Make sure to destroy the freelist.
        cleanup_freelist(buffer);
    }
    if (buffer->memory) {
        vkFreeMemory(context->device.logical_device, buffer->memory, context->allocator);
        buffer->memory = 0;
    }
    if (buffer->handle) {
        vkDestroyBuffer(context->device.logical_device, buffer->handle, context->allocator);
        buffer->handle = 0;
    }

    // Report the free memory.
    b8 is_device_memory = (buffer->memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    kfree_report(buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
    kzero_memory(&buffer->memory_requirements, sizeof(VkMemoryRequirements));

    buffer->total_size = 0;
    buffer->usage = 0;
    buffer->is_locked = false;
}

b8 vulkan_buffer_resize(
    vulkan_context* context,
    u64 new_size,
    vulkan_buffer* buffer,
    VkQueue queue,
    VkCommandPool pool) {
    // Sanity check.
    if (new_size < buffer->total_size) {
        KERROR("vulkan_buffer_resize requires that new size be larger than the old. Not doing this could lead to data loss.");
        return false;
    }

    if (buffer->has_freelist) {
        // Resize the freelist first, if used.
        u64 new_memory_requirement = 0;
        freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, 0, 0, 0);
        void* new_block = kallocate(new_memory_requirement, MEMORY_TAG_RENDERER);
        void* old_block = 0;
        if (!freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, new_block, new_size, &old_block)) {
            KERROR("vulkan_buffer_resize failed to resize internal free list.");
            kfree(new_block, new_memory_requirement, MEMORY_TAG_RENDERER);
            return false;
        }

        // Clean up the old memory, then assign the new properties over.
        kfree(old_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        buffer->freelist_memory_requirement = new_memory_requirement;
        buffer->freelist_block = new_block;
    }

    buffer->total_size = new_size;

    // Create new buffer.
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = new_size;
    buffer_info.usage = buffer->usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // NOTE: Only used in one queue.

    VkBuffer new_buffer;
    VK_CHECK(vkCreateBuffer(context->device.logical_device, &buffer_info, context->allocator, &new_buffer));

    // Gather memory requirements.
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(context->device.logical_device, new_buffer, &requirements);

    // Allocate memory info
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = (u32)buffer->memory_index;

    // Allocate the memory.
    VkDeviceMemory new_memory;
    VkResult result = vkAllocateMemory(context->device.logical_device, &allocate_info, context->allocator, &new_memory);
    if (result != VK_SUCCESS) {
        KERROR("Unable to resize vulkan buffer because the required memory allocation failed. Error: %i", result);
        return false;
    }

    // Bind the new buffer's memory
    VK_CHECK(vkBindBufferMemory(context->device.logical_device, new_buffer, new_memory, 0));

    // Copy over the data
    vulkan_buffer_copy_to(context, pool, 0, queue, buffer->handle, 0, new_buffer, 0, buffer->total_size);

    // Make sure anything potentially using these is finished.
    vkDeviceWaitIdle(context->device.logical_device);

    // Destroy the old
    if (buffer->memory) {
        vkFreeMemory(context->device.logical_device, buffer->memory, context->allocator);
        buffer->memory = 0;
    }
    if (buffer->handle) {
        vkDestroyBuffer(context->device.logical_device, buffer->handle, context->allocator);
        buffer->handle = 0;
    }

    // Report free of the old, allocate of the new.
    b8 is_device_memory = (buffer->memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    kfree_report(buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
    buffer->memory_requirements = requirements;
    kallocate_report(buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

    // Set new properties
    buffer->total_size = new_size;
    buffer->memory = new_memory;
    buffer->handle = new_buffer;

    return true;
}

void vulkan_buffer_bind(vulkan_context* context, vulkan_buffer* buffer, u64 offset) {
    VK_CHECK(vkBindBufferMemory(context->device.logical_device, buffer->handle, buffer->memory, offset));
}

void* vulkan_buffer_lock_memory(vulkan_context* context, vulkan_buffer* buffer, u64 offset, u64 size, u32 flags) {
    void* data;
    VK_CHECK(vkMapMemory(context->device.logical_device, buffer->memory, offset, size, flags, &data));
    return data;
}

void vulkan_buffer_unlock_memory(vulkan_context* context, vulkan_buffer* buffer) {
    vkUnmapMemory(context->device.logical_device, buffer->memory);
}

b8 vulkan_buffer_allocate(vulkan_buffer* buffer, u64 size, u64* out_offset) {
    if (!buffer || !size || !out_offset) {
        KERROR("vulkan_buffer_allocate requires valid buffer, a nonzero size and valid pointer to hold offset.");
        return false;
    }

    if (!buffer->has_freelist) {
        KWARN("vulkan_buffer_allocate called on a buffer not using freelists. Offset will not be valid. Call vulkan_buffer_load_data instead.");
        *out_offset = 0;
        return true;
    }
    return freelist_allocate_block(&buffer->buffer_freelist, size, out_offset);
}

b8 vulkan_buffer_free(vulkan_buffer* buffer, u64 size, u64 offset) {
    if (!buffer || !size) {
        KERROR("vulkan_buffer_free requires valid buffer and a nonzero size.");
        return false;
    }

    if (!buffer->has_freelist) {
        KWARN("vulkan_buffer_allocate called on a buffer not using freelists. Nothing was done.");
        return true;
    }
    return freelist_free_block(&buffer->buffer_freelist, size, offset);
}

void vulkan_buffer_load_data(vulkan_context* context, vulkan_buffer* buffer, u64 offset, u64 size, u32 flags, const void* data) {
    void* data_ptr;
    VK_CHECK(vkMapMemory(context->device.logical_device, buffer->memory, offset, size, flags, &data_ptr));
    kcopy_memory(data_ptr, data, size);
    vkUnmapMemory(context->device.logical_device, buffer->memory);
}

void vulkan_buffer_copy_to(
    vulkan_context* context,
    VkCommandPool pool,
    VkFence fence,
    VkQueue queue,
    VkBuffer source,
    u64 source_offset,
    VkBuffer dest,
    u64 dest_offset,
    u64 size) {
    vkQueueWaitIdle(queue);
    // Create a one-time-use command buffer.
    vulkan_command_buffer temp_command_buffer;
    vulkan_command_buffer_allocate_and_begin_single_use(context, pool, &temp_command_buffer);

    // Prepare the copy command and add it to the command buffer.
    VkBufferCopy copy_region;
    copy_region.srcOffset = source_offset;
    copy_region.dstOffset = dest_offset;
    copy_region.size = size;

    vkCmdCopyBuffer(temp_command_buffer.handle, source, dest, 1, &copy_region);

    // Submit the buffer for execution and wait for it to complete.
    vulkan_command_buffer_end_single_use(context, pool, &temp_command_buffer, queue);
}