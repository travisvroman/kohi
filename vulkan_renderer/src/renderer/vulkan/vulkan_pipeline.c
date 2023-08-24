#include "vulkan_pipeline.h"

#include <containers/darray.h>
#include <vulkan/vulkan_core.h>

#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "renderer/vulkan/vulkan_types.h"
#include "resources/resource_types.h"
#include "systems/shader_system.h"
#include "vulkan_utils.h"

b8 vulkan_graphics_pipeline_create(vulkan_context* context, const vulkan_pipeline_config* config, vulkan_pipeline* out_pipeline) {
    // Viewport state
    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &config->viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &config->scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer_create_info.depthClampEnable = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = (config->shader_flags & SHADER_FLAG_WIREFRAME) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer_create_info.lineWidth = 1.0f;
    switch (config->cull_mode) {
        case FACE_CULL_MODE_NONE:
            rasterizer_create_info.cullMode = VK_CULL_MODE_NONE;
            break;
        case FACE_CULL_MODE_FRONT:
            rasterizer_create_info.cullMode = VK_CULL_MODE_FRONT_BIT;
            break;
        default:
        case FACE_CULL_MODE_BACK:
            rasterizer_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
            break;
        case FACE_CULL_MODE_FRONT_AND_BACK:
            rasterizer_create_info.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
            break;
    }

    if (config->winding == RENDERER_WINDING_CLOCKWISE) {
        rasterizer_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    } else if (config->winding == RENDERER_WINDING_COUNTER_CLOCKWISE) {
        rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    } else {
        KWARN("Invalid front-face winding order specified, default to counter-clockwise");
        rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    rasterizer_create_info.depthBiasEnable = VK_FALSE;
    rasterizer_create_info.depthBiasConstantFactor = 0.0f;
    rasterizer_create_info.depthBiasClamp = 0.0f;
    rasterizer_create_info.depthBiasSlopeFactor = 0.0f;

    // Smooth line rasterisation, if supported.
    VkPipelineRasterizationLineStateCreateInfoEXT line_rasterization_ext = {0};
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) {
        line_rasterization_ext.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
        line_rasterization_ext.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
        rasterizer_create_info.pNext = &line_rasterization_ext;
    }

    // Multisampling.
    VkPipelineMultisampleStateCreateInfo multisampling_create_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling_create_info.sampleShadingEnable = VK_FALSE;
    multisampling_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling_create_info.minSampleShading = 1.0f;
    multisampling_create_info.pSampleMask = 0;
    multisampling_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_create_info.alphaToOneEnable = VK_FALSE;

    // Depth and stencil testing.
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (config->shader_flags & SHADER_FLAG_DEPTH_TEST) {
        depth_stencil.depthTestEnable = VK_TRUE;
        if (config->shader_flags & SHADER_FLAG_DEPTH_WRITE) {
            depth_stencil.depthWriteEnable = VK_TRUE;
        }
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = VK_FALSE;
    }

    VkPipelineColorBlendAttachmentState color_blend_attachment_state;
    kzero_memory(&color_blend_attachment_state, sizeof(VkPipelineColorBlendAttachmentState));
    color_blend_attachment_state.blendEnable = VK_TRUE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = 1;
    color_blend_state_create_info.pAttachments = &color_blend_attachment_state;

    // Dynamic state
    VkDynamicState* dynamic_states = darray_create(VkDynamicState);
    darray_push(dynamic_states, VK_DYNAMIC_STATE_VIEWPORT);
    darray_push(dynamic_states, VK_DYNAMIC_STATE_SCISSOR);
    // Primitive topology, if supported.
    if ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_TOPOLOGY_BIT) || (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_TOPOLOGY_BIT)) {
        darray_push(dynamic_states, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
    }
    // Front-face, if supported.
    if ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_FRONT_FACE_BIT) || (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_FRONT_FACE_BIT)) {
        darray_push(dynamic_states, VK_DYNAMIC_STATE_FRONT_FACE);
    }

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state_create_info.dynamicStateCount = darray_length(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    // Vertex input
    VkVertexInputBindingDescription binding_description;
    binding_description.binding = 0;  // Binding index
    binding_description.stride = config->stride;
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;  // Move to next data entry for each vertex.

    // Attributes
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = config->attribute_count;
    vertex_input_info.pVertexAttributeDescriptions = config->attributes;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    // The pipeline being created already has available types, so just grab the first one.
    for (u32 i = 1; i < PRIMITIVE_TOPOLOGY_TYPE_MAX; i = i << 1) {
        if (out_pipeline->supported_topology_types & i) {
            primitive_topology_type ptt = i;

            switch (ptt) {
                case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    break;
                case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;
                case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                    break;
                case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    break;
                case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    break;
                case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN:
                    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                    break;
                default:
                    KWARN("primitive topology '%u' not supported. Skipping.", ptt);
                    break;
            }

            break;
        }
    }
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    // Push constants
    VkPushConstantRange ranges[32];
    if (config->push_constant_range_count > 0) {
        if (config->push_constant_range_count > 32) {
            KERROR("vulkan_graphics_pipeline_create: cannot have more than 32 push constant ranges. Passed count: %i", config->push_constant_range_count);
            return false;
        }

        // NOTE: 32 is the max number of ranges we can ever have, since spec only guarantees 128 bytes with 4-byte alignment.
        kzero_memory(ranges, sizeof(VkPushConstantRange) * 32);
        for (u32 i = 0; i < config->push_constant_range_count; ++i) {
            ranges[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            ranges[i].offset = config->push_constant_ranges[i].offset;
            ranges[i].size = config->push_constant_ranges[i].size;
        }
        pipeline_layout_create_info.pushConstantRangeCount = config->push_constant_range_count;
        pipeline_layout_create_info.pPushConstantRanges = ranges;
    } else {
        pipeline_layout_create_info.pushConstantRangeCount = 0;
        pipeline_layout_create_info.pPushConstantRanges = 0;
    }

    // Descriptor set layouts
    pipeline_layout_create_info.setLayoutCount = config->descriptor_set_layout_count;
    pipeline_layout_create_info.pSetLayouts = config->descriptor_set_layouts;

    // Create the pipeline layout.
    VK_CHECK(vkCreatePipelineLayout(
        context->device.logical_device,
        &pipeline_layout_create_info,
        context->allocator,
        &out_pipeline->pipeline_layout));

    char pipeline_layout_name_buf[512] = {0};
    string_format(pipeline_layout_name_buf, "pipeline_layout_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT, out_pipeline->pipeline_layout, pipeline_layout_name_buf);

    // Pipeline create
    VkGraphicsPipelineCreateInfo pipeline_create_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_create_info.stageCount = config->stage_count;
    pipeline_create_info.pStages = config->stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;

    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    pipeline_create_info.pMultisampleState = &multisampling_create_info;
    pipeline_create_info.pDepthStencilState = (config->shader_flags & SHADER_FLAG_DEPTH_TEST) ? &depth_stencil : 0;
    pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    pipeline_create_info.pTessellationState = 0;

    pipeline_create_info.layout = out_pipeline->pipeline_layout;

    pipeline_create_info.renderPass = config->renderpass->handle;
    pipeline_create_info.subpass = 0;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;

    VkResult result = vkCreateGraphicsPipelines(
        context->device.logical_device,
        VK_NULL_HANDLE,
        1,
        &pipeline_create_info,
        context->allocator,
        &out_pipeline->handle);

    // Cleanup
    darray_destroy(dynamic_states);

    char pipeline_name_buf[512] = {0};
    string_format(pipeline_name_buf, "pipeline_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE, out_pipeline->handle, pipeline_name_buf);

    if (vulkan_result_is_success(result)) {
        KDEBUG("Graphics pipeline created!");
        return true;
    }

    KERROR("vkCreateGraphicsPipelines failed with %s.", vulkan_result_string(result, true));
    return false;
}

void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline) {
    if (pipeline) {
        // Destroy pipeline
        if (pipeline->handle) {
            vkDestroyPipeline(context->device.logical_device, pipeline->handle, context->allocator);
            pipeline->handle = 0;
        }

        // Destroy layout
        if (pipeline->pipeline_layout) {
            vkDestroyPipelineLayout(context->device.logical_device, pipeline->pipeline_layout, context->allocator);
            pipeline->pipeline_layout = 0;
        }
    }
}

void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline) {
    vkCmdBindPipeline(command_buffer->handle, bind_point, pipeline->handle);
}
