#include "containers/darray.h"

#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"

void* _darray_create(u64 length, u64 stride, frame_allocator_int* allocator) {
    u64 header_size = sizeof(darray_header);
    u64 array_size = length * stride;
    void* new_array = 0;
    if (allocator) {
        new_array = allocator->allocate(header_size + array_size);
    } else {
        new_array = kallocate(header_size + array_size, MEMORY_TAG_DARRAY);
    }
    kset_memory(new_array, 0, header_size + array_size);
    if (length == 0) {
        KFATAL("_darray_create called with length of 0");
    }
    darray_header* header = new_array;
    header->capacity = length;
    header->length = 0;
    header->stride = stride;
    header->allocator = allocator;

    return (void*)((u8*)new_array + header_size);
}

void darray_destroy(void* array) {
    if (array) {
        u64 header_size = sizeof(darray_header);
        darray_header* header = (darray_header*)((u8*)array - header_size);
        u64 total_size = header_size + header->capacity * header->stride;
        if (header->allocator) {
            header->allocator->free(header, total_size);
        } else {
            kfree(header, total_size, MEMORY_TAG_DARRAY);
        }
    }
}

void* _darray_resize(void* array) {
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    if (header->capacity == 0) {
        KFATAL("_darray_resize called on an array with 0 capacity. This should not be possible.");
        return 0;
    }
    void* temp = _darray_create((DARRAY_RESIZE_FACTOR * header->capacity), header->stride, header->allocator);

    darray_header* new_header = (darray_header*)((u8*)temp - header_size);
    new_header->length = header->length;

    kcopy_memory(temp, array, header->length * header->stride);

    darray_destroy(array);
    return temp;
}

void* _darray_push(void* array, const void* value_ptr) {
    KASSERT_DEBUG(array);
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    if (header->length >= header->capacity) {
        array = _darray_resize(array);
        header = (darray_header*)((u8*)array - header_size);
    }

    u64 addr = (u64)array;
    addr += (header->length * header->stride);
    kcopy_memory((void*)addr, value_ptr, header->stride);
    darray_length_set(array, header->length + 1);
    return array;
}

void _darray_pop(void* array, void* dest) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (length < 1) {
        KERROR("darray_pop called on an empty darray. Nothing to be done.");
        return;
    }
    u64 addr = (u64)array;
    addr += ((length - 1) * stride);
    if (dest) {
        kcopy_memory(dest, (void*)addr, stride);
    }
    darray_length_set(array, length - 1);
}

void* darray_pop_at(void* array, u64 index, void* dest) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (index >= length) {
        KERROR("Index outside the bounds of this array! Length: %i, index: %index", length, index);
        return array;
    }

    u64 addr = (u64)array;
    if (dest) {
        kcopy_memory(dest, (void*)(addr + (index * stride)), stride);
    }

    // If not on the last element, snip out the entry and copy the rest inward.
    if (index != length - 1) {
        kcopy_memory(
            (void*)(addr + (index * stride)),
            (void*)(addr + ((index + 1) * stride)),
            stride * (length - (index - 1)));
    }

    darray_length_set(array, length - 1);
    return array;
}

void* _darray_insert_at(void* array, u64 index, void* value_ptr) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (index >= length) {
        KERROR("Index outside the bounds of this array! Length: %i, index: %index", length, index);
        return array;
    }
    if (length >= darray_capacity(array)) {
        array = _darray_resize(array);
    }

    u64 addr = (u64)array;

    // Push element(s) from index forward out by one. This should
    // even happen if inserted at the last index.
    kcopy_memory(
        (void*)(addr + ((index + 1) * stride)),
        (void*)(addr + (index * stride)),
        stride * (length - index));

    // Set the value at the index
    kcopy_memory((void*)(addr + (index * stride)), value_ptr, stride);

    darray_length_set(array, length + 1);
    return array;
}

void* _darray_duplicate(u64 stride, void* array) {
    u64 header_size = sizeof(darray_header);
    darray_header* source_header = (darray_header*)((u8*)array - header_size);

    KASSERT_MSG(stride == source_header->stride, "_darray_duplicate: target and source stride mismatch.");

    // "reserve" by passing current capacity, using the source allocator if there is one.
    void* copy = _darray_create(source_header->capacity, stride, source_header->allocator);
    darray_header* target_header = (darray_header*)((u8*)copy - header_size);
    KASSERT_MSG(target_header->capacity == source_header->capacity, "capacity mismatch while duplicating darray.");

    // Copy internal header fields.
    target_header->stride = source_header->stride;
    target_header->length = source_header->length;
    target_header->allocator = source_header->allocator;

    // Copy internal memory.
    kcopy_memory(copy, array, target_header->capacity * target_header->stride);

    return copy;
}

void darray_clear(void* array) {
    darray_length_set(array, 0);
}

u64 darray_capacity(void* array) {
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    return header->capacity;
}

u64 darray_length(void* array) {
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    return header->length;
}

u64 darray_stride(void* array) {
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    return header->stride;
}

void darray_length_set(void* array, u64 value) {
    u64 header_size = sizeof(darray_header);
    darray_header* header = (darray_header*)((u8*)array - header_size);
    header->length = value;
}

// NEW DARRAY
void _kdarray_init(u32 length, u32 stride, u32 capacity, struct frame_allocator_int* allocator, u32* out_length, u32* out_stride, u32* out_capacity, void** block, struct frame_allocator_int** out_allocator) {
    *out_length = length;
    *out_stride = stride;
    *out_capacity = capacity;
    *out_allocator = allocator;
    if (allocator) {
        *block = allocator->allocate(capacity * stride);
    } else {
        *block = kallocate(capacity * stride, MEMORY_TAG_DARRAY);
    }
}

void _kdarray_free(u32* length, u32* capacity, u32* stride, void** block, struct frame_allocator_int** out_allocator) {
    if (*out_allocator) {
        (*out_allocator)->free(*block, (*capacity) * (*stride));
    } else {
        kfree(*block, (*capacity) * (*stride), MEMORY_TAG_DARRAY);
    }
    *length = 0;
    *capacity = 0;
    *stride = 0;
    *block = 0;
    *out_allocator = 0;
}

void _kdarray_ensure_size(u32 required_length, u32 stride, u32* out_capacity, struct frame_allocator_int* allocator, void** block, void** base_block) {
    if (required_length > *out_capacity) {
        u32 new_capacity = KMAX(required_length, (*out_capacity) * DARRAY_RESIZE_FACTOR);
        if (allocator) {
            void* new_block = allocator->allocate(new_capacity * stride);
            kcopy_memory(new_block, *block, (*out_capacity) * stride);
            allocator->free(*block, (*out_capacity) * stride);
            *block = new_block;
        } else {
            *block = kreallocate(*block, (*out_capacity) * stride, new_capacity * stride, MEMORY_TAG_DARRAY);
        }
        *base_block = *block;
        *out_capacity = new_capacity;
    }
}

darray_iterator darray_iterator_begin(darray_base* arr) {
    darray_iterator it;
    it.arr = arr;
    it.pos = 0;
    it.dir = 1;
    it.end = darray_iterator_end;
    it.value = darray_iterator_value;
    it.next = darray_iterator_next;
    it.prev = darray_iterator_prev;
    return it;
}

darray_iterator darray_iterator_rbegin(darray_base* arr) {
    darray_iterator it;
    it.arr = arr;
    it.pos = arr->length - 1;
    it.dir = -1;
    it.end = darray_iterator_end;
    it.value = darray_iterator_value;
    it.next = darray_iterator_next;
    it.prev = darray_iterator_prev;
    return it;
}

b8 darray_iterator_end(const darray_iterator* it) {
    return it->dir == 1 ? it->pos >= (i32)it->arr->length : it->pos < 0;
}

void* darray_iterator_value(const darray_iterator* it) {
    return (void*)(((u64)it->arr->p_data) + (it->arr->stride * it->pos));
}

void darray_iterator_next(darray_iterator* it) {
    it->pos += it->dir;
}

void darray_iterator_prev(darray_iterator* it) {
    it->pos -= it->dir;
}
