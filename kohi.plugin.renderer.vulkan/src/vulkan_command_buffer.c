#include "vulkan_command_buffer.h"

#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kstring.h>

#include "platform/vulkan_platform.h"
#include "vulkan_types.h"
#include "vulkan_utils.h"

void vulkan_command_buffer_allocate(
    vulkan_context* context,
    VkCommandPool pool,
    b8 is_primary,
    const char* name,
    vulkan_command_buffer* out_command_buffer,
    u32 secondary_buffer_count) {

    krhi_vulkan* rhi = &context->rhi;

    kzero_memory(out_command_buffer, sizeof(vulkan_command_buffer));

    VkCommandBufferAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate_info.commandPool = pool;
    allocate_info.level = is_primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocate_info.commandBufferCount = 1;
    allocate_info.pNext = 0;

    out_command_buffer->state = COMMAND_BUFFER_STATE_NOT_ALLOCATED;
    VK_CHECK(rhi->kvkAllocateCommandBuffers(
        context->device.logical_device,
        &allocate_info,
        &out_command_buffer->handle));
    out_command_buffer->state = COMMAND_BUFFER_STATE_READY;
    // Store if the buffer is primary.
    out_command_buffer->is_primary = is_primary;

    if (name) {
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_COMMAND_BUFFER, out_command_buffer->handle, name);

#ifdef KOHI_DEBUG
        // Also keep a copy of the name for debugging purposes.
        out_command_buffer->name = string_duplicate(name);
#endif
    }

    // Allocate new secondary command buffers, if needed.
    if (secondary_buffer_count) {
        out_command_buffer->secondary_count = secondary_buffer_count;
        out_command_buffer->secondary_buffers = KALLOC_TYPE_CARRAY(vulkan_command_buffer, out_command_buffer->secondary_count);
        for (u32 j = 0; j < out_command_buffer->secondary_count; ++j) {
            vulkan_command_buffer* secondary_buffer = &out_command_buffer->secondary_buffers[j];
            char* secondary_name = string_format("%s_secondary_%d", name, j);
            vulkan_command_buffer_allocate(context, context->device.graphics_command_pool, false, secondary_name, secondary_buffer, 0);
            string_free(secondary_name);
            // Set the primary buffer pointer.
            secondary_buffer->parent = out_command_buffer;
        }
    }

    out_command_buffer->secondary_buffer_index = 0; // Start at the first secondary buffer.
    out_command_buffer->in_secondary = false;       // start off as "not in secondary".
}

void vulkan_command_buffer_free(vulkan_context* context, VkCommandPool pool, vulkan_command_buffer* command_buffer) {

    krhi_vulkan* rhi = &context->rhi;

    rhi->kvkFreeCommandBuffers(
        context->device.logical_device,
        pool,
        1,
        &command_buffer->handle);

    command_buffer->handle = 0;
    command_buffer->state = COMMAND_BUFFER_STATE_NOT_ALLOCATED;
}

void vulkan_command_buffer_begin(
    vulkan_context* context,
    vulkan_command_buffer* command_buffer,
    b8 is_single_use,
    b8 is_renderpass_continue,
    b8 is_simultaneous_use) {

    krhi_vulkan* rhi = &context->rhi;

    if (command_buffer->is_primary && command_buffer->state != COMMAND_BUFFER_STATE_READY) {
        KFATAL("vulkan_command_buffer_begin called on a command buffer that is not ready.");
    }
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = 0;
    if (is_single_use) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    if (is_renderpass_continue) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    }
    if (is_simultaneous_use) {
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    }

    // Include required inheritance info if the buffer is secondary.
    // This is mostly blank due to using dynamic rendering, but would require
    // renderpass/subpass information if those were used.
    VkCommandBufferInheritanceInfo inheritance_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inheritance_info.subpass = 0;
    if (command_buffer->is_primary) {
        begin_info.pInheritanceInfo = 0;
    } else {
        begin_info.pInheritanceInfo = &inheritance_info;
    }

    VK_CHECK(rhi->kvkBeginCommandBuffer(command_buffer->handle, &begin_info));
    command_buffer->state = COMMAND_BUFFER_STATE_RECORDING;
}

void vulkan_command_buffer_end(vulkan_context* context, vulkan_command_buffer* command_buffer) {
    krhi_vulkan* rhi = &context->rhi;
    VK_CHECK(rhi->kvkEndCommandBuffer(command_buffer->handle));
    if (command_buffer->is_primary && command_buffer->state != COMMAND_BUFFER_STATE_RECORDING) {
        KFATAL("vulkan_command_buffer_begin called on a command buffer that is not currently being recorded to.");
    }
    command_buffer->state = COMMAND_BUFFER_STATE_RECORDING_ENDED;
}

b8 vulkan_command_buffer_submit(
    vulkan_context* context,
    vulkan_command_buffer* command_buffer,
    VkQueue queue,
    u32 signal_semaphore_count,
    VkSemaphore* signal_semaphores,
    u32 wait_semaphore_count,
    VkSemaphore* wait_semaphores,
    VkFence fence) {
    krhi_vulkan* rhi = &context->rhi;
    if (command_buffer->state != COMMAND_BUFFER_STATE_RECORDING_ENDED) {
        KFATAL("vulkan_command_buffer_update_submitted called on a command buffer that is not ready to be submitted.");
    }
    command_buffer->state = COMMAND_BUFFER_STATE_SUBMITTED;

    // Submit the queue and wait for the operation to complete.
    // Begin queue submission
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};

    // Command buffer(s) to be executed.
    submit_info.commandBufferCount = 1;
    // Update the state of the secondary buffers.
    for (u32 i = 0; i < command_buffer->secondary_count; ++i) {
        vulkan_command_buffer* secondary = &command_buffer->secondary_buffers[i];
        if (secondary->state == COMMAND_BUFFER_STATE_RECORDING_ENDED) {
            secondary->state = COMMAND_BUFFER_STATE_SUBMITTED;
        }
    }
    submit_info.pCommandBuffers = &command_buffer->handle;

    // The semaphore(s) to be signaled when the queue is complete.
    submit_info.signalSemaphoreCount = signal_semaphore_count;
    submit_info.pSignalSemaphores = signal_semaphores;

    // Wait semaphore ensures that the operation cannot begin until the image is available.
    submit_info.waitSemaphoreCount = wait_semaphore_count;
    submit_info.pWaitSemaphores = wait_semaphores;

    // Each semaphore waits on the corresponding pipeline stage to complete. 1:1
    // ratio. VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT prevents subsequent
    // colour attachment writes from executing until the semaphore signals (i.e.
    // one frame is presented at a time)
    VkPipelineStageFlags flags[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = flags;

    VkResult result = rhi->kvkQueueSubmit(queue, 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        KERROR("vulkan_command_buffer_submit() - vkQueueSubmit failed with result: %s", vulkan_result_string(result, true));
        return false;
    }

    return true;
}

void vulkan_command_buffer_execute_secondary(vulkan_context* context, vulkan_command_buffer* secondary) {
    krhi_vulkan* rhi = &context->rhi;
    vulkan_command_buffer* primary = secondary->parent;
    if (!primary) {
        if (secondary->is_primary) {
            KFATAL("vulkan_command_buffer_execute_secondary called on primary command buffer.");
        } else {
            KFATAL("vulkan_command_buffer_execute_secondary called on command buffer with no parent.");
        }
        return;
    }

    // Execute the secondary command buffer via the primary buffer.
    rhi->kvkCmdExecuteCommands(primary->handle, 1, &secondary->handle);

    // Move on to the next buffer index
    primary->secondary_buffer_index++;
    primary->in_secondary = false;
}

void vulkan_command_buffer_reset(vulkan_command_buffer* command_buffer) {
    if (command_buffer->state != COMMAND_BUFFER_STATE_SUBMITTED && command_buffer->state != COMMAND_BUFFER_STATE_READY) {
        KFATAL("vulkan_command_buffer_reset called on a command buffer that has not been submitted.");
    }
    command_buffer->state = COMMAND_BUFFER_STATE_READY;
}

void vulkan_command_buffer_allocate_and_begin_single_use(
    vulkan_context* context,
    VkCommandPool pool,
    vulkan_command_buffer* out_command_buffer) {
    vulkan_command_buffer_allocate(context, pool, true, "single_use_command_buffer", out_command_buffer, 0);
    vulkan_command_buffer_begin(context, out_command_buffer, true, false, false);
}

void vulkan_command_buffer_end_single_use(
    vulkan_context* context,
    VkCommandPool pool,
    vulkan_command_buffer* command_buffer,
    VkQueue queue) {
    krhi_vulkan* rhi = &context->rhi;
    // End the command buffer.
    vulkan_command_buffer_end(context, command_buffer);

    // Submit the queue
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer->handle;
    VK_CHECK(rhi->kvkQueueSubmit(queue, 1, &submit_info, 0));

    // Wait for it to finish
    VK_CHECK(rhi->kvkQueueWaitIdle(queue));

    // Free the command buffer.
    vulkan_command_buffer_free(context, pool, command_buffer);
}
