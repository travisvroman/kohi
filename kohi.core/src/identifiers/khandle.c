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

void khandle_invalidate(khandle* handle) {
    if (handle) {
        handle->handle_index = INVALID_ID;
        handle->unique_id.uniqueid = INVALID_ID_U64;
    }
}
