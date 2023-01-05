#pragma once

#include "defines.h"

typedef struct stack {
    u32 element_size;
    u32 element_count;
    u32 allocated;
    void* memory;
} stack;

KAPI b8 stack_create(stack* out_stack, u32 element_size);
KAPI void stack_destroy(stack* s);

KAPI b8 stack_push(stack* s, void* element_data);
KAPI b8 stack_pop(stack* s, void* out_element_data);
