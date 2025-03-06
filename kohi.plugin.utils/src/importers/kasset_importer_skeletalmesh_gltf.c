#include "kasset_importer_skeletalmesh_gltf.h"

#include "assets/kasset_types.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "defines.h"
#include "importers/kasset_importer_image.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "serializers/kasset_material_serializer.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "strings/kstring_id.h"
#include "systems/asset_system.h"
#include <math/geometry.h>
#include <systems/material_system.h>

#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf/cgltf.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#    define STB_IMAGE_IMPLEMENTATION
// Using our own filesystem.
#    define STBI_NO_STDIO
// User-friendly error messages.
#    define STBI_FAILURE_USERMSG
#endif
#include "vendor/stb_image.h"

// HACK: temp
//

// typedef struct kskeletalmesh_joint {
//     u32 id;
//     u32 child_count;
//     struct kskeletalmesh_joint* children;
//     mat4 transform;
// } kskeletalmesh_joint;

// typedef struct kskeletalmesh_joint_transform {
//     vec3 position;
//     quat rotation;
//     vec3 scale;
//     mat4 transform;
// } kskeletalmesh_joint_transform;

// typedef struct kskeletalmesh_animation_keyframe {
//     f32 timestamp;
//     u32 joint_transform_count;
//     kskeletalmesh_joint_transform* joint_transforms;
// } kskeletalmesh_animation_keyframe;

// typedef struct kskeletalmesh_animation {
//     u32 keyframe_count;
//     kskeletalmesh_animation_keyframe* keyframes;
// } kskeletalmesh_animation;

typedef struct kskeletalmesh_bone_transform {
    vec3 translation;
    quat rotation;
    vec3 scale;
} kskeletalmesh_bone_transform;

// skeletal animation bone
typedef struct kskeletalmesh_bone {
    kname name;
    i32 parent_index;
} kskeletalmesh_bone;

// skeletal mesh
typedef struct kskeletalmesh {
    u32 geometry_count;
    kgeometry* geometries;
    u32* geometry_materials; // Indices into the materials array.

    u32 material_count;
    material_instance* materials;

    // Animation data
    u32 bone_count;
    kskeletalmesh_bone* bones;
    kskeletalmesh_bone_transform* bones_base; // bind pose
} kskeletalmesh;

typedef struct kskeletalmesh_animation {
    u32 frame_count;
    u32 bone_count;
    kskeletalmesh_bone* bones;
    kskeletalmesh_bone_transform** frame_poses;
    kname name;
} kskeletalmesh_animation;

typedef struct gltf_source_image {
    // Image name. Should match the asset name if an external asset.
    kname name;
    u32 width;
    u32 height;
    u8 channel_count;
    // Pixel data. Used if the image data was embedded in the GLTF.
    // When true, an asset will need to be created/exported for this.
    u8* data;
} gltf_source_image;

typedef struct gltf_source_material {
    // Name of the material.
    kname name;
    // Material type.
    kmaterial_type type;
    // Material lighting model.
    kmaterial_model model;

    vec4 base_colour;
    gltf_source_image base_colour_image;

    // Combined metallic/roughness/ao image.
    gltf_source_image mra_image;
    f32 roughness;
    f32 metallic;

    gltf_source_image normal_image;

    vec3 emissive_colour;
    gltf_source_image emissive_image;

} gltf_source_material;

static b8 load_image(cgltf_image* src, gltf_source_image* out_image) {
    if (!src | !out_image) {
        return false;
    }

    // FIXME: Perhaps a default texture should be used anywhere this errors out...

    if (src->uri != 0) {
        if (strings_nequali(src->uri, "data:", 5)) {
            // String is a data uri, attempt to parse/decompose it.

            u32 type_subtype_length = 0;
            char* type_subtype = 0;
            u32 param_length = 0;
            char* param = 0;
            u32 base64_data_str_length = 0;
            char* base64_data_str = 0;
            if (!string_decompose_data_uri(src->uri, &type_subtype_length, &type_subtype, &param_length, &param, &base64_data_str_length, &base64_data_str)) {
                KERROR("Failed to decompose data URI. Image cannot be loaded.");
                return false;
            }

            // The raw image data decoded from base64. Still encoded as jpg/png/etc.
            void* decoded_data = 0;

            // Perform the data decode.
            cgltf_options options = {0};
            cgltf_result result = cgltf_load_buffer_base64(&options, base64_data_str_length, base64_data_str, &decoded_data);

            // Free up the temp strings above regardless of the result here.
            if (type_subtype) {
                string_free(type_subtype);
            }
            if (param) {
                string_free(param);
            }
            if (base64_data_str) {
                KFREE_TYPE_CARRAY(base64_data_str, char, base64_data_str_length);
            }

            if (result == cgltf_result_success) {
                // Get the actual decoded data.
                i32 width, height, channels_in_file;
                u8* pixel_data = stbi_load_from_memory((u8*)decoded_data, base64_data_str_length, &width, &height, &channels_in_file, 4);

                // Free the decoded base64 data regardless of the result here.
                kfree(decoded_data, base64_data_str_length, MEMORY_TAG_RESOURCE);

                if (!pixel_data) {
                    const char* failure_reason_str = stbi_failure_reason();
                    KERROR("Failed to decode base64 image data. Internal error: '%s'", failure_reason_str);
                    return false;
                }

                out_image->width = width;
                out_image->height = height;
                out_image->data = pixel_data;
                out_image->channel_count = 4;

                if (src->name) {
                    // Take the name as-is.
                    out_image->name = kname_create(src->name);
                } else {
                    // Generate a random name to use for the image since this will have to be "imported" later.
                    char* random_name = string_generate_random(32);
                    out_image->name = kname_create(random_name);
                    string_free(random_name);
                }

                return true;
            }

        } else {
            // Perhaps the image is provided as a file path.
            char temp[1024] = {0};
            string_filename_no_extension_from_path(temp, src->uri);
            out_image->name = kname_create(temp);

            // NOTE: This will be loaded via the asset system later on.
        }
    } else if (src->buffer_view && src->buffer_view->data) {
        // Image is provided as a data buffer.
        u8* data = kallocate(src->buffer_view->size, MEMORY_TAG_ARRAY);
        u32 offset = src->buffer_view->offset;
        u32 stride = src->buffer_view->stride;

        // Take a copy of the buffer for loading.
        for (u32 i = 0; i < src->buffer_view->size; ++i) {
            data[i] = ((u8*)src->buffer_view->buffer->data)[offset];
            offset += stride;
        }

        // TODO: Can verify the mime type here if need be...
        /* // NOTE: Some mime types are defined as "image\\/type" for some reason, so handle this too.

        if (strings_equal(src->mime_type, "image\\/png") || strings_equal(src->mime_type, "image/png")) {
            KTRACE("Processing GLTF buffer as image/png...");
        } else if (strings_equal(src->mime_type, "image\\/jpeg") || strings_equal(src->mime_type, "image/jpeg")) {
            KTRACE("Processing GLTF buffer as image/jpg...");
        } else {
            KERROR("GLTF mime type not recognized.");
            return false;
        } */

        // Get the actual decoded data.
        i32 width, height, channels_in_file;
        u8* pixel_data = stbi_load_from_memory(data, src->buffer_view->size, &width, &height, &channels_in_file, 4);

        // Free the buffer data copy regardless of the result here.
        kfree(data, src->buffer_view->size, MEMORY_TAG_ARRAY);

        if (!pixel_data) {
            const char* failure_reason_str = stbi_failure_reason();
            KERROR("Failed to decode buffer image data. Internal error: '%s'", failure_reason_str);
            return false;
        }

        out_image->width = width;
        out_image->height = height;
        out_image->data = pixel_data;
        out_image->channel_count = 4;

        if (src->name) {
            // Take the name as-is.
            out_image->name = kname_create(src->name);
        } else {
            // Generate a random name to use for the image since this will have to be "imported" later.
            char* random_name = string_generate_random(32);
            out_image->name = kname_create(random_name);
            string_free(random_name);
        }

        return true;
    }

    return false;
}

static b8 load_bones(cgltf_skin skin, u32* out_count, kskeletalmesh_bone** out_bones) {
    if (!out_count || !out_bones) {
        return false;
    }

    *out_count = skin.joints_count;

    *out_bones = KALLOC_TYPE_CARRAY(kskeletalmesh_bone, *out_count);

    for (u32 i = 0; i < skin.joints_count; ++i) {
        cgltf_node* node = skin.joints[i];
        if (node->name) {
            (*out_bones)[i].name = kname_create(node->name);
        }

        i32 parent_index = -1;
        for (u32 j = 0; j < skin.joints_count; ++j) {
            if (skin.joints[j] == node->parent) {
                parent_index = j;
                break;
            }
        }

        (*out_bones)[i].parent_index = parent_index;
    }

    return true;
}

static b8 load_gltf(u64 data_size, const void* raw_data, kskeletalmesh* out_mesh, const char* gltf_path, kname package_name) {
    if (!data_size || !raw_data || !out_mesh) {
        return false;
    }

    cgltf_options options = {0};
    options.file.read = 0;    // TODO: Do we need a callback here?
    options.file.release = 0; // TODO: Do we need to handle this callback?

    cgltf_data* data = 0;
    cgltf_result result = cgltf_parse(&options, raw_data, data_size, &data);

    if (result != cgltf_result_success) {
        KERROR("%s - Error loading GLTF data.", __FUNCTION__);
        return false;
    }

    // Verify file type.
    if (data->file_type == cgltf_file_type_glb) {
        KTRACE("Skeletalmesh base data for GLB loaded successfully.");
    } else if (data->file_type == cgltf_file_type_gltf) {
        KTRACE("Skeletalmesh base data for GLTF loaded successfully.");
    } else {
        KERROR("Skeletalmesh base data failed to load - invalid format.");
        return false;
    }

    // Manually load embedded buffers
    for (u32 i = 0; i < data->buffers_count; ++i) {
        cgltf_buffer* buf = &data->buffers[i];
        if (buf->uri == 0 && buf->size > 0) {
            // This is a GLB binary buffer. glb->bin contains the data.
            buf->data = (void*)data->bin;
        } else if (buf->uri && strings_nequal(buf->uri, "data:", 5)) {
            // Base-64-encoded buffer
            result = cgltf_load_buffer_base64(&options, buf->size, buf->uri, &buf->data);
            if (result != cgltf_result_success) {
                KERROR("Failed to load base64 buffer.");
                return false;
            }
        } else {
            // TODO: perhaps try to load an external .bin file as well?
            KERROR("External buffer detected, but 'cgltf_load_buffers' cannot be used without a file path.");
            return false;
        }
    }

    // Validate
    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        KERROR("%s - Error validating GLTF data.", __FUNCTION__);
        return false;
    }

    KTRACE("Mesh count: %u", data->meshes_count);
    KTRACE("Material count: %u", data->materials_count);
    KTRACE("Buffer count: %u", data->buffers_count);
    KTRACE("Image count: %u", data->images_count);
    KTRACE("Texture count: %u", data->textures_count);

    // Process every primitive as its own geometry. Start by getting a count.
    u32 primitive_count = 0;
    for (u32 i = 0; i < data->nodes_count; ++i) {
        cgltf_node* node = &data->nodes[i];
        cgltf_mesh* mesh = node->mesh;
        if (!mesh) {
            continue;
        }

        // Only count triangle-based primitives.
        for (u32 j = 0; j < mesh->primitives_count; ++j) {
            if (mesh->primitives[j].type == cgltf_primitive_type_triangles) {
                primitive_count++;
            }
        }
    }
    KTRACE("Triangle count: %u", primitive_count);

    out_mesh->geometry_count = primitive_count;
    out_mesh->geometries = KALLOC_TYPE_CARRAY(kgeometry, out_mesh->geometry_count);

    out_mesh->material_count = data->materials_count + 1; // NOTE: Retain one more slot for a default material.
    out_mesh->materials = KALLOC_TYPE_CARRAY(material_instance, out_mesh->material_count);
    out_mesh->materials[0] = material_system_get_default_standard(engine_systems_get()->material_system);

    out_mesh->geometry_materials = KALLOC_TYPE_CARRAY(u32, out_mesh->geometry_count);

    // Keep an array of source materials.
    gltf_source_material* materials = KALLOC_TYPE_CARRAY(gltf_source_material, data->materials_count);

    // Load material data.
    for (u32 i = 0, j = 1; i < data->materials_count; ++i, ++j) {
        // Start off using the default material in case parsing fails.
        out_mesh->materials[j] = material_system_get_default_standard(engine_systems_get()->material_system);

        gltf_source_material* material = &materials[i];
        cgltf_material* src = &data->materials[j];

        // Metallic/roughness workflow.
        if (src->has_pbr_metallic_roughness) {

            // Base colour/albedo
            if (src->pbr_metallic_roughness.base_color_texture.texture) {
                if (!load_image(src->pbr_metallic_roughness.base_color_texture.texture->image, &material->base_colour_image)) {
                    KERROR("Failed to load base colour/albedo image!");
                    return false;
                }
            }

            // Base colour (tint)
            for (u32 k = 0; k < 4; ++k) {
                material->base_colour.elements[k] = src->pbr_metallic_roughness.base_color_factor[k];
            }

            // Metallic/roughness (pipe into MRA)
            if (src->pbr_metallic_roughness.metallic_roughness_texture.texture) {
                gltf_source_image metallic_roughness = {0};
                if (!load_image(src->pbr_metallic_roughness.metallic_roughness_texture.texture->image, &metallic_roughness)) {
                    KERROR("Failed to load metallic/roughness image!");
                    return false;
                }

                // If image data is loaded, pipe it into a combined MRA image.
                if (metallic_roughness.data) {
                    material->mra_image.name = metallic_roughness.name;
                    material->mra_image.width = metallic_roughness.width;
                    material->mra_image.height = metallic_roughness.height;
                    material->mra_image.channel_count = metallic_roughness.channel_count;

                    u32 image_size = metallic_roughness.width * metallic_roughness.height * metallic_roughness.channel_count;
                    material->mra_image.data = kallocate(sizeof(u8) * image_size, MEMORY_TAG_ARRAY);

                    for (u32 k = 0; k < image_size; k += metallic_roughness.channel_count) {
                        // NOTE: GLTF stores roughness in g, metallic in b.
                        // Kohi stores MRA where metallic = r, roughness = g, ao = b.
                        material->mra_image.data[k + 0] = metallic_roughness.data[k + 1];
                        material->mra_image.data[k + 1] = metallic_roughness.data[k + 2];
                    }
                }
            }

            material->metallic = src->pbr_metallic_roughness.metallic_factor;
            material->roughness = src->pbr_metallic_roughness.roughness_factor;
        }

        // Normal texture
        if (src->normal_texture.texture) {
            if (!load_image(src->normal_texture.texture->image, &material->normal_image)) {
                KERROR("Failed to load normal image!");
                return false;
            }
        }

        // AO texture - Feed this into the MRA image.
        if (src->occlusion_texture.texture) {
            gltf_source_image ao_image = {0};
            if (!load_image(src->occlusion_texture.texture->image, &ao_image)) {
                KERROR("Failed to load ambient occlusion image!");
                return false;
            }

            if (ao_image.data) {
                u32 mra_image_size = material->mra_image.width * material->mra_image.height * material->mra_image.channel_count;

                // Verify the dimensions are the same before trying to combine.
                if (ao_image.width != material->mra_image.width || ao_image.height != material->mra_image.height) {
                    KWARN("Size mismatch on AO image vs MRA image! Using default value of white.");

                    for (u32 k = 0; k < mra_image_size; k += material->mra_image.channel_count) {
                        // NOTE: Kohi stores MRA where metallic = r, roughness = g, ao = b.
                        material->mra_image.data[k + 2] = 255;
                    }
                } else {
                    for (u32 k = 0; k < mra_image_size; k += material->mra_image.channel_count) {
                        // NOTE: Kohi stores MRA where metallic = r, roughness = g, ao = b.
                        material->mra_image.data[k + 2] = ao_image.data[k + 0];
                    }
                }
            }
        }

        // Emissive texture
        if (src->emissive_texture.texture) {
            if (!load_image(src->emissive_texture.texture->image, &material->emissive_image)) {
                KERROR("Failed to load emissive image!");
                return false;
            }
        }

        // Emissive colour factor.
        for (u32 k = 0; k < 4; ++k) {
            material->emissive_colour.elements[k] = src->emissive_factor[k];
        }

        // Export material.
        {
            kasset_material new_material = {0};
            new_material.base.name = material->name;
            new_material.base.package_name = package_name;
            // Since it's an import, make note of the source asset path as well (the gltf file).
            char* source_asset_path = string_format("./assets/models/source/%s", gltf_path);
            new_material.base.meta.source_asset_path = kstring_id_create(source_asset_path);
            string_free(source_asset_path);

            // Imports do not use a custom shader.
            new_material.custom_shader_name = 0;

            // All material imports are standard PBR.
            new_material.type = KMATERIAL_TYPE_STANDARD;
            new_material.model = KMATERIAL_MODEL_PBR;

            // Force defaults for things not considered in GLTF files.
            new_material.casts_shadow = true;
            new_material.recieves_shadow = true;

            // Transparency is determined by the mode _not_ being opaque.
            new_material.has_transparency = src->alpha_mode != cgltf_alpha_mode_opaque;

            // Material maps.
            {
                // Base colour.
                if (material->base_colour_image.data) {
                    // TODO: Image needs to be exported.
                }

                if (material->base_colour_image.name) {
                    // Save off the name as the resource name and request it later.
                    new_material.base_colour_map.resource_name = material->base_colour_image.name;
                    new_material.base_colour_map.package_name = package_name;
                } else {
                    KERROR("No name for base colour image!");
                    return false;
                }
                new_material.base_colour = material->base_colour;
            }

            {
                // MRA
                if (material->mra_image.data) {
                    // TODO: Image needs to be exported.
                }

                if (material->mra_image.name) {
                    // Save off the name as the resource name and request it later.
                    new_material.mra_map.resource_name = material->mra_image.name;
                    new_material.mra_map.package_name = package_name;
                } else {
                    KERROR("No name for MRA image!");
                    return false;
                }

                // Set flags and values to use MRA.
                new_material.metallic = material->metallic;
                new_material.roughness = material->roughness;
                new_material.ambient_occlusion = 1.0f;
                new_material.ambient_occlusion_enabled = true;
                new_material.use_mra = true;
            }

            {
                // Normal
                if (material->normal_image.data) {
                    // TODO: Image needs to be exported.
                }

                if (material->normal_image.name) {
                    // Save off the name as the resource name and request it later.
                    new_material.normal_map.resource_name = material->normal_image.name;
                    new_material.normal_map.package_name = package_name;
                } else {
                    KERROR("No name for normal image!");
                    return false;
                }

                new_material.normal_enabled = true;
            }

            {
                // Emissive
                if (material->emissive_image.data) {
                    new_material.emissive_enabled = true;
                    // TODO: Image needs to be exported.
                }

                if (material->emissive_image.name) {
                    // Save off the name as the resource name and request it later.
                    new_material.emissive_map.resource_name = material->emissive_image.name;
                    new_material.emissive_map.package_name = package_name;
                    new_material.emissive_enabled = true;
                } else {
                    KERROR("No name for normal image!");
                    return false;
                }
            }

            // Serialize the material.
            const char* serialized_text = kasset_material_serialize((kasset*)&new_material);
            if (!serialized_text) {
                KWARN("Failed to serialize material '%s'. See logs for details.", kname_string_get(new_material.base.name));
            }

            // FIXME: This needs to write to the manifest before outputting this to the VFS. Otherwise,
            // the VFS won't know where to write to.

            // Write out kmt file.
            if (!vfs_asset_write(engine_systems_get()->vfs_system_state, (kasset*)&new_material, false, string_length(serialized_text), serialized_text)) {
                KERROR("Failed to write serialized material to disk.");
            }
        }
    } // each material

    // Cleanup materials.
    KFREE_TYPE_CARRAY(materials, gltf_source_material, data->materials_count);

    // TODO: export materials and image assets.

    return true;
}

b8 kasset_importer_skeletalmesh_gltf_import(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset) {

    cgltf_options options = {0};
    cgltf_data* gltf = 0;
    cgltf_result result = cgltf_parse(&options, data, data_size, &gltf);

    // Manually load embedded buffers
    for (u32 i = 0; i < gltf->buffers_count; ++i) {
        cgltf_buffer* buf = &gltf->buffers[i];
        if (buf->uri == 0 && buf->size > 0) {
            // This is a GLB binary buffer. glb->bin contains the data.
            buf->data = (void*)gltf->bin;
        } else if (buf->uri && strings_nequal(buf->uri, "data:", 5)) {
            // Base-64-encoded buffer
            cgltf_load_buffer_base64(&options, buf->size, buf->uri, &buf->data);
        } else {
            // TODO: perhaps try to load an external .bin file as well?
            KERROR("External buffer detected, but 'cgltf_load_buffers' cannot be used without a file path.");
            return false;
        }
    }

    if (result == cgltf_result_success) {
        result = cgltf_validate(gltf);
    }

    if (result == cgltf_result_success) {
        KTRACE("GLTF file type: %u", gltf->file_type);

        // Extract meshes
        KTRACE("GLTF mesh count: %u", gltf->meshes_count);
        for (u32 i = 0; i < gltf->meshes_count; ++i) {
            cgltf_mesh* mesh = &gltf->meshes[i];
            KTRACE("Mesh name = '%s'", mesh->name);
            for (u32 j = 0; j < mesh->primitives_count; ++j) {
                cgltf_primitive* primitive = &mesh->primitives[j];
                if (primitive) {
                    //
                }
                KTRACE("Primitive %zu", j);
            }
        }

        /* // Extract skeleton (i.e. joints and skins)
        KTRACE("GLTF skeleton/skin count: %u", gltf->skins_count);
        for (u32 i = 0; i < gltf->skins_count; ++i) {
            cgltf_skin* skin = &gltf->skins[i];
            KTRACE("Skin name = '%s'", skin->name);
            // Joints
            for (u32 j = 0; j < skin->joints_count; ++j) {
                cgltf_node* joint_node = skin->joints[j];
                KTRACE("Joint idx=%zu, name='%s'", j, joint_node->name);
            }
        } */

        // Extract skeleton.
        kskeletalmesh_joint skeleton = {0};
        u32 joint_index = 0;
        for (u32 i = 0; i < gltf->nodes_count; ++i) {
            // Start from the root node, or the only one without a parent.
            if (gltf->nodes[i].parent == 0) {
                skeleton = joint_hierarchy_create(&gltf->nodes[i], &joint_index);
                break;
            }
        }

        // Extract animations
        KTRACE("GLTF animation count: %u", gltf->animations_count);
        if (gltf->animations_count) {
            kskeletalmesh_animation* animations = KALLOC_TYPE_CARRAY(kskeletalmesh_animation, gltf->animations_count);
            for (u32 i = 0; i < gltf->animations_count; ++i) {
                cgltf_animation* animation = &gltf->animations[i];
                KTRACE("Animation name = '%s'", animation->name);

                kskeletalmesh_animation* out_anim = &animations[i];
                out_anim->keyframe_count = animation->samplers[0].input->count; // TODO: is this safe?
                if (!out_anim->keyframe_count) {
                    KERROR("Animation with no keyframes found. Skipping.");
                    continue;
                }
                out_anim->keyframes = KALLOC_TYPE_CARRAY(kskeletalmesh_animation_keyframe, out_anim->keyframe_count);

                for (u32 k = 0; k < out_anim->keyframe_count; ++k) {

                    kskeletalmesh_animation_keyframe* keyframe = &out_anim->keyframes[k];
                    f32 time = 0;
                    cgltf_accessor_read_float(animation->samplers[0].input, k, &time, 1);
                    keyframe->timestamp = time;
                    keyframe->joint_transform_count = gltf->nodes_count;
                    keyframe->joint_transforms = KALLOC_TYPE_CARRAY(kskeletalmesh_joint_transform, keyframe->joint_transform_count);
                }

                // Channels - These contain the joint transforms for the keyframe.
                for (u32 c = 0; c < animation->channels_count; c++) {
                    cgltf_animation_channel* channel = &animation->channels[c];
                    if (!channel->target_node) {
                        continue;
                    }

                    /* cgltf_accessor* input = channel->sampler->input; */
                    cgltf_accessor* output = channel->sampler->output;

                    f32 values[16];

                    for (u32 k = 0; k < channel->sampler->input->count; ++k) {
                        kskeletalmesh_animation_keyframe* keyframe = &out_anim->keyframes[k];

                        // Depending on the type of movement, assign the value as needed.
                        i64 index = -1;
                        for (u64 n = 0; n < gltf->nodes_count; ++n) {
                            if (&gltf->nodes[n] == channel->target_node) {
                                index = (i64)n;
                                break;
                            }
                        }
                        if (index == -1) {
                            KERROR("Target node not found in animation channel. Skipping.");
                            continue;
                        }

                        kskeletalmesh_joint_transform* jt = &keyframe->joint_transforms[index];

                        cgltf_accessor_read_float(output, k, values, output->count);

                        if (channel->target_path == cgltf_animation_path_type_translation) {
                            jt->position.x = values[0];
                            jt->position.y = values[1];
                            jt->position.z = values[2];
                        } else if (channel->target_path == cgltf_animation_path_type_rotation) {
                            jt->rotation.x = values[0];
                            jt->rotation.y = values[1];
                            jt->rotation.z = values[2];
                            jt->rotation.w = values[3];
                        } else if (channel->target_path == cgltf_animation_path_type_scale) {
                            jt->scale.x = values[0];
                            jt->scale.y = values[1];
                            jt->scale.z = values[2];
                        }
                    }
                }
            }
            if (animations) {
                //
            }
        }

        // Materials
        KTRACE("GLTF material count: %u", gltf->materials_count);
        for (u32 i = 0; i < gltf->materials_count; ++i) {
            cgltf_material* material = &gltf->materials[i];
            KTRACE("Material name = '%s'", material->name);
        }
    }

    cgltf_free(gltf);
    return result == cgltf_result_success;
}
