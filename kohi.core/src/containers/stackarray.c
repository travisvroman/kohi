#include "stackarray.h"

stackarray_iterator stackarray_iterator_begin(stackarray_base* arr) {
    stackarray_iterator it;
    it.arr = arr;
    it.pos = 0;
    it.dir = 1;
    it.end = stackarray_iterator_end;
    it.value = stackarray_iterator_value;
    it.next = stackarray_iterator_next;
    it.prev = stackarray_iterator_prev;
    return it;
}

stackarray_iterator stackarray_iterator_rbegin(stackarray_base* arr) {
    stackarray_iterator it;
    it.arr = arr;
    it.pos = arr->length - 1;
    it.dir = -1;
    it.end = stackarray_iterator_end;
    it.value = stackarray_iterator_value;
    it.next = stackarray_iterator_next;
    it.prev = stackarray_iterator_prev;
    return it;
}

b8 stackarray_iterator_end(const stackarray_iterator* it) {
    return it->dir == 1 ? it->pos >= (i32)it->arr->length : it->pos < 0;
}

void* stackarray_iterator_value(const stackarray_iterator* it) {
    return (void*)(((u64)it->arr->p_data) + it->arr->stride * it->pos);
}

void stackarray_iterator_next(stackarray_iterator* it) {
    it->pos += it->dir;
}

void stackarray_iterator_prev(stackarray_iterator* it) {
    it->pos -= it->dir;
}
