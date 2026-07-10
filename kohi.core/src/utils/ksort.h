#pragma once

#include "defines.h"

typedef i32 (*PFN_kquicksort_compare)(void *a, void *b);
typedef i32 (*PFN_kquicksort_compare_with_context)(void *a, void *b, void *context);

KAPI void ptr_swap (void *scratch_mem, u64 size, void *a, void *b);

KAPI void kquick_sort (u64 type_size, void *data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn);

KAPI void kquick_sort_with_context (u64 type_size, void *data, i32 low_index, i32 high_index, PFN_kquicksort_compare_with_context compare_pfn, void *context);

KAPI i32 kquicksort_compare_u32_desc (void *a, void *b);
KAPI i32 kquicksort_compare_u32 (void *a, void *b);
