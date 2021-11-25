#include "vulkan_renderpass.h"

#include "core/kmemory.h"

void vulkan_renderpass_create(
    vulkan_context* context,
    vulkan_renderpass* out_renderpass,
    vec4 render_area,
    vec4 clear_colour,
    f32 depth,
    u32 stencil,
    u8 clear_flags,
    b8 has_prev_pass,
    b8 has_next_pass) {
    out_renderpass->clear_flags = clear_flags;
    out_renderpass->render_area = render_area;
    out_renderpass->clear_colour = clear_colour;
    out_renderpass->has_prev_pass = has_prev_pass;
    out_renderpass->has_next_pass = has_next_pass;

    out_renderpass->depth = depth;
    out_renderpass->stencil = stencil;

    // Main subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Attachments TODO: make this configurable.
    u32 attachment_description_count = 0;
    VkAttachmentDescription attachment_descriptions[2];

    // Color attachment
    b8 do_clear_colour = (out_renderpass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;
    VkAttachmentDescription color_attachment;
    color_attachment.format = context->swapchain.image_format.format;  // TODO: configurable
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = do_clear_colour ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // If coming from a previous pass, should already be VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. Otherwise undefined.
    color_attachment.initialLayout = has_prev_pass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

    // If going to another pass, use VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. Otherwise VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
    color_attachment.finalLayout = has_next_pass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Transitioned to after the render pass
    color_attachment.flags = 0;

    attachment_descriptions[attachment_description_count] = color_attachment;
    attachment_description_count++;

    VkAttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;  // Attachment description array index
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;

    // Depth attachment, if there is one
    b8 do_clear_depth = (out_renderpass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
    if (do_clear_depth) {
        VkAttachmentDescription depth_attachment = {};
        depth_attachment.format = context->device.depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = do_clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachment_descriptions[attachment_description_count] = depth_attachment;
        attachment_description_count++;

        // Depth attachment reference
        VkAttachmentReference depth_attachment_reference;
        depth_attachment_reference.attachment = 1;
        depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // TODO: other attachment types (input, resolve, preserve)

        // Depth stencil data.
        subpass.pDepthStencilAttachment = &depth_attachment_reference;
    } else {
        kzero_memory(&attachment_descriptions[attachment_description_count], sizeof(VkAttachmentDescription));
        subpass.pDepthStencilAttachment = 0;
    }

    // Input from a shader
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = 0;

    // Attachments used for multisampling colour attachments
    subpass.pResolveAttachments = 0;

    // Attachments not used in this subpass, but must be preserved for the next.
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = 0;

    // Render pass dependencies. TODO: make this configurable.
    VkSubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    // Render pass create.
    VkRenderPassCreateInfo render_pass_create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_create_info.attachmentCount = attachment_description_count;
    render_pass_create_info.pAttachments = attachment_descriptions;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;
    render_pass_create_info.pNext = 0;
    render_pass_create_info.flags = 0;

    VK_CHECK(vkCreateRenderPass(
        context->device.logical_device,
        &render_pass_create_info,
        context->allocator,
        &out_renderpass->handle));
}

void vulkan_renderpass_destroy(vulkan_context* context, vulkan_renderpass* renderpass) {
    if (renderpass && renderpass->handle) {
        vkDestroyRenderPass(context->device.logical_device, renderpass->handle, context->allocator);
        renderpass->handle = 0;
    }
}

void vulkan_renderpass_begin(
    vulkan_command_buffer* command_buffer,
    vulkan_renderpass* renderpass,
    VkFramebuffer frame_buffer) {
    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = renderpass->handle;
    begin_info.framebuffer = frame_buffer;
    begin_info.renderArea.offset.x = renderpass->render_area.x;
    begin_info.renderArea.offset.y = renderpass->render_area.y;
    begin_info.renderArea.extent.width = renderpass->render_area.z;
    begin_info.renderArea.extent.height = renderpass->render_area.w;

    begin_info.clearValueCount = 0;
    begin_info.pClearValues = 0;

    VkClearValue clear_values[2];
    kzero_memory(clear_values, sizeof(VkClearValue) * 2);
    b8 do_clear_colour = (renderpass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;
    if (do_clear_colour) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, renderpass->clear_colour.elements, sizeof(f32) * 4);
        begin_info.clearValueCount++;
    }

    b8 do_clear_depth = (renderpass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
    if (do_clear_depth) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, renderpass->clear_colour.elements, sizeof(f32) * 4);
        clear_values[begin_info.clearValueCount].depthStencil.depth = renderpass->depth;

        b8 do_clear_stencil = (renderpass->clear_flags & RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG) != 0;
        clear_values[begin_info.clearValueCount].depthStencil.stencil = do_clear_stencil ? renderpass->stencil : 0;
        begin_info.clearValueCount++;
    }
    
    begin_info.pClearValues = begin_info.clearValueCount > 0 ? clear_values : 0;

    vkCmdBeginRenderPass(command_buffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    command_buffer->state = COMMAND_BUFFER_STATE_IN_RENDER_PASS;
}

void vulkan_renderpass_end(vulkan_command_buffer* command_buffer, vulkan_renderpass* renderpass) {
    vkCmdEndRenderPass(command_buffer->handle);
    command_buffer->state = COMMAND_BUFFER_STATE_RECORDING;
}