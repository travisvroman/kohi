#include "ksort.h"

#include "core/kmemory.h"

void ptr_swap(void* scratch_mem, u64 size, void* a, void* b) {
    kcopy_memory(scratch_mem, a, size);
    kcopy_memory(a, b, size);
    kcopy_memory(b, scratch_mem, size);
}

static void* data_at_index(void* block, u64 element_size, u32 index) {
    u64 addr = (u64)block;
    addr += (element_size * index);
    return (void*)(addr);
}

static i32 kquick_sort_partition(void* scratch_mem, u64 size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn) {
    void* pivot = data_at_index(data, size, high_index);
    i32 i = (low_index - 1);

    for (i32 j = low_index; j <= high_index - 1; ++j) {
        void* dataj = data_at_index(data, size, j);
        i32 result = compare_pfn(dataj, pivot);
        if (result > 0) {
            ++i;
            void* datai = data_at_index(data, size, i);
            ptr_swap(scratch_mem, size, datai, dataj);
        }
    }
    ptr_swap(scratch_mem, size, data_at_index(data, size, i + 1), data_at_index(data, size, high_index));
    return i + 1;
}

static void kquick_sort_internal(void* scratch_mem, u64 size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn) {
    if (low_index < high_index) {
        i32 partition_index = kquick_sort_partition(scratch_mem, size, data, low_index, high_index, compare_pfn);
        kquick_sort_internal(scratch_mem, size, data, low_index, partition_index - 1, compare_pfn);
        kquick_sort_internal(scratch_mem, size, data, partition_index + 1, high_index, compare_pfn);
    }
}

void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn) {
    if (low_index < high_index) {
        void* scratch_mem = kallocate(type_size, MEMORY_TAG_ARRAY);
        i32 partition_index = kquick_sort_partition(scratch_mem, type_size, data, low_index, high_index, compare_pfn);
        kquick_sort_internal(scratch_mem, type_size, data, low_index, partition_index - 1, compare_pfn);
        kquick_sort_internal(scratch_mem, type_size, data, partition_index + 1, high_index, compare_pfn);
        kfree(scratch_mem, type_size, MEMORY_TAG_ARRAY);
    }
}
