#include "vulkan_image.h"

#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "resources/resource_types.h"
#include "vulkan/vulkan_core.h"
#include "vulkan_device.h"
#include "vulkan_utils.h"

void vulkan_image_create(
    vulkan_context* context,
    texture_type type,
    u32 width,
    u32 height,
    u16 layer_count,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memory_flags,
    b32 create_view,
    VkImageAspectFlags view_aspect_flags,
    const char* name,
    u32 mip_levels,
    vulkan_image* out_image) {
    if (mip_levels < 1) {
        KWARN("Mip levels must be >= 1. Defaulting to 1.");
        mip_levels = 1;
    }
    // Copy params
    out_image->width = width;
    out_image->height = height;
    out_image->memory_flags = memory_flags;
    out_image->name = string_duplicate(name);
    out_image->mip_levels = mip_levels;
    out_image->format = format;
    out_image->layer_count = layer_count;
    out_image->layer_views = 0;
    if (layer_count < 1) {
        layer_count = 1;
    }
    // Creation info.
    VkImageCreateInfo image_create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    switch (type) {
        default:
        case TEXTURE_TYPE_2D:
        case TEXTURE_TYPE_CUBE:      // Intentional, there is no cube image type.
        case TEXTURE_TYPE_2D_ARRAY:  // Intentional, there is no 2d_array image type.
            image_create_info.imageType = VK_IMAGE_TYPE_2D;
            break;
    }

    image_create_info.extent.width = width;
    image_create_info.extent.height = height;
    image_create_info.extent.depth = 1;  // TODO: Support configurable depth.
    image_create_info.mipLevels = out_image->mip_levels;
    image_create_info.arrayLayers = layer_count;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;          // TODO: Configurable sample count.
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // TODO: Configurable sharing mode.
    if (type == TEXTURE_TYPE_CUBE) {
        image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VK_CHECK(vkCreateImage(context->device.logical_device, &image_create_info, context->allocator, &out_image->handle));

    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_IMAGE, out_image->handle, out_image->name);

    // Query memory requirements.
    vkGetImageMemoryRequirements(context->device.logical_device, out_image->handle, &out_image->memory_requirements);

    i32 memory_type = context->find_memory_index(context, out_image->memory_requirements.memoryTypeBits, memory_flags);
    if (memory_type == -1) {
        KERROR("Required memory type not found. Image not valid.");
    }

    // Allocate memory
    VkMemoryAllocateInfo memory_allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory_allocate_info.allocationSize = out_image->memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(context->device.logical_device, &memory_allocate_info, context->allocator, &out_image->memory));
    if (out_image->name) {
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DEVICE_MEMORY, out_image->memory, out_image->name);
    }

    // Bind the memory
    VK_CHECK(vkBindImageMemory(context->device.logical_device, out_image->handle, out_image->memory, 0));  // TODO: configurable memory offset.

    // Report the memory as in-use.
    b8 is_device_memory = (out_image->memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    kallocate_report(out_image->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

    // Create view
    if (create_view) {
        // Single view, encapsulating all layers.
        out_image->view = 0;
        vulkan_image_view_create(context, type, layer_count, -1, format, out_image, view_aspect_flags, &out_image->view);

        // Only create views per layer if not a cube map.
        if (layer_count > 1 && type != TEXTURE_TYPE_CUBE) {
            // Multiple views, one per layer
            out_image->layer_views = kallocate(sizeof(VkImageView) * layer_count, MEMORY_TAG_ARRAY);
            for (u32 i = 0; i < layer_count; ++i) {
                vulkan_image_view_create(context, type, 1, i, format, out_image, view_aspect_flags, &out_image->layer_views[i]);
            }
        }
    }
}

// A lookup table of vulkan image view types indexed Kohi's texture types.
static VkImageViewType vulkan_view_types[3] = {
    VK_IMAGE_VIEW_TYPE_2D,
    VK_IMAGE_VIEW_TYPE_2D_ARRAY,
    VK_IMAGE_VIEW_TYPE_CUBE};

// Ensure changes to texture types break this if it isn't also updated.
STATIC_ASSERT(TEXTURE_TYPE_COUNT == (sizeof(vulkan_view_types) / sizeof(*vulkan_view_types)), "Texture type count does not match Vulkan image view lookup table count.");

void vulkan_image_view_create(
    vulkan_context* context,
    texture_type type,
    u16 layer_count,
    i32 layer_index,
    VkFormat format,
    vulkan_image* image,
    VkImageAspectFlags aspect_flags,
    VkImageView* out_view) {
    VkImageViewCreateInfo view_create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_create_info.image = image->handle;
    view_create_info.viewType = vulkan_view_types[type];
    view_create_info.format = format;
    view_create_info.subresourceRange.aspectMask = aspect_flags;
    view_create_info.subresourceRange.baseMipLevel = 0;
    view_create_info.subresourceRange.levelCount = image->mip_levels;
    view_create_info.subresourceRange.layerCount = layer_index < 0 ? layer_count : 1;
    view_create_info.subresourceRange.baseArrayLayer = layer_index < 0 ? 0 : layer_index;

    VK_CHECK(vkCreateImageView(context->device.logical_device, &view_create_info, context->allocator, out_view));

    char formatted_name[TEXTURE_NAME_MAX_LENGTH] = {0};
    string_format(formatted_name, "%s_view_idx_%u", image->name, layer_index);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_IMAGE_VIEW, *out_view, formatted_name);
}

void vulkan_image_transition_layout(
    vulkan_context* context,
    vulkan_command_buffer* command_buffer,
    vulkan_image* image,
    VkFormat format,
    VkImageLayout old_layout,
    VkImageLayout new_layout) {
    //
    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags dest_stage;
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    // TODO: only set source/dest stage once... split into functions.
    // Don't care about the old layout - transition to optimal layout (for the underlying implementation).
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        // Don't care what stage the pipeline is in at the start.
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        // Used for copying
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transitioning from a transfer destination layout to a shader-readonly layout.
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // From a copying stage to...
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        // The fragment stage.
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transitioning from a transfer source layout to a shader-readonly layout.
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // From a copying stage to...
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        // The fragment stage.
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        // Don't care what stage the pipeline is in at the start.
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        // Used for copying
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        KFATAL("unsupported layout transition!");
        return;
    }

    vkCmdPipelineBarrier(
        command_buffer->handle,
        source_stage, dest_stage,
        0,
        0, 0,
        0, 0,
        1, &barrier);
}

b8 vulkan_image_mipmaps_generate(vulkan_context* context, vulkan_image* image, vulkan_command_buffer* command_buffer) {
    if (image->mip_levels <= 1) {
        KWARN("Attempted to generate mips for an image that isn't configured for them.");
        return false;
    }

    // Check if the image format supports linear blitting.
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(context->device.physical_device, image->format, &format_properties);

    if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        KWARN("Texture image format does not support linear blitting! Mipmaps cannot be created.");
        return false;
    }

    // The same barrier can be used for all mip levels, albeit with some modifications for each one.
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image->handle;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;

    // One mip level at a time.
    barrier.subresourceRange.levelCount = 1;

    // Generate for all layers.
    barrier.subresourceRange.layerCount = image->layer_count;

    i32 mip_width = (i32)image->width;
    i32 mip_height = (i32)image->height;

    // Iterate each sub-mip level, starting at 1 (i.e. not the base level/full res image).
    // Each mip level uses the previous level as source material for the blitting operation.
    for (u32 i = 1; i < image->mip_levels; ++i) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        // Transition the mip image subresource to a transfer layout.
        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);

        // Setup the blit.
        VkImageBlit blit = {0};
        // Source offset is always in the upper-left corner.
        blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
        // The extents of the source mip level.
        blit.srcOffsets[1] = (VkOffset3D){mip_width, mip_height, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // Source is the previous level.
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = image->layer_count;
        // Destination offset is also always in the upper-left corner.
        blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
        // The destination extents are now half the width/height of the
        // previous, unless that previous was already 1.
        blit.dstOffsets[1] = (VkOffset3D){mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        // The destination is the current mip level.
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = image->layer_count;

        // Perform the blit for this layer.
        vkCmdBlitImage(
            command_buffer->handle,
            image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // Transition the previous mip layer's image subresource to a shader-readable layout.
        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);

        // Split the width and height in half for the next level, if there is one.
        if (mip_width > 1) {
            mip_width /= 2;
        }
        if (mip_height > 1) {
            mip_height /= 2;
        }
    }

    // Finally, transition the last mipmap level to a shader-readable layout.
    // This would not have been handled in the above loop since that always transitions
    // the previous layer.
    barrier.subresourceRange.baseMipLevel = image->mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, 0,
        0, 0,
        1, &barrier);

    return true;
}

void vulkan_image_copy_from_buffer(
    vulkan_context* context,
    vulkan_image* image,
    VkBuffer buffer,
    u64 offset,
    vulkan_command_buffer* command_buffer) {
    VkBufferImageCopy region;
    kzero_memory(&region, sizeof(VkBufferImageCopy));
    region.bufferOffset = offset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = image->layer_count;

    region.imageExtent.width = image->width;
    region.imageExtent.height = image->height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(
        command_buffer->handle,
        buffer,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

void vulkan_image_copy_to_buffer(
    vulkan_context* context,
    vulkan_image* image,
    VkBuffer buffer,
    vulkan_command_buffer* command_buffer) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = image->layer_count;

    region.imageExtent.width = image->width;
    region.imageExtent.height = image->height;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &region);
}

void vulkan_image_copy_pixel_to_buffer(
    vulkan_context* context,
    vulkan_image* image,
    VkBuffer buffer,
    u32 x,
    u32 y,
    vulkan_command_buffer* command_buffer) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = image->layer_count;

    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageExtent.width = 1;
    region.imageExtent.height = 1;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &region);
}

void vulkan_image_destroy(vulkan_context* context, vulkan_image* image) {
    if (image->view) {
        vkDestroyImageView(context->device.logical_device, image->view, context->allocator);
        image->view = 0;
    }
    if (image->layer_views) {
        for (u32 i = 0; i < image->layer_count; ++i) {
            vkDestroyImageView(context->device.logical_device, image->layer_views[i], context->allocator);
        }
        kfree(image->layer_views, sizeof(VkImageView) * image->layer_count, MEMORY_TAG_ARRAY);
        image->layer_views = 0;
        image->layer_count = 0;
    }
    if (image->memory) {
        vkFreeMemory(context->device.logical_device, image->memory, context->allocator);
        image->memory = 0;
    }
    if (image->handle) {
        vkDestroyImage(context->device.logical_device, image->handle, context->allocator);
        image->handle = 0;
    }
    if (image->name) {
        kfree(image->name, string_length(image->name) + 1, MEMORY_TAG_STRING);
        image->name = 0;
    }

    // Report the memory as no longer in-use.
    b8 is_device_memory = (image->memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    kfree_report(image->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
    kzero_memory(&image->memory_requirements, sizeof(VkMemoryRequirements));
}
