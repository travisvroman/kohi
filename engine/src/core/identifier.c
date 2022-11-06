#include "identifier.h"

#include "containers/darray.h"
#include "core/logger.h"

static void** owners = 0;

u32 identifier_aquire_new_id(void* owner) {
    if (!owners) {
        owners = darray_reserve(void*, 100);
        // Push invalid id to the first entry. This is to keep
        // index 0 from ever being used.
        darray_push(owners, INVALID_ID_U64);
    }
    u64 length = darray_length(owners);
    for (u64 i = 0; i < length; ++i) {
        // Existing free spot. Take it.
        if (owners[i] == 0) {
            owners[i] = owner;
            return i;
        }
    }

    // If here, no existing free slots. Need a new id, so push one.
    // This means the id will be length - 1
    darray_push(owners, owner);
    length = darray_length(owners);
    return length - 1;
}

void identifier_release_id(u32 id) {
    if (!owners) {
        KERROR("identifier_release_id called before initialization. identifier_aquire_new_id should have been called first. Nothing was done.");
        return;
    }

    u64 length = darray_length(owners);
    if (id > length) {
        KERROR("identifier_release_id: id '%u' out of range (max=%llu). Nothing was done.", id, length);
        return;
    }

    // Just zero out the entry, making it available for use.
    owners[id] = 0;
}
