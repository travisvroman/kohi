#include "vulkan_shader.h"

#include "vulkan_utils.h"
#include "vulkan_pipeline.h"
#include "vulkan_buffer.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "systems/resource_system.h"
#include "systems/texture_system.h"

// The index of the global descriptor set.
const u32 DESC_SET_INDEX_GLOBAL = 0;
// The index of the instance descriptor set.
const u32 DESC_SET_INDEX_INSTANCE = 1;

// The index of the UBO binding.
const u32 BINDING_INDEX_UBO = 0;

// The index of the image sampler binding.
const u32 BINDING_INDEX_SAMPLER = 1;

/** @brief Destroys the shader and returns false. */
#define FAIL_DESTROY(shader)       \
    vulkan_shader_destroy(shader); \
    return false;

b8 create_module(vulkan_shader* shader, vulkan_shader_stage_config config, vulkan_shader_stage* shader_stage);
b8 uniform_name_valid(vulkan_shader* shader, const char* uniform_name);
b8 shader_uniform_add_state_valid(vulkan_shader* shader);
b8 uniform_add(vulkan_shader* shader, const char* uniform_name, u32 size, vulkan_shader_scope scope, u32* out_location, b8 is_sampler);

b8 vulkan_shader_create(vulkan_context* context, const char* name, vulkan_renderpass* renderpass, VkShaderStageFlags stages, u32 max_descriptor_count, b8 use_instances, b8 use_local, vulkan_shader* out_shader) {
    if (!context || !name || !out_shader) {
        KERROR("vulkan_shader_create must supply valid pointer to context, name and out_shader. Creation failed.");
        return false;
    }
    if (stages == 0) {
        KERROR("vulkan_shader_create stages must be nonzero.");
        return false;
    }

    // Zero out the entire structure
    kzero_memory(out_shader, sizeof(vulkan_shader));
    out_shader->state = VULKAN_SHADER_STATE_NOT_CREATED;
    // Take a copy of the pointer to the context.
    out_shader->context = context;
    // Take a copy of the name.
    out_shader->name = string_duplicate(name);
    out_shader->use_instances = use_instances;
    out_shader->use_push_constants = use_local;
    out_shader->renderpass = renderpass;
    out_shader->config.attribute_stride = 0;
    out_shader->config.push_constant_range_count = 0;
    kzero_memory(out_shader->config.push_constant_ranges, sizeof(range) * VULKAN_SHADER_MAX_PUSH_CONST_RANGES);
    out_shader->bound_instance_id = INVALID_ID;

    // Build out the configuration.
    out_shader->config.max_descriptor_set_count = max_descriptor_count;

    // Shader stages. Parse out the flags.
    kzero_memory(out_shader->config.stages, sizeof(vulkan_shader_stage_config) * VULKAN_SHADER_MAX_STAGES);
    out_shader->config.stage_count = 0;
    for (u32 i = VK_SHADER_STAGE_VERTEX_BIT; i < VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM; i <<= 1) {  // shift left/double each time.
        if ((stages & i) == i) {
            vulkan_shader_stage_config stage_config;
            switch (i) {
                // Check for a supported type. Unsupported types are ignored.
                // Note that this prioritizes stages based on the order of the enum,
                // but that should be fine.
                case VK_SHADER_STAGE_VERTEX_BIT:
                    string_ncopy(stage_config.stage_str, "vert", 7);
                    break;
                case VK_SHADER_STAGE_FRAGMENT_BIT:
                    string_ncopy(stage_config.stage_str, "frag", 7);
                    break;

                default:
                    KERROR("vulkan_shader_create: Unsupported shader stage flagged: %d. Stage ignored.", i);
                    // Go to the next type.
                    continue;
            }
            stage_config.stage = i;
            if (out_shader->config.stage_count + 1 > VULKAN_SHADER_MAX_STAGES) {
                KERROR("Shaders may have a maximum of %d stages", VULKAN_SHADER_MAX_STAGES);
                return false;
            }
            // Add the stage.
            out_shader->config.stages[out_shader->config.stage_count] = stage_config;
            out_shader->config.stage_count++;
        }
    }

    // Zero out arrays and counts.
    kzero_memory(out_shader->config.descriptor_sets, sizeof(vulkan_descriptor_set_config) * 2);
    // Global textures array.
    kzero_memory(out_shader->global_textures, sizeof(texture*) * VULKAN_SHADER_MAX_GLOBAL_TEXTURES);
    out_shader->global_texture_count = 0;
    // Attributes array.
    kzero_memory(out_shader->config.attributes, sizeof(VkVertexInputAttributeDescription) * VULKAN_SHADER_MAX_ATTRIBUTES);
    out_shader->config.attribute_count = 0;
    // Uniforms array.
    kzero_memory(out_shader->uniforms, sizeof(vulkan_uniform_lookup_entry) * VULKAN_SHADER_MAX_UNIFORMS);
    out_shader->uniform_count = 0;

    // Create a hashtable to store uniform array indexes. This provides a direct index into the
    // 'uniforms' array stored in the shader for quick lookups by name.
    u64 element_size = sizeof(u32);  // Indexes are stored as u32s.
    u64 element_count = 1024;        // This is more uniforms than we will ever need, but a bigger table reduces collision chance.
    out_shader->hashtable_block = kallocate(element_size * element_count, MEMORY_TAG_UNKNOWN);
    hashtable_create(element_size, element_count, out_shader->hashtable_block, false, &out_shader->uniform_lookup);

    // Invalidate all spots in the hashtable.
    u32 invalid = INVALID_ID;
    hashtable_fill(&out_shader->uniform_lookup, &invalid);

    // A running total of the actual global uniform buffer object size.
    out_shader->global_ubo_size = 0;
    // A running total of the actual instance uniform buffer object size.
    out_shader->ubo_size = 0;
    // NOTE: This is to fit the lowest common denominator in that some nVidia GPUs require
    // a 256-byte stride (or offset) for uniform buffers.
    // TODO: Enhance this to adjust to the actual GPU's capabilities in the future to save where we can.
    out_shader->required_ubo_alignment = 256;

    // This is hard-coded because the Vulkan spec only guarantees that a _minimum_ 128 bytes of space are available,
    // and it's up to the driver to determine how much is available. Therefore, to avoid complexity, only the
    // lowest common denominator of 128B will be used.
    out_shader->push_constant_stride = 128;
    out_shader->push_constant_size = 0;

    // For now, shaders will only ever have these 2 types of descriptor pools.
    out_shader->config.pool_sizes[0] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024};          // HACK: max number of ubo descriptor sets.
    out_shader->config.pool_sizes[1] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096};  // HACK: max number of image sampler descriptor sets.

    // Global descriptor set config.
    vulkan_descriptor_set_config global_descriptor_set_config = {};

    // UBO is always available and first.
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].binding = BINDING_INDEX_UBO;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorCount = 1;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    global_descriptor_set_config.binding_count++;

    out_shader->config.descriptor_sets[DESC_SET_INDEX_GLOBAL] = global_descriptor_set_config;
    out_shader->config.descriptor_set_count++;
    if (out_shader->use_instances) {
        // If using instances, add a second descriptor set.
        vulkan_descriptor_set_config instance_descriptor_set_config = {};

        // Add a UBO to it, as instances should always have one available.
        // NOTE: Might be a good idea to only add this if it is going to be used...
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].binding = BINDING_INDEX_UBO;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorCount = 1;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        instance_descriptor_set_config.binding_count++;

        out_shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE] = instance_descriptor_set_config;
        out_shader->config.descriptor_set_count++;
    }

    // Invalidate all instance states.
    // TODO: dynamic
    for (u32 i = 0; i < 1024; ++i) {
        out_shader->instance_states[i].id = INVALID_ID;
    }

    // Ready to be initialized.
    out_shader->state = VULKAN_SHADER_STATE_UNINITIALIZED;

    return true;
}

b8 vulkan_shader_destroy(vulkan_shader* shader) {
    if (!shader) {
        KERROR("vulkan_shader_destroy requires a valid pointer to a shader.");
        return false;
    }

    VkDevice logical_device = shader->context->device.logical_device;
    VkAllocationCallbacks* vk_allocator = shader->context->allocator;

    // Set it to be unusable right away.
    shader->state = VULKAN_SHADER_STATE_NOT_CREATED;

    // Free the name.
    u32 length = string_length(shader->name);
    kfree(shader->name, length + 1, MEMORY_TAG_STRING);
    shader->name = 0;

    // Descriptor set layouts.
    for (u32 i = 0; i < shader->config.descriptor_set_count; ++i) {
        if (shader->descriptor_set_layouts[i]) {
            vkDestroyDescriptorSetLayout(logical_device, shader->descriptor_set_layouts[i], vk_allocator);
            shader->descriptor_set_layouts[i] = 0;
        }
    }

    // Descriptor pool
    if (shader->descriptor_pool) {
        vkDestroyDescriptorPool(logical_device, shader->descriptor_pool, vk_allocator);
    }

    // Uniform buffer.
    vulkan_buffer_unlock_memory(shader->context, &shader->uniform_buffer);
    shader->mapped_uniform_buffer_block = 0;
    vulkan_buffer_destroy(shader->context, &shader->uniform_buffer);

    // Pipeline
    vulkan_pipeline_destroy(shader->context, &shader->pipeline);

    // Shader modules
    for (u32 i = 0; i < shader->config.stage_count; ++i) {
        vkDestroyShaderModule(shader->context->device.logical_device, shader->stages[i].handle, shader->context->allocator);
    }

    // Destroy the configuration.
    kzero_memory(&shader->config, sizeof(vulkan_shader_config));

    return true;
}

typedef struct vulkan_format_size {
    VkFormat format;
    u32 size;
} vulkan_format_size;
b8 vulkan_shader_add_attribute(vulkan_shader* shader, const char* name, shader_attribute_type type) {
    if (!shader || !name) {
        KERROR("vulkan_shader_add_attribute requires a valid pointer to a shader and a name.");
        return false;
    }

    // Static lookup table for our types->Vulkan ones.
    static vulkan_format_size* types = 0;
    static vulkan_format_size t[29];
    if (!types) {
        t[SHADER_ATTRIB_TYPE_FLOAT32] = (vulkan_format_size){VK_FORMAT_R32_SFLOAT, 4};
        t[SHADER_ATTRIB_TYPE_FLOAT32_2] = (vulkan_format_size){VK_FORMAT_R32G32_SFLOAT, 8};
        t[SHADER_ATTRIB_TYPE_FLOAT32_3] = (vulkan_format_size){VK_FORMAT_R32G32B32_SFLOAT, 12};
        t[SHADER_ATTRIB_TYPE_FLOAT32_4] = (vulkan_format_size){VK_FORMAT_R32G32B32A32_SFLOAT, 16};
        t[SHADER_ATTRIB_TYPE_INT8] = (vulkan_format_size){VK_FORMAT_R8_SINT, 1};
        t[SHADER_ATTRIB_TYPE_INT8_2] = (vulkan_format_size){VK_FORMAT_R8G8_SINT, 2};
        t[SHADER_ATTRIB_TYPE_INT8_3] = (vulkan_format_size){VK_FORMAT_R8G8B8_SINT, 3};
        t[SHADER_ATTRIB_TYPE_INT8_4] = (vulkan_format_size){VK_FORMAT_R8G8B8A8_SINT, 4};
        t[SHADER_ATTRIB_TYPE_UINT8] = (vulkan_format_size){VK_FORMAT_R8_UINT, 1};
        t[SHADER_ATTRIB_TYPE_UINT8_2] = (vulkan_format_size){VK_FORMAT_R8G8_UINT, 2};
        t[SHADER_ATTRIB_TYPE_UINT8_3] = (vulkan_format_size){VK_FORMAT_R8G8B8_UINT, 3};
        t[SHADER_ATTRIB_TYPE_UINT8_4] = (vulkan_format_size){VK_FORMAT_R8G8B8A8_UINT, 4};
        t[SHADER_ATTRIB_TYPE_INT16] = (vulkan_format_size){VK_FORMAT_R16_SINT, 2};
        t[SHADER_ATTRIB_TYPE_INT16_2] = (vulkan_format_size){VK_FORMAT_R16G16_SINT, 4};
        t[SHADER_ATTRIB_TYPE_INT16_3] = (vulkan_format_size){VK_FORMAT_R16G16B16_SINT, 6};
        t[SHADER_ATTRIB_TYPE_INT16_4] = (vulkan_format_size){VK_FORMAT_R16G16B16A16_SINT, 8};
        t[SHADER_ATTRIB_TYPE_UINT16] = (vulkan_format_size){VK_FORMAT_R16_UINT, 2};
        t[SHADER_ATTRIB_TYPE_UINT16_2] = (vulkan_format_size){VK_FORMAT_R16G16_UINT, 4};
        t[SHADER_ATTRIB_TYPE_UINT16_3] = (vulkan_format_size){VK_FORMAT_R16G16B16_UINT, 6};
        t[SHADER_ATTRIB_TYPE_UINT16_4] = (vulkan_format_size){VK_FORMAT_R16G16B16A16_UINT, 8};
        t[SHADER_ATTRIB_TYPE_INT32] = (vulkan_format_size){VK_FORMAT_R32_SINT, 4};
        t[SHADER_ATTRIB_TYPE_INT32_2] = (vulkan_format_size){VK_FORMAT_R32G32_SINT, 8};
        t[SHADER_ATTRIB_TYPE_INT32_3] = (vulkan_format_size){VK_FORMAT_R32G32B32_SINT, 12};
        t[SHADER_ATTRIB_TYPE_INT32_4] = (vulkan_format_size){VK_FORMAT_R32G32B32A32_SINT, 16};
        t[SHADER_ATTRIB_TYPE_UINT32] = (vulkan_format_size){VK_FORMAT_R32_UINT, 4};
        t[SHADER_ATTRIB_TYPE_UINT32_2] = (vulkan_format_size){VK_FORMAT_R32G32_UINT, 8};
        t[SHADER_ATTRIB_TYPE_UINT32_3] = (vulkan_format_size){VK_FORMAT_R32G32B32_UINT, 12};
        t[SHADER_ATTRIB_TYPE_UINT32_4] = (vulkan_format_size){VK_FORMAT_R32G32B32A32_UINT, 16};

        types = t;
    }

    // Setup the new attribute.
    VkVertexInputAttributeDescription attribute;
    attribute.location = shader->config.attribute_count;  // Location is simply the current number of elements before adding the attribute.
    attribute.binding = 0;                                // TODO: should match the binding description.
    attribute.offset = shader->config.attribute_stride;   // Offset is the current stride before adding the new attribute.
    attribute.format = types[type].format;

    // Push into the config's attribute collection and add to the stride.
    shader->config.attributes[shader->config.attribute_count] = attribute;
    shader->config.attribute_count++;
    shader->config.attribute_stride += types[type].size;

    return true;
}

b8 vulkan_shader_add_sampler(vulkan_shader* shader, const char* sampler_name, vulkan_shader_scope scope, u32* out_location) {
    if (scope == VULKAN_SHADER_SCOPE_INSTANCE && !shader->use_instances) {
        KERROR("vulkan_shader_add_sampler cannot add an instance sampler for a shader that does not use instances.");
        return false;
    }

    // Samples can't be used for push constants.
    if (scope == VULKAN_SHADER_SCOPE_LOCAL) {
        KERROR("vulkan_shader_add_sampler cannot add a sampler at local scope.");
        return false;
    }

    // Verify the name is valid and unique.
    if (!uniform_name_valid(shader, sampler_name) || !shader_uniform_add_state_valid(shader)) {
        return false;
    }

    const u32 set_index = (scope == VULKAN_SHADER_SCOPE_GLOBAL ? DESC_SET_INDEX_GLOBAL : DESC_SET_INDEX_INSTANCE);
    vulkan_descriptor_set_config* set_config = &shader->config.descriptor_sets[set_index];
    if (set_config->binding_count < 2) {
        // There isn't a binding yet, meaning this is the first sampler to be added.
        // Create the binding with a single descriptor for this sampler.
        set_config->bindings[BINDING_INDEX_SAMPLER].binding = BINDING_INDEX_SAMPLER;  // Always going to be the second one.
        set_config->bindings[BINDING_INDEX_SAMPLER].descriptorCount = 1;              // Default to 1, will increase with each sampler added to the appropriate level.
        set_config->bindings[BINDING_INDEX_SAMPLER].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        set_config->bindings[BINDING_INDEX_SAMPLER].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        set_config->binding_count++;

        // This is the first sampler descriptor.
        *out_location = 0;
    } else {
        // There is already a binding for samplers, so just add a descriptor to it.
        // Take the current descriptor count as the location and increment the number of descriptors.
        *out_location = set_config->bindings[BINDING_INDEX_SAMPLER].descriptorCount;
        set_config->bindings[BINDING_INDEX_SAMPLER].descriptorCount++;
    }

    // If global, push into the global list.
    if (scope == VULKAN_SHADER_SCOPE_GLOBAL) {
        shader->global_textures[shader->global_texture_count] = texture_system_get_default_texture();
        shader->global_texture_count++;
    } else {
        // Otherwise, it's instance-level, so keep count of how many need to be added during the resource acquisition.
        shader->instance_texture_count++;
    }

    // Treat it like a uniform. NOTE: In the case of samplers, out_location is used to determine the
    // hashtable entry's 'location' field value directly, and is then set to the index of the uniform array.
    // This allows location lookups for samplers as if they were uniforms as well (since technically they are).
    if (!uniform_add(shader, sampler_name, 0, scope, out_location, true)) {
        KERROR("Unable to add sampler uniform.");
        return false;
    }

    return true;
}

// Verify shader state, output pointer and uniform name are all valid.
#define VERIFY_UNIFORM(shader, uniform_name, out_location) \
    if (!out_location || !shader_uniform_add_state_valid(shader) || !uniform_name_valid(shader, uniform_name)) return false;

b8 vulkan_shader_add_uniform_i8(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(i8), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_i16(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(i16), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_i32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(i32), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_u8(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(u8), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_u16(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(u16), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_u32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(u32), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_f32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(f32), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_vec2(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(vec2), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_vec3(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(vec3), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_vec4(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(vec4), scope, out_location, false);
}
b8 vulkan_shader_add_uniform_mat4(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location) {
    VERIFY_UNIFORM(shader, uniform_name, out_location);
    return uniform_add(shader, uniform_name, sizeof(mat4), scope, out_location, false);
}

b8 vulkan_shader_initialize(vulkan_shader* shader) {
    if (!shader) {
        KERROR("vulkan_shader_initialize requires a valid pointer to a shader.");
        return false;
    }
    vulkan_context* context = shader->context;
    VkDevice logical_device = context->device.logical_device;
    VkAllocationCallbacks* vk_allocator = context->allocator;

    // Create a module for each stage.
    kzero_memory(shader->stages, sizeof(vulkan_shader_stage) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < shader->config.stage_count; ++i) {
        if (!create_module(shader, shader->config.stages[i], &shader->stages[i])) {
            KERROR("Unable to create %s shader module for '%s'. Shader will be destroyed.", shader->config.stages[i].stage_str, shader->name);
            FAIL_DESTROY(shader);
        }
    }

    // Descriptor pool.
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = shader->config.pool_sizes;
    pool_info.maxSets = shader->config.max_descriptor_set_count;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    // Create descriptor pool.
    VkResult result = vkCreateDescriptorPool(logical_device, &pool_info, vk_allocator, &shader->descriptor_pool);
    if (!vulkan_result_is_success(result)) {
        KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
        FAIL_DESTROY(shader);
    }

    // Create descriptor set layouts.
    kzero_memory(shader->descriptor_set_layouts, shader->config.descriptor_set_count);
    for (u32 i = 0; i < shader->config.descriptor_set_count; ++i) {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = shader->config.descriptor_sets[i].binding_count;
        layout_info.pBindings = shader->config.descriptor_sets[i].bindings;
        result = vkCreateDescriptorSetLayout(logical_device, &layout_info, vk_allocator, &shader->descriptor_set_layouts[i]);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
            FAIL_DESTROY(shader);
        }
    }

    // TODO: This feels wrong to have these here, at least in this fashion. Should probably
    // Be configured to pull from someplace instead.
    // Viewport.
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)shader->context->framebuffer_height;
    viewport.width = (f32)shader->context->framebuffer_width;
    viewport.height = -(f32)shader->context->framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = shader->context->framebuffer_width;
    scissor.extent.height = shader->context->framebuffer_height;

    VkPipelineShaderStageCreateInfo stage_create_infos[VULKAN_SHADER_MAX_STAGES];
    kzero_memory(stage_create_infos, sizeof(VkPipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < shader->config.stage_count; ++i) {
        stage_create_infos[i] = shader->stages[i].shader_stage_create_info;
    }

    b8 pipeline_result = vulkan_graphics_pipeline_create(
        context,
        shader->renderpass,
        shader->config.attribute_stride,
        shader->config.attribute_count,
        shader->config.attributes,
        shader->config.descriptor_set_count,
        shader->descriptor_set_layouts,
        shader->config.stage_count,
        stage_create_infos,
        viewport,
        scissor,
        false,
        true,
        shader->config.push_constant_range_count,
        shader->config.push_constant_ranges,
        &shader->pipeline);

    if (!pipeline_result) {
        KERROR("Failed to load graphics pipeline for object shader.");
        return false;
    }

    // Get the closest valid stride (instance)
    shader->global_ubo_stride = 0;
    while (shader->global_ubo_stride < shader->global_ubo_size) {
        shader->global_ubo_stride += shader->required_ubo_alignment;
    }

    // Get the closest valid stride (instance)
    if (shader->use_instances) {
        shader->ubo_stride = 0;
        while (shader->ubo_stride < shader->ubo_size) {
            shader->ubo_stride += shader->required_ubo_alignment;
        }
    }

    // Uniform  buffer.
    u32 device_local_bits = context->device.supports_device_local_host_visible ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0;
    // TODO: max count should be configurable, or perhaps long term support of buffer resizing.
    u64 total_buffer_size = shader->global_ubo_stride + (shader->ubo_stride * VULKAN_MAX_MATERIAL_COUNT);  // global + (locals)
    if (!vulkan_buffer_create(
            context,
            total_buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | device_local_bits,
            true,
            &shader->uniform_buffer)) {
        KERROR("Vulkan buffer creation failed for object shader.");
        return false;
    }

    // Allocate space for the global UBO, whcih should occupy the _stride_ space, _not_ the actual size used.
    if (!vulkan_buffer_allocate(&shader->uniform_buffer, shader->global_ubo_stride, &shader->global_ubo_offset)) {
        KERROR("Failed to allocate space for the uniform buffer!");
        return false;
    }

    // Map the entire buffer's memory.
    shader->mapped_uniform_buffer_block = vulkan_buffer_lock_memory(shader->context, &shader->uniform_buffer, 0, total_buffer_size, 0);

    // Allocate global descriptor sets, one per frame. Global is always the first set.
    VkDescriptorSetLayout global_layouts[3] = {
        shader->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL],
        shader->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL],
        shader->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL]};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = shader->descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = global_layouts;
    VK_CHECK(vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, shader->global_descriptor_sets));

    shader->state = VULKAN_SHADER_STATE_INITIALIZED;
    return true;
}

b8 vulkan_shader_use(vulkan_shader* shader) {
    u32 image_index = shader->context->image_index;
    vulkan_pipeline_bind(&shader->context->graphics_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, &shader->pipeline);
    return true;
}

b8 vulkan_shader_bind_globals(vulkan_shader* shader) {
    if (!shader) {
        return false;
    }

    // Global UBO is always at the beginning, but use this anyway.
    shader->bound_ubo_offset = shader->global_ubo_offset;
    return true;
}

b8 vulkan_shader_bind_instance(vulkan_shader* shader, u32 instance_id) {
    if (!shader) {
        KERROR("vulkan_shader_bind_instance requires a valid pointer to a shader.");
        return false;
    }

    shader->bound_instance_id = instance_id;
    vulkan_shader_instance_state* object_state = &shader->instance_states[instance_id];
    shader->bound_ubo_offset = object_state->offset;
    return true;
}

b8 vulkan_shader_apply_globals(vulkan_shader* shader) {
    vulkan_context* context = shader->context;
    u32 image_index = context->image_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;
    VkDescriptorSet global_descriptor = shader->global_descriptor_sets[image_index];

    // Apply UBO first
    VkDescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = shader->uniform_buffer.handle;
    bufferInfo.offset = shader->global_ubo_offset;
    bufferInfo.range = shader->global_ubo_stride;

    // Update descriptor sets.
    VkWriteDescriptorSet ubo_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ubo_write.dstSet = shader->global_descriptor_sets[image_index];
    ubo_write.dstBinding = 0;
    ubo_write.dstArrayElement = 0;
    ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_write.descriptorCount = 1;
    ubo_write.pBufferInfo = &bufferInfo;

    VkWriteDescriptorSet descriptor_writes[2];
    descriptor_writes[0] = ubo_write;

    u32 global_set_binding_count = shader->config.descriptor_sets[DESC_SET_INDEX_GLOBAL].binding_count;
    if (global_set_binding_count > 1) {
        // TODO: There are samplers to be written. Support this.
        global_set_binding_count = 1;
        KERROR("Global image samplers are not yet supported.");

        // VkWriteDescriptorSet sampler_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        // descriptor_writes[1] = ...
    }

    vkUpdateDescriptorSets(context->device.logical_device, global_set_binding_count, descriptor_writes, 0, 0);

    // Bind the global descriptor set to be updated.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline.pipeline_layout, 0, 1, &global_descriptor, 0, 0);

    return true;
}

b8 vulkan_shader_apply_instance(vulkan_shader* shader) {
    if (!shader->use_instances) {
        KERROR("This shader does not use instances.");
        return false;
    }
    vulkan_context* context = shader->context;
    u32 image_index = context->image_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;

    // Obtain instance data.
    vulkan_shader_instance_state* object_state = &shader->instance_states[shader->bound_instance_id];
    VkDescriptorSet object_descriptor_set = object_state->descriptor_set_state.descriptor_sets[image_index];

    // TODO: if needs update
    VkWriteDescriptorSet descriptor_writes[2];  // Always a max of 2 descriptor sets.
    kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * 2);
    u32 descriptor_count = 0;
    u32 descriptor_index = 0;

    // Descriptor 0 - Uniform buffer
    // Only do this if the descriptor has not yet been updated.
    u32* instance_ubo_generation = &(object_state->descriptor_set_state.descriptor_states[descriptor_index].generations[image_index]);
    // TODO: determine if update is required.
    if (*instance_ubo_generation == INVALID_ID /*|| *global_ubo_generation != material->generation*/) {
        VkDescriptorBufferInfo buffer_info;
        buffer_info.buffer = shader->uniform_buffer.handle;
        buffer_info.offset = object_state->offset;
        buffer_info.range = shader->ubo_stride;

        VkWriteDescriptorSet ubo_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        ubo_descriptor.dstSet = object_descriptor_set;
        ubo_descriptor.dstBinding = descriptor_index;
        ubo_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_descriptor.descriptorCount = 1;
        ubo_descriptor.pBufferInfo = &buffer_info;

        descriptor_writes[descriptor_count] = ubo_descriptor;
        descriptor_count++;

        // Update the frame generation. In this case it is only needed once since this is a buffer.
        *instance_ubo_generation = 1;  // material->generation; TODO: some generation from... somewhere
    }
    descriptor_index++;

    // Samplers will always be in the binding. If the binding count is less than 2, there are no samplers.
    if (shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].binding_count > 1) {
        // Iterate samplers.
        u32 total_sampler_count = shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].bindings[BINDING_INDEX_SAMPLER].descriptorCount;
        u32 update_sampler_count = 0;
        VkDescriptorImageInfo image_infos[VULKAN_SHADER_MAX_GLOBAL_TEXTURES];
        for (u32 i = 0; i < total_sampler_count; ++i) {
            // TODO: only update in the list if actually needing an update.
            texture* t = shader->instance_states[shader->bound_instance_id].instance_textures[i];
            vulkan_texture_data* internal_data = (vulkan_texture_data*)t->internal_data;
            image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_infos[i].imageView = internal_data->image.view;
            image_infos[i].sampler = internal_data->sampler;

            // TODO: change up descriptor state to handle this properly.
            // Sync frame generation if not using a default texture.
            // if (t->generation != INVALID_ID) {
            //     *descriptor_generation = t->generation;
            //     *descriptor_id = t->id;
            // }

            update_sampler_count++;
        }

        VkWriteDescriptorSet sampler_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        sampler_descriptor.dstSet = object_descriptor_set;
        sampler_descriptor.dstBinding = descriptor_index;
        sampler_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_descriptor.descriptorCount = update_sampler_count;
        sampler_descriptor.pImageInfo = image_infos;

        descriptor_writes[descriptor_count] = sampler_descriptor;
        descriptor_count++;
    }

    if (descriptor_count > 0) {
        vkUpdateDescriptorSets(context->device.logical_device, descriptor_count, descriptor_writes, 0, 0);
    }

    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline.pipeline_layout, 1, 1, &object_descriptor_set, 0, 0);

    return true;
}

b8 vulkan_shader_acquire_instance_resources(vulkan_shader* shader, u32* out_instance_id) {
    // TODO: dynamic
    *out_instance_id = INVALID_ID;
    for (u32 i = 0; i < 1024; ++i) {
        if (shader->instance_states[i].id == INVALID_ID) {
            shader->instance_states[i].id = i;
            *out_instance_id = i;
            break;
        }
    }
    if (*out_instance_id == INVALID_ID) {
        KERROR("vulkan_shader_acquire_instance_resources failed to acquire new id");
        return false;
    }

    vulkan_shader_instance_state* instance_state = &shader->instance_states[*out_instance_id];
    u32 instance_texture_count = shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].bindings[BINDING_INDEX_SAMPLER].descriptorCount;
    // Wipe out the memory for the entire array, even if it isn't all used.
    kzero_memory(instance_state->instance_textures, sizeof(texture*) * VULKAN_SHADER_MAX_INSTANCE_TEXTURES);
    texture* default_texture = texture_system_get_default_texture();
    // Set all the texture pointers to default until assigned.
    for (u32 i = 0; i < instance_texture_count; ++i) {
        instance_state->instance_textures[i] = default_texture;
    }

    // Allocate some space in the UBO - by the stride, not the size.
    u64 size = shader->ubo_stride;
    if (!vulkan_buffer_allocate(&shader->uniform_buffer, size, &instance_state->offset)) {
        KERROR("vulkan_material_shader_acquire_resources failed to acquire ubo space");
        return false;
    }

    vulkan_shader_descriptor_set_state* set_state = &instance_state->descriptor_set_state;

    // Each descriptor binding in the set
    u32 binding_count = shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].binding_count;
    kzero_memory(set_state->descriptor_states, sizeof(vulkan_descriptor_state) * VULKAN_SHADER_MAX_BINDINGS);
    for (u32 i = 0; i < binding_count; ++i) {
        for (u32 j = 0; j < 3; ++j) {
            set_state->descriptor_states[i].generations[j] = INVALID_ID;
            set_state->descriptor_states[i].ids[j] = INVALID_ID;
        }
    }

    // Allocate 3 descriptor sets (one per frame).
    VkDescriptorSetLayout layouts[3] = {
        shader->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE],
        shader->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE],
        shader->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE]};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = shader->descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(
        shader->context->device.logical_device,
        &alloc_info,
        instance_state->descriptor_set_state.descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating instance descriptor sets in shader: '%s'.", vulkan_result_string(result, true));
        return false;
    }

    return true;
}
b8 vulkan_shader_release_instance_resources(vulkan_shader* shader, u32 instance_id) {
    vulkan_shader_instance_state* instance_state = &shader->instance_states[instance_id];

    // Wait for any pending operations using the descriptor set to finish.
    vkDeviceWaitIdle(shader->context->device.logical_device);

    // Free 3 descriptor sets (one per frame)
    VkResult result = vkFreeDescriptorSets(
        shader->context->device.logical_device,
        shader->descriptor_pool,
        3,
        instance_state->descriptor_set_state.descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error freeing object shader descriptor sets!");
    }

    // Destroy descriptor states.
    kzero_memory(instance_state->descriptor_set_state.descriptor_states, sizeof(vulkan_descriptor_state) * VULKAN_SHADER_MAX_BINDINGS);

    kzero_memory(instance_state->instance_textures, sizeof(texture*) * VULKAN_SHADER_MAX_INSTANCE_TEXTURES);

    vulkan_buffer_free(&shader->uniform_buffer, shader->ubo_stride, instance_state->offset);
    instance_state->offset = INVALID_ID;
    instance_state->id = INVALID_ID;

    return true;
}

b8 vulkan_shader_set_sampler(vulkan_shader* shader, u32 location, texture* t) {
    vulkan_uniform_lookup_entry* entry = &shader->uniforms[location];
    if (entry->scope == VULKAN_SHADER_SCOPE_GLOBAL) {
        shader->global_textures[entry->location] = t;
    } else {
        shader->instance_states[shader->bound_instance_id].instance_textures[entry->location] = t;
    }

    return true;
}

u32 vulkan_shader_uniform_location(vulkan_shader* shader, const char* uniform_name) {
    u32 location = INVALID_ID;
    if (!hashtable_get(&shader->uniform_lookup, uniform_name, &location) || location == INVALID_ID) {
        KERROR("Shader '%s' does not have a registered uniform named '%s'", shader->name, uniform_name);
        return INVALID_ID;
    }
    return location;
}

b8 check_uniform_size(vulkan_shader* shader, u32 location, u32 expected_size) {
    vulkan_uniform_lookup_entry* entry = &shader->uniforms[location];
    if (entry->size != expected_size) {
        KERROR("Uniform location '%d' on shader '%s' is a different size (%dB) than expected (%dB).", location, shader->name, entry->size, expected_size);
        return false;
    }
    return true;
}

b8 set_uniform(vulkan_shader* shader, u32 location, void* value, u64 size) {
    if (!check_uniform_size(shader, location, size)) {
        return false;
    }
    // Map the appropriate memory location and copy the data over.
    void* block = 0;
    vulkan_uniform_lookup_entry* entry = &shader->uniforms[location];
    if (entry->scope == VULKAN_SHADER_SCOPE_GLOBAL) {
        block = (void*)(shader->mapped_uniform_buffer_block + shader->global_ubo_offset + entry->offset);
    } else if (entry->scope == VULKAN_SHADER_SCOPE_INSTANCE) {
        block = (void*)(shader->mapped_uniform_buffer_block + shader->bound_ubo_offset + entry->offset);
    } else {
        // Is local, using push constants. Do this immediately.
        VkCommandBuffer command_buffer = shader->context->graphics_command_buffers[shader->context->image_index].handle;
        vkCmdPushConstants(command_buffer, shader->pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, entry->offset, entry->size, value);
        return true;
    }
    kcopy_memory(block, value, size);
    return true;
}

b8 vulkan_shader_set_uniform_i8(vulkan_shader* shader, u32 location, i8 value) {
    u32 size = sizeof(i8);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_i16(vulkan_shader* shader, u32 location, i16 value) {
    u32 size = sizeof(i16);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_i32(vulkan_shader* shader, u32 location, i32 value) {
    u32 size = sizeof(i32);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_u8(vulkan_shader* shader, u32 location, u8 value) {
    u32 size = sizeof(u8);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_u16(vulkan_shader* shader, u32 location, u16 value) {
    u32 size = sizeof(u16);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_u32(vulkan_shader* shader, u32 location, u32 value) {
    u32 size = sizeof(u32);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_f32(vulkan_shader* shader, u32 location, f32 value) {
    u32 size = sizeof(f32);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec2(vulkan_shader* shader, u32 location, vec2 value) {
    u32 size = sizeof(vec2);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec2f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1) {
    u32 size = sizeof(vec2);
    vec2 value = (vec2){value_0, value_1};
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec3(vulkan_shader* shader, u32 location, vec3 value) {
    u32 size = sizeof(vec3);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec3f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2) {
    u32 size = sizeof(vec3);
    vec3 value = (vec3){value_0, value_1, value_2};
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec4(vulkan_shader* shader, u32 location, vec4 value) {
    u32 size = sizeof(vec4);
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_vec4f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3) {
    u32 size = sizeof(vec4);
    vec4 value = (vec4){value_0, value_1, value_2, value_3};
    return set_uniform(shader, location, &value, size);
}
b8 vulkan_shader_set_uniform_mat4(vulkan_shader* shader, u32 location, mat4 value) {
    u32 size = sizeof(mat4);
    return set_uniform(shader, location, &value, size);
}

b8 create_module(vulkan_shader* shader, vulkan_shader_stage_config config, vulkan_shader_stage* shader_stage) {
    // Build file name, which will also be used as the resource name.
    char file_name[512];
    string_format(file_name, "shaders/%s.%s.spv", shader->name, config.stage_str);

    // Read the resource.
    resource binary_resource;
    if (!resource_system_load(file_name, RESOURCE_TYPE_BINARY, &binary_resource)) {
        KERROR("Unable to read shader module: %s.", file_name);
        return false;
    }

    kzero_memory(&shader_stage->create_info, sizeof(VkShaderModuleCreateInfo));
    shader_stage->create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Use the resource's size and data directly.
    shader_stage->create_info.codeSize = binary_resource.data_size;
    shader_stage->create_info.pCode = (u32*)binary_resource.data;

    VK_CHECK(vkCreateShaderModule(
        shader->context->device.logical_device,
        &shader_stage->create_info,
        shader->context->allocator,
        &shader_stage->handle));

    // Release the resource.
    resource_system_unload(&binary_resource);

    // Shader stage info
    kzero_memory(&shader_stage->shader_stage_create_info, sizeof(VkPipelineShaderStageCreateInfo));
    shader_stage->shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage->shader_stage_create_info.stage = config.stage;
    shader_stage->shader_stage_create_info.module = shader_stage->handle;
    shader_stage->shader_stage_create_info.pName = "main";

    return true;
}

b8 uniform_name_valid(vulkan_shader* shader, const char* uniform_name) {
    if (!uniform_name || !string_length(uniform_name)) {
        KERROR("Uniform name must exist.");
        return false;
    }
    u32 location;
    if (hashtable_get(&shader->uniform_lookup, uniform_name, &location) && location != INVALID_ID) {
        KERROR("A uniform by the name '%s' already exists on shader '%s'.", uniform_name, shader->name);
        return false;
    }
    return true;
}

b8 shader_uniform_add_state_valid(vulkan_shader* shader) {
    if (shader->state != VULKAN_SHADER_STATE_UNINITIALIZED) {
        KERROR("Uniforms may only be added to shaders before initialization.");
        return false;
    }
    return true;
}

b8 uniform_add(vulkan_shader* shader, const char* uniform_name, u32 size, vulkan_shader_scope scope, u32* out_location, b8 is_sampler) {
    if (shader->uniform_count + 1 > VULKAN_SHADER_MAX_UNIFORMS) {
        KERROR("A shader can only accept a combined maximum of %d uniforms and samplers at global, instance and local scopes.");
        return false;
    }
    vulkan_uniform_lookup_entry entry;
    entry.index = shader->uniform_count;  // Index is saved to the hashtable for lookups.
    entry.scope = scope;
    b8 is_global = (scope == VULKAN_SHADER_SCOPE_GLOBAL);
    if (is_sampler) {
        // Just use the passed in location
        entry.location = *out_location;
    } else {
        entry.location = entry.index;
    }

    if (scope != VULKAN_SHADER_SCOPE_LOCAL) {
        entry.set_index = (u32)scope;
        entry.offset = is_sampler ? 0 : is_global ? shader->global_ubo_size
                                                  : shader->ubo_size;
        entry.size = is_sampler ? 0 : size;
    } else {
        if (entry.scope == VULKAN_SHADER_SCOPE_LOCAL && !shader->use_push_constants) {
            KERROR("Cannot add a locally-scoped uniform for a shader that does not support locals.");
            return false;
        }
        // Push a new aligned range (align to 4, as required by Vulkan spec)
        entry.set_index = INVALID_ID;
        range r = get_aligned_range(shader->push_constant_size, size, 4);
        // utilize the aligned offset/range
        entry.offset = r.offset;
        entry.size = r.size;

        // Track in configuration for use in initialization.
        shader->config.push_constant_ranges[shader->config.push_constant_range_count] = r;
        shader->config.push_constant_range_count++;

        // Increase the push constant's size by the total value.
        shader->push_constant_size += r.size;
    }

    if (!hashtable_set(&shader->uniform_lookup, uniform_name, &entry.index)) {
        KERROR("Failed to add uniform.");
        return false;
    }
    shader->uniforms[shader->uniform_count] = entry;
    shader->uniform_count++;

    if (!is_sampler) {
        if (entry.scope == VULKAN_SHADER_SCOPE_GLOBAL) {
            shader->global_ubo_size += entry.size;
        } else if (entry.scope == VULKAN_SHADER_SCOPE_INSTANCE) {
            shader->ubo_size += entry.size;
        }
    }

    *out_location = entry.index;
    return true;
}