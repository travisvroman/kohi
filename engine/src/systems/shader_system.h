

#pragma once

#include "defines.h"
#include "renderer/renderer_types.inl"
#include "containers/hashtable.h"

typedef struct shader_system_config {
    u16 max_shader_count;
    u8 max_uniform_count;
    u8 max_global_textures;
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
 *
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

typedef struct shader_attribute {
    char* name;
    shader_attribute_type type;
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

    void* internal_data;
} shader;

b8 shader_system_initialize(u64* memory_requirement, void* memory, shader_system_config config);
void shader_system_shutdown(void* state);

KAPI b8 shader_system_create(const shader_config* config);

KAPI u32 shader_system_get_id(const char* shader_name);
KAPI shader* shader_system_get_by_id(u32 shader_id);
KAPI shader* shader_system_get(const char* shader_name);

KAPI b8 shader_system_use(const char* shader_name);

KAPI u16 shader_system_uniform_index(const char* uniform_name);
KAPI b8 shader_system_uniform_set(const char* uniform_name, const void* value);
KAPI b8 shader_system_sampler_set(const char* sampler_name, const texture* t);

KAPI b8 shader_system_uniform_set_by_index(u16 location, const void* value);
KAPI b8 shader_system_sampler_set_by_index(u16 location, const struct texture* t);

KAPI b8 shader_system_apply_global();
KAPI b8 shader_system_apply_instance();

KAPI b8 shader_system_bind_instance(u32 instance_id);
