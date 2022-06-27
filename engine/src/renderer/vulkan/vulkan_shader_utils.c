#include "vulkan_shader_utils.h"

#include "core/kstring.h"
#include "core/logger.h"
#include "core/kmemory.h"

#include "systems/resource_system.h"

b8 create_shader_module(
    vulkan_context* context,
    const char* name,
    const char* type_str,
    VkShaderStageFlagBits shader_stage_flag,
    u32 stage_index,
    vulkan_shader_stage* shader_stages) {
    // Build file name, which will also be used as the resource name..
    char file_name[512];
    string_format(file_name, "shaders/%s.%s.spv", name, type_str);

    // Read the resource.
    resource binary_resource;
    if (!resource_system_load(file_name, RESOURCE_TYPE_BINARY, 0, &binary_resource)) {
        KERROR("Unable to read shader module: %s.", file_name);
        return false;
    }

    kzero_memory(&shader_stages[stage_index].create_info, sizeof(VkShaderModuleCreateInfo));
    shader_stages[stage_index].create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Use the resource's size and data directly.
    shader_stages[stage_index].create_info.codeSize = binary_resource.data_size;
    shader_stages[stage_index].create_info.pCode = (u32*)binary_resource.data;

    VK_CHECK(vkCreateShaderModule(
        context->device.logical_device,
        &shader_stages[stage_index].create_info,
        context->allocator,
        &shader_stages[stage_index].handle));

    // Release the resource.
    resource_system_unload(&binary_resource);

    // Shader stage info
    kzero_memory(&shader_stages[stage_index].shader_stage_create_info, sizeof(VkPipelineShaderStageCreateInfo));
    shader_stages[stage_index].shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[stage_index].shader_stage_create_info.stage = shader_stage_flag;
    shader_stages[stage_index].shader_stage_create_info.module = shader_stages[stage_index].handle;
    shader_stages[stage_index].shader_stage_create_info.pName = "main";

    return true;
}