#include "identifiers/khandle.h"

#include "defines.h"
#include "identifiers/identifier.h"

khandle khandle_create(u32 handle_index) {
    khandle out_handle = {0};
    out_handle.handle_index = handle_index;
    out_handle.unique_id = identifier_create();
    return out_handle;
}

khandle khandle_create_with_identifier(u32 handle_index, identifier id) {
    khandle out_handle = {0};
    out_handle.handle_index = handle_index;
    out_handle.unique_id = id;
    return out_handle;
}

khandle khandle_create_with_u64_identifier(u32 handle_index, u64 uniqueid) {
    khandle out_handle = {0};
    out_handle.handle_index = handle_index;
    out_handle.unique_id.uniqueid = uniqueid;
    return out_handle;
}

khandle khandle_invalid(void) {
    khandle out_handle = {0};
    out_handle.handle_index = INVALID_ID;
    out_handle.unique_id.uniqueid = INVALID_ID_U64;
    return out_handle;
}

b8 khandle_is_invalid(khandle handle) {
    return handle.handle_index == INVALID_ID || handle.unique_id.uniqueid == INVALID_ID_U64;
}

b8 khandle_is_valid(khandle handle) {
    return handle.handle_index != INVALID_ID && handle.unique_id.uniqueid != INVALID_ID_U64;
}

void khandle_invalidate(khandle* handle) {
    if (handle) {
        handle->handle_index = INVALID_ID;
        handle->unique_id.uniqueid = INVALID_ID_U64;
    }
}

b8 khandle_is_pristine(khandle handle, u64 uniqueid) {
    return handle.unique_id.uniqueid == uniqueid;
}

b8 khandle_is_stale(khandle handle, u64 uniqueid) {
    return handle.unique_id.uniqueid != uniqueid;
}

/**
 * kahandle16 implementation
 */

khandle16 khandle16_create(u16 handle_index) {
    return (khandle16){
        .handle_index = handle_index,
        .generation = 0};
}

khandle16 khandle16_create_with_u16_generation(u16 handle_index, u16 generation) {
    return (khandle16){
        .handle_index = handle_index,
        .generation = generation};
}

khandle16 khandle16_invalid(void) {
    return (khandle16){
        .handle_index = INVALID_ID_U16,
        .generation = INVALID_ID_U16};
}

b8 khandle16_is_valid(khandle16 handle) {
    return handle.handle_index == INVALID_ID_U16 || handle.generation == INVALID_ID_U16;
}

b8 khandle16_is_invalid(khandle16 handle) {
    return handle.handle_index != INVALID_ID_U16 && handle.generation != INVALID_ID_U16;
}

void khandle16_update(khandle16* handle) {
    if (handle) {
        handle->generation++;
        if (handle->generation == INVALID_ID_U16) {
            handle->generation = 0;
        }
    }
}

void khandle16_invalidate(khandle16* handle) {
    if (handle) {
        handle->handle_index = INVALID_ID_U16;
        handle->generation = INVALID_ID_U16;
    }
}

b8 khandle16_is_stale(khandle16 handle, u16 generation) {
    return handle.generation == generation;
}

b8 khandle16_is_pristine(khandle16 handle, u16 generation) {
    return handle.generation != generation;
}
