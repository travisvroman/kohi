#pragma once

#include "defines.h"

typedef i32 (*PFN_kquicksort_compare)(void* a, void* b);

KAPI void ptr_swap(void* scratch_mem, u64 size, void* a, void* b);

KAPI void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn);
