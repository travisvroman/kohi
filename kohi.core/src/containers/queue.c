#include "queue.h"

#include "memory/kmemory.h"
#include "logger.h"

static void queue_ensure_allocated(queue* s, u32 count) {
    if (s->allocated < s->element_size * count) {
        void* temp = kallocate(count * s->element_size, MEMORY_TAG_ARRAY);
        if (s->memory) {
            kcopy_memory(temp, s->memory, s->allocated);
            kfree(s->memory, s->allocated, MEMORY_TAG_ARRAY);
        }
        s->memory = temp;
        s->allocated = count * s->element_size;
    }
}

b8 queue_create(queue* out_queue, u32 element_size) {
    if (!out_queue) {
        KERROR("queue_create requires a pointer to a valid queue.");
        return false;
    }

    kzero_memory(out_queue, sizeof(queue));
    out_queue->element_size = element_size;
    out_queue->element_count = 0;
    queue_ensure_allocated(out_queue, 1);
    return true;
}

void queue_destroy(queue* s) {
    if (s) {
        if (s->memory) {
            kfree(s->memory, s->allocated, MEMORY_TAG_ARRAY);
        }
        kzero_memory(s, sizeof(queue));
    }
}

b8 queue_push(queue* s, void* element_data) {
    if (!s) {
        KERROR("queue_push requires a pointer to a valid queue.");
        return false;
    }

    queue_ensure_allocated(s, s->element_count + 1);
    kcopy_memory((void*)((u64)s->memory + (s->element_count * s->element_size)), element_data, s->element_size);
    s->element_count++;
    return true;
}

b8 queue_peek(const queue* s, void* out_element_data) {
    if (!s || !out_element_data) {
        KERROR("queue_peek requires a pointer to a valid queue and to hold element data output.");
        return false;
    }

    if (s->element_count < 1) {
        KWARN("Cannot peek from an empty queue.");
        return false;
    }

    // Copy the front entry to out_element_data
    kcopy_memory(out_element_data, s->memory, s->element_size);

    return true;
}

b8 queue_pop(queue* s, void* out_element_data) {
    if (!s || !out_element_data) {
        KERROR("queue_pop requires a pointer to a valid queue and to hold element data output.");
        return false;
    }

    if (s->element_count < 1) {
        KWARN("Cannot pop from an empty queue.");
        return false;
    }

    // Copy the front entry to out_element_data
    kcopy_memory(out_element_data, s->memory, s->element_size);

    // Move everything "forward".
    kcopy_memory(s->memory, (void*)(((u64)s->memory) + s->element_size), s->element_size * (s->element_count - 1));

    s->element_count--;

    return true;
}
