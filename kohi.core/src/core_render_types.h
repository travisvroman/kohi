#pragma once

#include "defines.h"

/** @brief Represents supported texture filtering modes. */
typedef enum texture_filter {
    /** @brief Nearest-neighbor filtering. */
    TEXTURE_FILTER_MODE_NEAREST = 0x0,
    /** @brief Linear (i.e. bilinear) filtering.*/
    TEXTURE_FILTER_MODE_LINEAR = 0x1
} texture_filter;

typedef enum texture_repeat {
    TEXTURE_REPEAT_REPEAT = 0x0,
    TEXTURE_REPEAT_MIRRORED_REPEAT = 0x1,
    TEXTURE_REPEAT_CLAMP_TO_EDGE = 0x2,
    TEXTURE_REPEAT_CLAMP_TO_BORDER = 0x3,
    TEXTURE_REPEAT_COUNT
} texture_repeat;

/** @brief Shader stages available in the system. */
typedef enum shader_stage {
    SHADER_STAGE_VERTEX = 0x00000001,
    SHADER_STAGE_GEOMETRY = 0x00000002,
    SHADER_STAGE_FRAGMENT = 0x00000004,
    SHADER_STAGE_COMPUTE = 0x0000008
} shader_stage;

/**
 * @brief Defines shader scope, which indicates how
 * often it gets updated.
 */
typedef enum shader_scope {
    /** @brief Global shader scope, generally updated once per frame. */
    SHADER_SCOPE_GLOBAL = 0,
    /** @brief Instance shader scope, generally updated "per-instance" of the
       shader. */
    SHADER_SCOPE_INSTANCE = 1,
    /** @brief Local shader scope, generally updated per-object */
    SHADER_SCOPE_LOCAL = 2
} shader_scope;

/** @brief Available attribute types. */
typedef enum shader_attribute_type {
    SHADER_ATTRIB_TYPE_FLOAT32 = 0U,
    SHADER_ATTRIB_TYPE_FLOAT32_2 = 1U,
    SHADER_ATTRIB_TYPE_FLOAT32_3 = 2U,
    SHADER_ATTRIB_TYPE_FLOAT32_4 = 3U,
    SHADER_ATTRIB_TYPE_MATRIX_4 = 4U,
    SHADER_ATTRIB_TYPE_INT8 = 5U,
    SHADER_ATTRIB_TYPE_UINT8 = 6U,
    SHADER_ATTRIB_TYPE_INT16 = 7U,
    SHADER_ATTRIB_TYPE_UINT16 = 8U,
    SHADER_ATTRIB_TYPE_INT32 = 9U,
    SHADER_ATTRIB_TYPE_UINT32 = 10U,
} shader_attribute_type;

/** @brief Available uniform types. */
typedef enum shader_uniform_type {
    SHADER_UNIFORM_TYPE_FLOAT32 = 0U,
    SHADER_UNIFORM_TYPE_FLOAT32_2 = 1U,
    SHADER_UNIFORM_TYPE_FLOAT32_3 = 2U,
    SHADER_UNIFORM_TYPE_FLOAT32_4 = 3U,
    SHADER_UNIFORM_TYPE_INT8 = 4U,
    SHADER_UNIFORM_TYPE_UINT8 = 5U,
    SHADER_UNIFORM_TYPE_INT16 = 6U,
    SHADER_UNIFORM_TYPE_UINT16 = 7U,
    SHADER_UNIFORM_TYPE_INT32 = 8U,
    SHADER_UNIFORM_TYPE_UINT32 = 9U,
    SHADER_UNIFORM_TYPE_MATRIX_4 = 10U,
    SHADER_UNIFORM_TYPE_SAMPLER_1D = 11U,
    SHADER_UNIFORM_TYPE_SAMPLER_2D = 12U,
    SHADER_UNIFORM_TYPE_SAMPLER_3D = 13U,
    SHADER_UNIFORM_TYPE_SAMPLER_CUBE = 14U,
    SHADER_UNIFORM_TYPE_SAMPLER_1D_ARRAY = 15U,
    SHADER_UNIFORM_TYPE_SAMPLER_2D_ARRAY = 16U,
    SHADER_UNIFORM_TYPE_SAMPLER_CUBE_ARRAY = 17U,
    SHADER_UNIFORM_TYPE_CUSTOM = 255U
} shader_uniform_type;
