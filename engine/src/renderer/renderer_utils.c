#include "renderer_utils.h"

b8 uniform_type_is_sampler(shader_uniform_type type) {
    switch (type) {
        case SHADER_UNIFORM_TYPE_SAMPLER_1D:
        case SHADER_UNIFORM_TYPE_SAMPLER_2D:
        case SHADER_UNIFORM_TYPE_SAMPLER_3D:
        case SHADER_UNIFORM_TYPE_SAMPLER_CUBE:
        case SHADER_UNIFORM_TYPE_SAMPLER_1D_ARRAY:
        case SHADER_UNIFORM_TYPE_SAMPLER_2D_ARRAY:
        case SHADER_UNIFORM_TYPE_SAMPLER_CUBE_ARRAY:
            return true;
        default:
            return false;
    }
}
