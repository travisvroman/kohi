#include "khandle.h"

#include "core/identifier.h"
#include "defines.h"

k_handle k_handle_create(u32 handle_index) {
    k_handle out_handle = {0};
    out_handle.handle_index = handle_index;
    out_handle.unique_id = identifier_create();
    return out_handle;
}

k_handle k_handle_create_with_identifier(u32 handle_index, identifier id) {
    k_handle out_handle = {0};
    out_handle.handle_index = handle_index;
    out_handle.unique_id = id;
    return out_handle;
}

k_handle k_handle_invalid(void) {
    k_handle out_handle = {0};
    out_handle.handle_index = INVALID_ID;
    out_handle.unique_id.uniqueid = INVALID_ID_U64;
    return out_handle;
}

b8 k_handle_is_invalid(k_handle handle) {
    return handle.handle_index == INVALID_ID || handle.unique_id.uniqueid == INVALID_ID_U64;
}

void k_handle_invalidate(k_handle* handle) {
    if (handle) {
        handle->handle_index = INVALID_ID;
        handle->unique_id.uniqueid = INVALID_ID_U64;
    }
}
