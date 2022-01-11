/**
 * @file renderer_frontend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The renderer frontend, which is the only thing the rest of the engine sees.
 * This is responsible for transferring any data to and from the renderer backend in an
 * agnostic way.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer_types.inl"

/**
 * @brief Initializes the renderer frontend/system. Should be called twice - once
 * to obtain the memory requirement (passing state=0), and a second time passing
 * allocated memory to state.
 * 
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A block of memory to hold state data, or 0 if obtaining memory requirement.
 * @param application_name The name of the application.
 * @return True on success; otherwise false.
 */
b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name);

/**
 * @brief Shuts the renderer system/frontend down.
 * 
 * @param state A pointer to the state block of memory.
 */
void renderer_system_shutdown(void* state);

/**
 * @brief Handles resize events.
 * 
 * @param width The new window width.
 * @param height The new window height.
 */
void renderer_on_resized(u16 width, u16 height);

/**
 * @brief Draws the next frame using the data provided in the render packet.
 * 
 * @param packet A pointer to the render packet, which contains data on what should be rendered.
 * @return True on success; otherwise false.
 */
b8 renderer_draw_frame(render_packet* packet);

/**
 * @brief Sets the view matrix in the renderer. NOTE: exposed to public API.
 * 
 * @deprecated HACK: this should not be exposed outside the engine.
 * @param view The view matrix to be set.
 */
KAPI void renderer_set_view(mat4 view);

/**
 * @brief Creates a new texture.
 * 
 * @param pixels The raw image data to be uploaded to the GPU.
 * @param texture A pointer to the texture to be loaded.
 */
void renderer_create_texture(const u8* pixels, struct texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources from the GPU.
 * 
 * @param texture A pointer to the texture to be destroyed.
 */
void renderer_destroy_texture(struct texture* texture);

/**
 * @brief Creates a new material instance, acquiring GPU resources.
 * 
 * @param material A pointer to the material to load.
 * @return True on success; otherwise false.
 */
b8 renderer_create_material(struct material* material);

/**
 * @brief Destroys the given material, releasing GPU resources.
 * 
 * @param material A pointer to the material to unload.
 */
void renderer_destroy_material(struct material* material);

/**
 * @brief Acquiores GPU resources and uploads geometry data.
 * 
 * @param geometry A pointer to the geometry to acquire resources for.
 * @param vertex_size The size of each vertex.
 * @param vertex_count The number of vertices.
 * @param vertices The vertex array.
 * @param index_size The size of each index.
 * @param index_count The number of indices.
 * @param indices The index array.
 * @return True on success; otherwise false.
 */
b8 renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 * 
 * @param geometry A pointer to the geometry to be destroyed.
 */
void renderer_destroy_geometry(geometry* geometry);
