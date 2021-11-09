#include "vulkan_object_shader.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "math/math_types.h"
#include "math/kmath.h"

#include "renderer/vulkan/vulkan_shader_utils.h"
#include "renderer/vulkan/vulkan_pipeline.h"
#include "renderer/vulkan/vulkan_buffer.h"

#define BUILTIN_SHADER_NAME_OBJECT "Builtin.ObjectShader"

b8 vulkan_object_shader_create(vulkan_context* context, vulkan_object_shader* out_shader) {
    // Shader module init per stage.
    char stage_type_strs[OBJECT_SHADER_STAGE_COUNT][5] = {"vert", "frag"};
    VkShaderStageFlagBits stage_types[OBJECT_SHADER_STAGE_COUNT] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};

    for (u32 i = 0; i < OBJECT_SHADER_STAGE_COUNT; ++i) {
        if (!create_shader_module(context, BUILTIN_SHADER_NAME_OBJECT, stage_type_strs[i], stage_types[i], i, out_shader->stages)) {
            KERROR("Unable to create %s shader module for '%s'.", stage_type_strs[i], BUILTIN_SHADER_NAME_OBJECT);
            return false;
        }
    }

    // Global Descriptors
    VkDescriptorSetLayoutBinding global_ubo_layout_binding;
    global_ubo_layout_binding.binding = 0;
    global_ubo_layout_binding.descriptorCount = 1;
    global_ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_ubo_layout_binding.pImmutableSamplers = 0;
    global_ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo global_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    global_layout_info.bindingCount = 1;
    global_layout_info.pBindings = &global_ubo_layout_binding;
    VK_CHECK(vkCreateDescriptorSetLayout(context->device.logical_device, &global_layout_info, context->allocator, &out_shader->global_descriptor_set_layout));

    // Global descriptor pool: Used for global items such as view/projection matrix.
    VkDescriptorPoolSize global_pool_size;
    global_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_pool_size.descriptorCount = context->swapchain.image_count;

    VkDescriptorPoolCreateInfo global_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    global_pool_info.poolSizeCount = 1;
    global_pool_info.pPoolSizes = &global_pool_size;
    global_pool_info.maxSets = context->swapchain.image_count;
    VK_CHECK(vkCreateDescriptorPool(context->device.logical_device, &global_pool_info, context->allocator, &out_shader->global_descriptor_pool));

    // Local/Object Descriptors
    const u32 local_sampler_count = 1;
    VkDescriptorType descriptor_types[VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // Binding 0 - uniform buffer
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // Binding 1 - Diffuse sampler layout.
    };
    VkDescriptorSetLayoutBinding bindings[VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT];
    kzero_memory(&bindings, sizeof(VkDescriptorSetLayoutBinding) * VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT);
    for (u32 i = 0; i < VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = descriptor_types[i];
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT;
    layout_info.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(context->device.logical_device, &layout_info, 0, &out_shader->object_descriptor_set_layout));

    // Local/Object descriptor pool: Used for object-specific items like diffuse colour
    VkDescriptorPoolSize object_pool_sizes[2];
    // The first section will be used for uniform buffers
    object_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    object_pool_sizes[0].descriptorCount = VULKAN_OBJECT_MAX_OBJECT_COUNT;
    // The second section will be used for image samplers.
    object_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    object_pool_sizes[1].descriptorCount = local_sampler_count * VULKAN_OBJECT_MAX_OBJECT_COUNT;

    VkDescriptorPoolCreateInfo object_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    object_pool_info.poolSizeCount = 2;
    object_pool_info.pPoolSizes = object_pool_sizes;
    object_pool_info.maxSets = VULKAN_OBJECT_MAX_OBJECT_COUNT;

    // Create object descriptor pool.
    VK_CHECK(vkCreateDescriptorPool(context->device.logical_device, &object_pool_info, context->allocator, &out_shader->object_descriptor_pool));

    // Pipeline creation
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)context->framebuffer_height;
    viewport.width = (f32)context->framebuffer_width;
    viewport.height = -(f32)context->framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = context->framebuffer_width;
    scissor.extent.height = context->framebuffer_height;

    // Attributes
    u32 offset = 0;
#define ATTRIBUTE_COUNT 2
    VkVertexInputAttributeDescription attribute_descriptions[ATTRIBUTE_COUNT];
    // Position, texcoord
    VkFormat formats[ATTRIBUTE_COUNT] = {
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT};
    u64 sizes[ATTRIBUTE_COUNT] = {
        sizeof(vec3),
        sizeof(vec2)};
    for (u32 i = 0; i < ATTRIBUTE_COUNT; ++i) {
        attribute_descriptions[i].binding = 0;   // binding index - should match binding desc
        attribute_descriptions[i].location = i;  // attrib location
        attribute_descriptions[i].format = formats[i];
        attribute_descriptions[i].offset = offset;
        offset += sizes[i];
    }

    // Desciptor set layouts.
    const i32 descriptor_set_layout_count = 2;
    VkDescriptorSetLayout layouts[2] = {
        out_shader->global_descriptor_set_layout,
        out_shader->object_descriptor_set_layout};

    // Stages
    // NOTE: Should match the number of shader->stages.
    VkPipelineShaderStageCreateInfo stage_create_infos[OBJECT_SHADER_STAGE_COUNT];
    kzero_memory(stage_create_infos, sizeof(stage_create_infos));
    for (u32 i = 0; i < OBJECT_SHADER_STAGE_COUNT; ++i) {
        stage_create_infos[i].sType = out_shader->stages[i].shader_stage_create_info.sType;
        stage_create_infos[i] = out_shader->stages[i].shader_stage_create_info;
    }

    if (!vulkan_graphics_pipeline_create(
            context,
            &context->main_renderpass,
            ATTRIBUTE_COUNT,
            attribute_descriptions,
            descriptor_set_layout_count,
            layouts,
            OBJECT_SHADER_STAGE_COUNT,
            stage_create_infos,
            viewport,
            scissor,
            false,
            &out_shader->pipeline)) {
        KERROR("Failed to load graphics pipeline for object shader.");
        return false;
    }

    // Create uniform buffer.
    if (!vulkan_buffer_create(
            context,
            sizeof(global_uniform_object),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true,
            &out_shader->global_uniform_buffer)) {
        KERROR("Vulkan buffer creation failed for object shader.");
        return false;
    }

    // Allocate global descriptor sets.
    VkDescriptorSetLayout global_layouts[3] = {
        out_shader->global_descriptor_set_layout,
        out_shader->global_descriptor_set_layout,
        out_shader->global_descriptor_set_layout};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = out_shader->global_descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = global_layouts;
    VK_CHECK(vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, out_shader->global_descriptor_sets));

    // Create the object uniform buffer.
    if (!vulkan_buffer_create(
            context,
            sizeof(object_uniform_object),  //* MAX_MATERIAL_INSTANCE_COUNT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true,
            &out_shader->object_uniform_buffer)) {
        KERROR("Material instance buffer creation failed for shader.");
        return false;
    }

    return true;
}

void vulkan_object_shader_destroy(vulkan_context* context, struct vulkan_object_shader* shader) {
    VkDevice logical_device = context->device.logical_device;

    vkDestroyDescriptorPool(logical_device, shader->object_descriptor_pool, context->allocator);
    vkDestroyDescriptorSetLayout(logical_device, shader->object_descriptor_set_layout, context->allocator);

    // Destroy uniform buffers.
    vulkan_buffer_destroy(context, &shader->global_uniform_buffer);
    vulkan_buffer_destroy(context, &shader->object_uniform_buffer);

    // Destroy pipeline.
    vulkan_pipeline_destroy(context, &shader->pipeline);

    // Destroy global descriptor pool.
    vkDestroyDescriptorPool(logical_device, shader->global_descriptor_pool, context->allocator);

    // Destroy descriptor set layouts.
    vkDestroyDescriptorSetLayout(logical_device, shader->global_descriptor_set_layout, context->allocator);

    // Destroy shader modules.
    for (u32 i = 0; i < OBJECT_SHADER_STAGE_COUNT; ++i) {
        vkDestroyShaderModule(context->device.logical_device, shader->stages[i].handle, context->allocator);
        shader->stages[i].handle = 0;
    }
}

void vulkan_object_shader_use(vulkan_context* context, struct vulkan_object_shader* shader) {
    u32 image_index = context->image_index;
    vulkan_pipeline_bind(&context->graphics_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, &shader->pipeline);
}

void vulkan_object_shader_update_global_state(vulkan_context* context, struct vulkan_object_shader* shader, f32 delta_time) {
    u32 image_index = context->image_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;
    VkDescriptorSet global_descriptor = shader->global_descriptor_sets[image_index];

    // Bind the global descriptor set to be updated.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline.pipeline_layout, 0, 1, &global_descriptor, 0, 0);

    // Configure the descriptors for the given index.
    u32 range = sizeof(global_uniform_object);
    u64 offset = 0;

    // Copy data to buffer
    vulkan_buffer_load_data(context, &shader->global_uniform_buffer, offset, range, 0, &shader->global_ubo);

    VkDescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = shader->global_uniform_buffer.handle;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    // Update descriptor sets.
    VkWriteDescriptorSet descriptor_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptor_write.dstSet = shader->global_descriptor_sets[image_index];
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(context->device.logical_device, 1, &descriptor_write, 0, 0);
}

void vulkan_object_shader_update_object(vulkan_context* context, struct vulkan_object_shader* shader, geometry_render_data data) {
    u32 image_index = context->image_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;

    vkCmdPushConstants(command_buffer, shader->pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &data.model);

    // Obtain material data.
    vulkan_object_shader_object_state* object_state = &shader->object_states[data.object_id];
    VkDescriptorSet object_descriptor_set = object_state->descriptor_sets[image_index];

    // TODO: if needs update
    VkWriteDescriptorSet descriptor_writes[VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT];
    kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT);
    u32 descriptor_count = 0;
    u32 descriptor_index = 0;

    // Descriptor 0 - Uniform buffer
    u32 range = sizeof(object_uniform_object);
    u64 offset = sizeof(object_uniform_object) * data.object_id;  // also the index into the array.
    object_uniform_object obo;

    // TODO: get diffuse colour from a material.
    static f32 accumulator = 0.0f;
    accumulator += context->frame_delta_time;
    f32 s = (ksin(accumulator) + 1.0f) / 2.0f;  // scale from -1, 1 to 0, 1
    obo.diffuse_color = vec4_create(s, s, s, 1.0f);

    // Load the data into the buffer.
    vulkan_buffer_load_data(context, &shader->object_uniform_buffer, offset, range, 0, &obo);

    // Only do this if the descriptor has not yet been updated.
    if (object_state->descriptor_states[descriptor_index].generations[image_index] == INVALID_ID) {
        VkDescriptorBufferInfo buffer_info;
        buffer_info.buffer = shader->object_uniform_buffer.handle;
        buffer_info.offset = offset;
        buffer_info.range = range;

        VkWriteDescriptorSet descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        descriptor.dstSet = object_descriptor_set;
        descriptor.dstBinding = descriptor_index;
        descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor.descriptorCount = 1;
        descriptor.pBufferInfo = &buffer_info;

        descriptor_writes[descriptor_count] = descriptor;
        descriptor_count++;

        // Update the frame generation. In this case it is only needed once since this is a buffer.
        object_state->descriptor_states[descriptor_index].generations[image_index] = 1;
    }
    descriptor_index++;

    // TODO: samplers.
    const u32 sampler_count = 1;
    VkDescriptorImageInfo image_infos[1];
    for (u32 sampler_index = 0; sampler_index < sampler_count; ++sampler_index) {
        texture* t = data.textures[sampler_index];
        u32* descriptor_generation = &object_state->descriptor_states[descriptor_index].generations[image_index];

        // Check if the descriptor needs updating first.
        if (t && (*descriptor_generation != t->generation || *descriptor_generation == INVALID_ID)) {
            vulkan_texture_data* internal_data = (vulkan_texture_data*)t->internal_data;

            // Assign view and sampler.
            image_infos[sampler_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_infos[sampler_index].imageView = internal_data->image.view;
            image_infos[sampler_index].sampler = internal_data->sampler;

            VkWriteDescriptorSet descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            descriptor.dstSet = object_descriptor_set;
            descriptor.dstBinding = descriptor_index;
            descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptor.descriptorCount = 1;
            descriptor.pImageInfo = &image_infos[sampler_index];

            descriptor_writes[descriptor_count] = descriptor;
            descriptor_count++;

            // Sync frame generation if not using a default texture.
            if (t->generation != INVALID_ID) {
                *descriptor_generation = t->generation;
            }
            descriptor_index++;
        }
    }

    if (descriptor_count > 0) {
        vkUpdateDescriptorSets(context->device.logical_device, descriptor_count, descriptor_writes, 0, 0);
    }

    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline.pipeline_layout, 1, 1, &object_descriptor_set, 0, 0);
}

b8 vulkan_object_shader_acquire_resources(vulkan_context* context, struct vulkan_object_shader* shader, u32* out_object_id) {
    // TODO: free list
    *out_object_id = shader->object_uniform_buffer_index;
    shader->object_uniform_buffer_index++;

    u32 object_id = *out_object_id;
    vulkan_object_shader_object_state* object_state = &shader->object_states[object_id];
    for (u32 i = 0; i < VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT; ++i) {
        for (u32 j = 0; j < 3; ++j) {
            object_state->descriptor_states[i].generations[j] = INVALID_ID;
        }
    }

    // Allocate descriptor sets.
    VkDescriptorSetLayout layouts[3] = {
        shader->object_descriptor_set_layout,
        shader->object_descriptor_set_layout,
        shader->object_descriptor_set_layout};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = shader->object_descriptor_pool;
    alloc_info.descriptorSetCount = 3;  // one per frame
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, object_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating descriptor sets in shader!");
        return false;
    }

    return true;
}

void vulkan_object_shader_release_resources(vulkan_context* context, struct vulkan_object_shader* shader, u32 object_id) {
    vulkan_object_shader_object_state* object_state = &shader->object_states[object_id];

    const u32 descriptor_set_count = 3;
    // Release object descriptor sets.
    VkResult result = vkFreeDescriptorSets(context->device.logical_device, shader->object_descriptor_pool, descriptor_set_count, object_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error freeing object shader descriptor sets!");
    }

    for (u32 i = 0; i < VULKAN_OBJECT_SHADER_DESCRIPTOR_COUNT; ++i) {
        for (u32 j = 0; j < 3; ++j) {
            object_state->descriptor_states[i].generations[j] = INVALID_ID;
        }
    }

    // TODO: add the object_id to the free list
}