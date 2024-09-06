#include "array.h"

#include "debug/kassert.h"
#include "memory/kmemory.h"

void _karray_init(u32 length, u32 stride, u32* out_length, u32* out_stride, void** block) {
    KASSERT_DEBUG(length);
    KASSERT_DEBUG(stride);
    KASSERT_DEBUG(out_length);
    KASSERT_DEBUG(out_stride);
    KASSERT_DEBUG(block);
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

array_iterator array_iterator_begin(const array_base* arr) {
    array_iterator it;
    it.arr = arr;
    it.pos = 0;
    it.dir = 1;
    it.end = array_iterator_end;
    it.value = array_iterator_value;
    it.next = array_iterator_next;
    it.prev = array_iterator_prev;
    return it;
}

array_iterator array_iterator_rbegin(const array_base* arr) {
    array_iterator it;
    it.arr = arr;
    it.pos = arr->length - 1;
    it.dir = -1;
    it.end = array_iterator_end;
    it.value = array_iterator_value;
    it.next = array_iterator_next;
    it.prev = array_iterator_prev;
    return it;
}

b8 array_iterator_end(const array_iterator* it) {
    return it->dir == 1 ? it->pos >= (i32)it->arr->length : it->pos < 0;
}

void* array_iterator_value(const array_iterator* it) {
    return (void*)(((u64)it->arr->p_data) + it->arr->stride * it->pos);
}

void array_iterator_next(array_iterator* it) {
    it->pos += it->dir;
}

void array_iterator_prev(array_iterator* it) {
    it->pos -= it->dir;
}
