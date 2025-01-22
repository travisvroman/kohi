/**
 * @file registry.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A registry holds arbitrarily-sized blocks of memory that are referenced by handle.
 * These blocks are stored along with their size in the registry, and the registry owns
 * its own copy of the data (if a block is probided, it takes a copy in the add and set functions). Entries can opt-in
 * to being auto-released when thier internal reference counter reaches 0. Callers can also
 * register callbacks when acquiring references to registry blocks to be informed of any updates
 * to said block when it occurs (via the block_set function) by passing a callback during acquisition.
 * @version 1.0
 * @date 2024-03-04
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */
#pragma once

#include "defines.h"
#include "identifiers/khandle.h"

typedef enum kregistry_entry_change_type {
    // The block of memory was changed/replaced.
    K_REGISTRY_CHANGE_TYPE_BLOCK_CHANGED,
    // The block of memory/the entry was destroyed.
    K_REGISTRY_CHANGE_TYPE_DESTROYED
} kregistry_entry_change_type;

/**
 * @brief Callback to be made when a registry block is updated via registry_entry_block_set().
 */
typedef void (*PFN_on_registry_entry_updated)(void* sender, void* block, u64 size, kregistry_entry_change_type change_type);

typedef struct kregistry_entry_listener_callback {
    void* listener;
    PFN_on_registry_entry_updated callback;
} kregistry_entry_listener_callback;

typedef struct kregistry_entry {
    u64 uniqueid;
    u64 block_size;
    void* block;
    i32 reference_count;
    b8 auto_release;

    // darray
    kregistry_entry_listener_callback* callbacks;
} kregistry_entry;

typedef struct k_registry {
    // darray
    kregistry_entry* entries;
} kregistry;

/**
 * Creates a new registry.
 *
 * @param out_registry A pointer to hold the newly-created registry.
 */
KAPI void kregistry_create(kregistry* out_registry);

/**
 * Destroys the provided registry.
 *
 * @param registry A pointer to the registry to be destroyed.
 */
KAPI void kregistry_destroy(kregistry* registry);

/**
 * @brief Adds a new entry to the registry and returns a handle to it.
 *
 * @param registry A pointer to the registry.
 * @param block A block of memory to be tracked. Optional. If provided, a copy of this is taken and owned by the registry.
 * @param size The size of the block of memory.
 * @param auto_release Indicates if this entry should automatically release memory when its internal reference counter reaches 0.
 * @returns A handle to the newly-created registry entry.
 */
KAPI khandle kregistry_add_entry(kregistry* registry, const void* block, u64 size, b8 auto_release);

/**
 * @brief Attempts to set an existing registry block's data. When this occurs, the old data block will be released
 * and replaced by the new one. If any callbacks are registered for this block, they will be informed of the update
 * so thier pointer to the block may be updated. NOTE: This may only be done against existing entries. Non-existing will fail.
 *
 * @param registry A pointer to the registry.
 * @param entry_handle The handle to the entry.
 * @param block The new block of memory.
 * @param size The new size of the block of memory.
 * @param sender An optional pointer to the initiator of this operation.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 kregistry_entry_set(kregistry* registry, khandle entry_handle, const void* block, u64 size, void* sender);

/**
 * @brief Attempts to update the callback for the given entry for the provided listener.
 *
 * @param registry A pointer to the registry.
 * @param entry_handle The handle to the entry.
 * @param listener The listener to update the callback for. Optional. If 0/null is passed, it's assumed that the block itself will be the listener.
 * @param updated_callback The callback to be set. Required.
 * @returns True on success; otherwise false.
 */
KAPI b8 kregistry_entry_update_callback_for_listener(kregistry* registry, khandle entry_handle, void* listener, PFN_on_registry_entry_updated updated_callback);

/**
 * @brief Attempts to acquire a reference for the given entry for the provided listener.
 * This increments the internal reference counter.
 *
 * @param registry A pointer to the registry.
 * @param entry_handle The handle to the entry to acquire.
 * @param listener The listener to update the callback for. Optional, but required if updated_callback is set.
 * @param updated_callback The callback to be set. Optional.
 * @returns A pointer to the entry's memory block on success; otherwise 0/null.
 */
KAPI void* kregistry_entry_acquire(kregistry* registry, khandle entry_handle, void* listener, PFN_on_registry_entry_updated updated_callback);

/**
 * @brief Attempts to release a reference for the given entry for the provided listener.
 * This decrements the internal reference counter. If set to auto-release, the entry's internal
 * block of memory will be released when its internal reference counter reaches 0.
 *
 * @param registry A pointer to the registry.
 * @param entry_handle The handle to the entry to release.
 * @param listener A pointer to the listener. Optional.
 */
KAPI void kregistry_entry_release(kregistry* registry, khandle entry_handle, void* listener);
