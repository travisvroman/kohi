
#pragma once

#include "core_render_types.h"
#include <assets/kasset_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <resources/resource_types.h>

typedef struct obj_mtl_source_material {
    // Name of the material.
    kname name;
    // Material type.
    kmaterial_type type;
    // Material lighting model.
    kmaterial_model model;

    vec3 ambient_colour;
    kname ambient_image_asset_name;
    vec3 diffuse_colour;
    kname diffuse_image_asset_name;
    f32 diffuse_transparency;
    kname diffuse_transparency_image_asset_name;

    f32 roughness;
    kname roughness_image_asset_name;
    f32 metallic;
    kname metallic_image_asset_name;
    f32 sheen;
    kname sheen_image_asset_name;

    kname normal_image_asset_name;

    kname displacement_image_asset_name;

    vec3 specular_colour;
    f32 specular_exponent;
    kname specular_image_asset_name;

    // 0-1, 1=fully opaque, 0=fully transparent.
    f32 transparency;
    kname alpha_image_asset_name;

    kname mra_image_asset_name;

    vec3 emissive_colour;
    kname emissive_image_asset_name;

    // Index of refraction/optical density. [0.001-10]. 1.0 means light does not bend as it passes through. Glass should be ~1.5. Values < 1.0 are strange.
    f32 ior;
} obj_mtl_source_material;

typedef struct obj_mtl_source_asset {
    u32 material_count;
    obj_mtl_source_material* materials;

} obj_mtl_source_asset;

b8 obj_mtl_serializer_serialize(const obj_mtl_source_asset* source_asset, const char** out_file_text);

/**
 * Attempts to deserialize the contents of Wavefront MTL file.
 *
 * @param mtl_file_text The mtl file content. Optional.
 * @param out_mtl_source_asset A pointer to hold the deserialized material data. Optional unless mtl_file_text is provided, then required.
 * @return True on success; otherwise false.
 */
b8 obj_mtl_serializer_deserialize(const char* mtl_file_text, obj_mtl_source_asset* out_mtl_source_asset);
