#include "stack.h"

#include "core/kmemory.h"
#include "core/logger.h"

static void stack_ensure_allocated(stack* s, u32 count) {
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

b8 stack_create(stack* out_stack, u32 element_size) {
    if (!out_stack) {
        KERROR("stack_create requires a pointer to a valid stack.");
        return false;
    }

    kzero_memory(out_stack, sizeof(stack));
    out_stack->element_size = element_size;
    out_stack->element_count = 0;
    stack_ensure_allocated(out_stack, 1);
    return true;
}

void stack_destroy(stack* s) {
    if (s) {
        if (s->memory) {
            kfree(s->memory, s->allocated, MEMORY_TAG_ARRAY);
        }
        kzero_memory(s, sizeof(stack));
    }
}

b8 stack_push(stack* s, void* element_data) {
    if (!s) {
        KERROR("stack_push requires a pointer to a valid stack.");
        return false;
    }

    stack_ensure_allocated(s, s->element_count + 1);
    kcopy_memory((void*)((u64)s->memory + (s->element_count * s->element_size)), element_data, s->element_size);
    s->element_count++;
    return true;
}

b8 stack_peek(const stack* s, void* out_element_data) {
    if (!s || !out_element_data) {
        KERROR("stack_peek requires a pointer to a valid stack and to hold element data output.");
        return false;
    }

    if (s->element_count < 1) {
        KWARN("Cannot peek from an empty stack.");
        return false;
    }

    kcopy_memory(out_element_data, (void*)((u64)s->memory + ((s->element_count - 1) * s->element_size)), s->element_size);

    return true;
}

b8 stack_pop(stack* s, void* out_element_data) {
    if (!s || !out_element_data) {
        KERROR("stack_pop requires a pointer to a valid stack and to hold element data output.");
        return false;
    }

    if (s->element_count < 1) {
        KWARN("Cannot pop from an empty stack.");
        return false;
    }

    kcopy_memory(out_element_data, (void*)((u64)s->memory + ((s->element_count - 1) * s->element_size)), s->element_size);

    s->element_count--;

    return true;
}
