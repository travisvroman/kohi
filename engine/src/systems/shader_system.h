/**
 * @file shader_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system to manage shaders. Respondible for working with the
 * renderer to create, destroy, bind/unbind and set shader properties
 * such as uniforms.
 * @version 1.0
 * @date 2022-03-09
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "defines.h"
#include "renderer/renderer_types.inl"
#include "containers/hashtable.h"

/** @brief Configuration for the shader system. */
typedef struct shader_system_config {
    /** @brief The maximum number of shaders held in the system. NOTE: Should be at least 512. */
    u16 max_shader_count;
    /** @brief The maximum number of uniforms allowed in a single shader. */
    u8 max_uniform_count;
    /** @brief The maximum number of global-scope textures allowed in a single shader. */
    u8 max_global_textures;
    /** @brief The maximum number of instance-scope textures allowed in a single shader. */
    u8 max_instance_textures;
} shader_system_config;

/**
 * @brief Represents the current state of a given shader.
 */
typedef enum shader_state {
    /** @brief The shader has not yet gone through the creation process, and is unusable.*/
    SHADER_STATE_NOT_CREATED,
    /** @brief The shader has gone through the creation process, but not initialization. It is unusable.*/
    SHADER_STATE_UNINITIALIZED,
    /** @brief The shader is created and initialized, and is ready for use.*/
    SHADER_STATE_INITIALIZED,
} shader_state;

/**
 * @brief Represents a single entry in the internal uniform array.
 */
typedef struct shader_uniform {
    /** @brief The offset in bytes from the beginning of the uniform set (global/instance/local) */
    u64 offset;
    /**
     * @brief The location to be used as a lookup. Typically the same as the index except for samplers,
     * which is used to lookup texture index within the internal array at the given scope (global/instance).
     */
    u16 location;
    /** @brief Index into the internal uniform array. */
    u16 index;
    /** @brief The size of the uniform, or 0 for samplers. */
    u16 size;
    /** @brief The index of the descriptor set the uniform belongs to (0=global, 1=instance, INVALID_ID=local). */
    u8 set_index;
    /** @brief The scope of the uniform. */
    shader_scope scope;
    /** @brief The type of uniform. */
    shader_uniform_type type;
} shader_uniform;

/**
 * @brief Represents a single shader vertex attribute.
 */
typedef struct shader_attribute {
    /** @brief The attribute name. */
    char* name;
    /** @brief The attribute type. */
    shader_attribute_type type;
    /** @brief The attribute size in bytes. */
    u32 size;
} shader_attribute;

/**
 * @brief Represents a shader on the frontend.
 */
typedef struct shader {
    /** @brief The shader identifier */
    u32 id;

    char* name;
    /**
     * @brief Indicates if the shader uses instances. If not, it is assumed
     * that only global uniforms and samplers are used.
     */
    b8 use_instances;
    /** @brief Indicates if locals are used (typically for model matrices, etc.).*/
    b8 use_locals;

    /**
     * @brief The amount of bytes that are required for UBO alignment.
     *
     * This is used along with the UBO size to determine the ultimate
     * stride, which is how much the UBOs are spaced out in the buffer.
     * For example, a required alignment of 256 means that the stride
     * must be a multiple of 256 (true for some nVidia cards).
     */
    u64 required_ubo_alignment;

    /** @brief The actual size of the global uniform buffer object. */
    u64 global_ubo_size;
    /** @brief The stride of the global uniform buffer object. */
    u64 global_ubo_stride;
    /**
     * @brief The offset in bytes for the global UBO from the beginning
     * of the uniform buffer.
     */
    u64 global_ubo_offset;

    /** @brief The actual size of the instance uniform buffer object. */
    u64 ubo_size;

    /** @brief The stride of the instance uniform buffer object. */
    u64 ubo_stride;

    /** @brief The total size of all push constant ranges combined. */
    u64 push_constant_size;
    /** @brief The push constant stride, aligned to 4 bytes as required by Vulkan. */
    u64 push_constant_stride;

    /** @brief An array of global texture pointers. Darray */
    texture** global_textures;

    /** @brief The number of instance textures. */
    u8 instance_texture_count;

    shader_scope bound_scope;

    /** @brief The identifier of the currently bound instance. */
    u32 bound_instance_id;
    /** @brief The currently bound instance's ubo offset. */
    u32 bound_ubo_offset;

    /** @brief The block of memory used by the uniform hashtable. */
    void* hashtable_block;
    /** @brief A hashtable to store uniform index/locations by name. */
    hashtable uniform_lookup;

    /** @brief An array of uniforms in this shader. Darray. */
    shader_uniform* uniforms;

    /** @brief An array of attributes. Darray. */
    shader_attribute* attributes;

    /** @brief The internal state of the shader. */
    shader_state state;

    /** @brief The number of push constant ranges. */
    u8 push_constant_range_count;
    /** @brief An array of push constant ranges. */
    range push_constant_ranges[32];
    /** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
    u16 attribute_stride;

    /** @brief An opaque pointer to hold renderer API specific data. Renderer is responsible for creation and destruction of this.  */
    void* internal_data;
} shader;

/**
 * @brief Initializes the shader system using the supplied configuration.
 * NOTE: Call this twice, once to obtain memory requirement (memory = 0) and a second time
 * including allocated memory.
 * 
 * @param memory_requirement A pointer to hold the memory requirement of this system in bytes.
 * @param memory A memory block to be used to hold the state of this system. Pass 0 on the first call to get memory requirement.
 * @param config The configuration to be used when initializing the system.
 * @return b8 True on success; otherwise false.
 */
b8 shader_system_initialize(u64* memory_requirement, void* memory, shader_system_config config);

/**
 * @brief Shuts down the shader system.
 * 
 * @param state A pointer to the system state.
 */
void shader_system_shutdown(void* state);

/**
 * @brief Creates a new shader with the given config.
 * 
 * @param config The configuration to be used when creating the shader.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_create(const shader_config* config);

/**
 * @brief Gets the identifier of a shader by name.
 * 
 * @param shader_name The name of the shader.
 * @return The shader id, if found; otherwise INVALID_ID.
 */
KAPI u32 shader_system_get_id(const char* shader_name);

/**
 * @brief Returns a pointer to a shader with the given identifier.
 * 
 * @param shader_id The shader identifier.
 * @return A pointer to a shader, if found; otherwise 0.
 */
KAPI shader* shader_system_get_by_id(u32 shader_id);

/**
 * @brief Returns a pointer to a shader with the given name.
 * 
 * @param shader_name The name to search for. Case sensitive.
 * @return A pointer to a shader, if found; otherwise 0.
 */
KAPI shader* shader_system_get(const char* shader_name);

/**
 * @brief Uses the shader with the given name.
 * 
 * @param shader_name The name of the shader to use. Case sensitive.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_use(const char* shader_name);

/**
 * @brief Uses the shader with the given identifier.
 * 
 * @param shader_id The identifier of the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_use_by_id(u32 shader_id);

/**
 * @brief Returns the uniform index for a uniform with the given name, if found.
 * 
 * @param s A pointer to the shader to obtain the index from.
 * @param uniform_name The name of the uniform to search for.
 * @return The uniform index, if found; otherwise INVALID_ID_U16.
 */
KAPI u16 shader_system_uniform_index(shader* s, const char* uniform_name);

/**
 * @brief Sets the value of a uniform with the given name to the supplied value.
 * NOTE: Operates against the currently-used shader.
 * 
 * @param uniform_name The name of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set(const char* uniform_name, const void* value);

/**
 * @brief Sets the texture of a sampler with the given name to the supplied texture.
 * NOTE: Operates against the currently-used shader.
 * 
 * @param uniform_name The name of the uniform to be set.
 * @param t A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_sampler_set(const char* sampler_name, const texture* t);

/**
 * @brief Sets a uniform value by index.
 * NOTE: Operates against the currently-used shader.
 * 
 * @param index The index of the uniform.
 * @param value The value of the uniform.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set_by_index(u16 index, const void* value);

/**
 * @brief Sets a sampler value by index.
 * NOTE: Operates against the currently-used shader.
 * 
 * @param index The index of the uniform.
 * @param value A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_sampler_set_by_index(u16 index, const struct texture* t);

/**
 * @brief Applies global-scoped uniforms.
 * NOTE: Operates against the currently-used shader.
 * 
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_apply_global();

/**
 * @brief Applies instance-scoped uniforms.
 * NOTE: Operates against the currently-used shader.
 * @param needs_update Indicates if the shader needs uniform updates or just needs to be bound.
 * 
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_apply_instance(b8 needs_update);

/**
 * @brief Binds the instance with the given id for use. Must be done before setting
 * instance-scoped uniforms.
 * NOTE: Operates against the currently-used shader.
 * 
 * @param instance_id The identifier of the instance to bind.
 * @return True on success; otherwise false. 
 */
KAPI b8 shader_system_bind_instance(u32 instance_id);
