#include "memory/kmemory.h"

#include "debug/kassert.h"
#include "logger.h"
#include "memory/allocators/dynamic_allocator.h"
#include "platform/platform.h"
#include "strings/kstring.h"
#include "threads/kmutex.h"

// TODO: Custom string lib
#include <stdio.h>
#include <string.h>

#define K_USE_CUSTOM_MEMORY_ALLOCATOR 1

#if !K_USE_CUSTOM_MEMORY_ALLOCATOR
#    if _MSC_VER
#        include <malloc.h>
#        define kaligned_alloc _aligned_malloc
#        define kaligned_free _aligned_free
#    else
#        include <stdlib.h>
#        define kaligned_alloc(size, alignment) aligned_alloc(alignment, size)
#        define kaligned_free free
#    endif
#endif
struct memory_stats {
    u64 total_allocated;
    u64 tagged_allocations[MEMORY_TAG_MAX_TAGS];
    u64 new_tagged_allocations[MEMORY_TAG_MAX_TAGS];
    u64 new_tagged_deallocations[MEMORY_TAG_MAX_TAGS];
};

static const char* memory_tag_strings[MEMORY_TAG_MAX_TAGS] = {
    "UNKNOWN    ",
    "ARRAY      ",
    "LINEAR_ALLC",
    "DARRAY     ",
    "DICT       ",
    "RING_QUEUE ",
    "BST        ",
    "STRING     ",
    "ENGINE     ",
    "JOB        ",
    "TEXTURE    ",
    "MAT_INST   ",
    "RENDERER   ",
    "GAME       ",
    "TRANSFORM  ",
    "ENTITY     ",
    "ENTITY_NODE",
    "SCENE      ",
    "RESOURCE   ",
    "VULKAN     ",
    "VULKAN_EXT ",
    "DIRECT3D   ",
    "OPENGL     ",
    "GPU_LOCAL  ",
    "BITMAP_FONT",
    "SYSTEM_FONT",
    "KEYMAP     ",
    "HASHTABLE  ",
    "UI         ",
    "AUDIO      ",
    "REGISTRY   ",
    "PLUGIN     ",
    "PLATFORM   ",
    "SERIALIZER ",
    "ASSET      ",
    "XML        "};

typedef struct memory_system_state {
    memory_system_configuration config;
    struct memory_stats stats;
    u64 alloc_count;
    u64 allocator_memory_requirement;
    dynamic_allocator allocator;
    void* allocator_block;
    // A mutex for allocations/frees
    kmutex allocation_mutex;
} memory_system_state;

// Pointer to system state.
static memory_system_state* state_ptr;

b8 memory_system_initialize(memory_system_configuration config) {
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    // The amount needed by the system state.
    u64 state_memory_requirement = sizeof(memory_system_state);

    // Figure out how much space the dynamic allocator needs.
    u64 alloc_requirement = 0;
    dynamic_allocator_create(config.total_alloc_size, &alloc_requirement, 0, 0);

    // Call the platform allocator to get the memory for the whole system, including the state.
    // TODO: memory alignment
    void* block = platform_allocate(state_memory_requirement + alloc_requirement, true);
    if (!block) {
        KFATAL("Memory system allocation failed and the system cannot continue.");
        return false;
    }

    // The state is in the first part of the massive block of memory.
    state_ptr = (memory_system_state*)block;
    state_ptr->config = config;
    state_ptr->alloc_count = 0;
    state_ptr->allocator_memory_requirement = alloc_requirement;
    platform_zero_memory(&state_ptr->stats, sizeof(state_ptr->stats));
    // The allocator block is in the same block of memory, but after the state.
    state_ptr->allocator_block = ((void*)block + state_memory_requirement);

    if (!dynamic_allocator_create(
            config.total_alloc_size,
            &state_ptr->allocator_memory_requirement,
            state_ptr->allocator_block,
            &state_ptr->allocator)) {
        KFATAL("Memory system is unable to setup internal allocator. Application cannot continue.");
        return false;
    }
#else
    state_ptr = kaligned_alloc(sizeof(memory_system_state), 16);
    state_ptr->config = config;
    state_ptr->alloc_count = 0;
    state_ptr->allocator_memory_requirement = 0;
#endif

    // Create allocation mutex
    if (!kmutex_create(&state_ptr->allocation_mutex)) {
        KFATAL("Unable to create allocation mutex!");
        return false;
    }

    KDEBUG("Memory system successfully allocated %llu bytes.", config.total_alloc_size);
    return true;
}

void memory_system_shutdown(void) {
    if (state_ptr) {
        // Destroy allocation mutex
        kmutex_destroy(&state_ptr->allocation_mutex);

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        dynamic_allocator_destroy(&state_ptr->allocator);
        // Free the entire block.
        platform_free(state_ptr, state_ptr->allocator_memory_requirement + sizeof(memory_system_state));
#else
        kaligned_free(state_ptr);
#endif
    }
    state_ptr = 0;
}

void* kallocate(u64 size, memory_tag tag) {
    return kallocate_aligned(size, 1, tag);
}

void* kallocate_aligned(u64 size, u16 alignment, memory_tag tag) {
    KASSERT_MSG(size, "kallocate_aligned requires a nonzero size.");
    if (tag == MEMORY_TAG_UNKNOWN) {
        KWARN("kallocate_aligned called using MEMORY_TAG_UNKNOWN. Re-class this allocation.");
    }

    // Either allocate from the system's allocator or the OS. The latter shouldn't ever
    // really happen.
    void* block = 0;
    if (state_ptr) {
        // Make sure multithreaded requests don't trample each other.
        if (!kmutex_lock(&state_ptr->allocation_mutex)) {
            KFATAL("Error obtaining mutex lock during allocation.");
            return 0;
        }

        // FIXME: Track aligned alloc offset as part of size.
        state_ptr->stats.total_allocated += size;
        state_ptr->stats.tagged_allocations[tag] += size;
        state_ptr->stats.new_tagged_allocations[tag] += size;
        state_ptr->alloc_count++;

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        block = dynamic_allocator_allocate_aligned(&state_ptr->allocator, size, alignment);
#else
        block = kaligned_alloc(size, alignment);
#endif
        kmutex_unlock(&state_ptr->allocation_mutex);
    } else {
        // If the system is not up yet, warn about it but give memory for now.
        /* KTRACE("Warning: kallocate_aligned called before the memory system is initialized."); */
        // TODO: Memory alignment
        block = platform_allocate(size, false);
    }

    if (block) {
        platform_zero_memory(block, size);
        return block;
    }

    KFATAL("kallocate_aligned failed to allocate successfully.");
    return 0;
}

void kallocate_report(u64 size, memory_tag tag) {
    // Make sure multithreaded requests don't trample each other.
    if (!kmutex_lock(&state_ptr->allocation_mutex)) {
        KFATAL("Error obtaining mutex lock during allocation reporting.");
        return;
    }
    state_ptr->stats.total_allocated += size;
    state_ptr->stats.tagged_allocations[tag] += size;
    state_ptr->stats.new_tagged_allocations[tag] += size;
    state_ptr->alloc_count++;
    kmutex_unlock(&state_ptr->allocation_mutex);
}

void* kreallocate(void* block, u64 old_size, u64 new_size, memory_tag tag) {
    return kreallocate_aligned(block, old_size, new_size, 1, tag);
}

void* kreallocate_aligned(void* block, u64 old_size, u64 new_size, u16 alignment, memory_tag tag) {
    void* new_block = kallocate_aligned(new_size, alignment, tag);
    if (block && new_block) {
        kcopy_memory(new_block, block, old_size);
        kfree_aligned(block, old_size, alignment, tag);
    }
    return new_block;
}

void kreallocate_report(u64 old_size, u64 new_size, memory_tag tag) {
    kfree_report(old_size, tag);
    kallocate_report(new_size, tag);
}

void kfree(void* block, u64 size, memory_tag tag) {
    kfree_aligned(block, size, 1, tag);
}

void kfree_aligned(void* block, u64 size, u16 alignment, memory_tag tag) {
    if (tag == MEMORY_TAG_UNKNOWN) {
        KWARN("kfree_aligned called using MEMORY_TAG_UNKNOWN. Re-class this allocation.");
    }
    if (state_ptr) {
        // Make sure multithreaded requests don't trample each other.
        if (!kmutex_lock(&state_ptr->allocation_mutex)) {
            KFATAL("Unable to obtain mutex lock for free operation. Heap corruption is likely.");
            return;
        }

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        u64 osize = 0;
        u16 oalignment = 0;
        dynamic_allocator_get_size_alignment(&state_ptr->allocator, block, &osize, &oalignment);
        if (osize != size) {
            printf("Free size mismatch! (original=%llu, requested=%llu)\n", osize, size);
        }
        if (oalignment != alignment) {
            printf("Free alignment mismatch! (original=%hu, requested=%hu)\n", oalignment, alignment);
        }
#endif

        state_ptr->stats.total_allocated -= size;
        state_ptr->stats.tagged_allocations[tag] -= size;
        state_ptr->stats.new_tagged_deallocations[tag] += size;
        state_ptr->alloc_count--;
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        b8 result = dynamic_allocator_free_aligned(&state_ptr->allocator, block);
#else
        kaligned_free(block);
        b8 result = true;
#endif

        kmutex_unlock(&state_ptr->allocation_mutex);

        // If the free failed, it's possible this is because the allocation was made
        // before this system was started up. Since this absolutely should be an exception
        // to the rule, try freeing it on the platform level. If this fails, some other
        // brand of skulduggery is afoot, and we have bigger problems on our hands.
        if (!result) {
            // TODO: Memory alignment
            platform_free(block, false);
        }
    } else {
        // TODO: Memory alignment
        platform_free(block, false);
    }
}

void kfree_report(u64 size, memory_tag tag) {
    // Make sure multithreaded requests don't trample each other.
    if (!kmutex_lock(&state_ptr->allocation_mutex)) {
        KFATAL("Error obtaining mutex lock during allocation reporting.");
        return;
    }
    state_ptr->stats.total_allocated -= size;
    state_ptr->stats.tagged_allocations[tag] -= size;
    state_ptr->stats.new_tagged_deallocations[tag] += size;
    state_ptr->alloc_count--;
    kmutex_unlock(&state_ptr->allocation_mutex);
}

b8 kmemory_get_size_alignment(void* block, u64* out_size, u16* out_alignment) {
    if (!kmutex_lock(&state_ptr->allocation_mutex)) {
        KFATAL("Error obtaining mutex lock during kmemory_get_size_alignment.");
        return false;
    }
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    b8 result = dynamic_allocator_get_size_alignment(&state_ptr->allocator, block, out_size, out_alignment);
#else
    *out_size = 0;
    *out_alignment = 1;
    b8 result = true;
#endif
    kmutex_unlock(&state_ptr->allocation_mutex);
    return result;
}

void* kzero_memory(void* block, u64 size) {
    return platform_zero_memory(block, size);
}

void* kcopy_memory(void* dest, const void* source, u64 size) {
    return platform_copy_memory(dest, source, size);
}

void* kset_memory(void* dest, i32 value, u64 size) {
    return platform_set_memory(dest, value, size);
}

const char* get_unit_for_size(u64 size_bytes, f32* out_amount) {
    if (size_bytes >= GIBIBYTES(1)) {
        *out_amount = (f64)size_bytes / GIBIBYTES(1);
        return "GiB";
    } else if (size_bytes >= MEBIBYTES(1)) {
        *out_amount = (f64)size_bytes / MEBIBYTES(1);
        return "MiB";
    } else if (size_bytes >= KIBIBYTES(1)) {
        *out_amount = (f64)size_bytes / KIBIBYTES(1);
        return "KiB";
    } else {
        *out_amount = (f32)size_bytes;
        return "B";
    }
}

char* get_memory_usage_str(void) {
    char buffer[8000] = "System memory use (tagged):\n";
    u64 offset = strlen(buffer);
    for (u32 i = 0; i < MEMORY_TAG_MAX_TAGS; ++i) {
        f32 amounts[3] = {1.0f, 1.0f, 1.0f};
        const char* units[3] = {
            get_unit_for_size(state_ptr->stats.tagged_allocations[i], &amounts[0]),
            get_unit_for_size(state_ptr->stats.new_tagged_allocations[i], &amounts[1]),
            get_unit_for_size(state_ptr->stats.new_tagged_deallocations[i], &amounts[2])};

        i32 length = snprintf(buffer + offset, 8000, "  %s: %-7.2f %-3s [+ %-7.2f %-3s | - %-7.2f%-3s]\n",
                              memory_tag_strings[i],
                              amounts[0], units[0], amounts[1], units[1], amounts[2], units[2]);
        offset += length;
    }
    kzero_memory(&state_ptr->stats.new_tagged_allocations, sizeof(state_ptr->stats.new_tagged_allocations));
    kzero_memory(&state_ptr->stats.new_tagged_deallocations, sizeof(state_ptr->stats.new_tagged_deallocations));
    {
// Compute total usage.
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        u64 total_space = dynamic_allocator_total_space(&state_ptr->allocator);
        u64 free_space = dynamic_allocator_free_space(&state_ptr->allocator);
        u64 used_space = total_space - free_space;
#else
        u64 total_space = 0;
        // u64 free_space = 0;
        u64 used_space = 0;
#endif

        f32 used_amount = 1.0f;
        const char* used_unit = get_unit_for_size(used_space, &used_amount);

        f32 total_amount = 1.0f;
        const char* total_unit = get_unit_for_size(total_space, &total_amount);

        f64 percent_used = (f64)(used_space) / total_space;

        i32 length = snprintf(buffer + offset, 8000, "Total memory usage: %.2f%s of %.2f%s (%.2f%%)\n", used_amount, used_unit, total_amount, total_unit, percent_used);
        offset += length;
    }

    char* out_string = string_duplicate(buffer);
    return out_string;
}

u64 get_memory_alloc_count(void) {
    if (state_ptr) {
        return state_ptr->alloc_count;
    }
    return 0;
}

u32 pack_u8_into_u32(u8 x, u8 y, u8 z, u8 w) {
    return (x << 24) | (y << 16) | (z << 8) | (w);
}

b8 unpack_u8_from_u32(u32 n, u8* x, u8* y, u8* z, u8* w) {
    if (!x || !y || !z || !w) {
        return false;
    }

    *x = (n >> 24) & 0xFF;
    *y = (n >> 16) & 0xFF;
    *z = (n >> 8) & 0xFF;
    *w = n & 0xFF;

    return true;
}
