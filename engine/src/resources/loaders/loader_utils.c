#include "loader_utils.h"

#include "core/kmemory.h"
#include "core/logger.h"
#include "core/kstring.h"

b8 resource_unload(struct resource_loader* self, resource* resource, memory_tag tag) {
    if (!self || !resource) {
        KWARN("resource_unload called with nullptr for self or resource.");
        return false;
    }

    if (resource->full_path) {
        u32 path_length = string_length(resource->full_path);
        if (path_length) {
            kfree(resource->full_path, sizeof(char) * path_length + 1, MEMORY_TAG_STRING);
        }
    }

    if (resource->data) {
        kfree(resource->data, resource->data_size, tag);
        resource->data = 0;
        resource->data_size = 0;
        resource->loader_id = INVALID_ID;
    }

    return true;
}