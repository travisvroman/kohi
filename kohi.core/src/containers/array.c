#include "array.h"

#include "memory/kmemory.h"

void _karray_init(u32 length, u32 stride, u32* out_length, u32* out_stride, void** block) {
    *out_length = length;
    *out_stride = stride;
    *block = kallocate_aligned(length * stride, 16, MEMORY_TAG_ARRAY);
}

void _karray_free(u32* length, u32* stride, void** block) {
    if (block && *block) {
        kfree_aligned(*block, (*length) * (*stride), 16, MEMORY_TAG_ARRAY);
        *length = 0;
        *stride = 0;
        *block = 0;
    }
}
