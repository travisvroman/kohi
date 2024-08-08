#include "memory/kmemory.h"

#include "logger.h"
#include "platform/platform.h"
#include "strings/kstring.h"
#include "threads/kmutex.h"
#include "containers/freelist.h"

// TODO: Custom string lib
#include <stdio.h>
#include <string.h>

#define K_USE_CUSTOM_MEMORY_ALLOCATOR 1

#if !K_USE_CUSTOM_MEMORY_ALLOCATOR
#    if _MSC_VER
#        include <malloc.h>
#        define aligned_alloc _aligned_malloc
#        define aligned_free _aligned_free
#        define aligned_realloc _aligned_realloc
#    else
#        include <stdlib.h>
#    endif
#endif

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
    "ASSET      "};

// A allocation "header" to keep track of the base (unaligned) address,
// as well as the size, alignment and tag of an allocaton.
typedef struct alloc_header {
    // The start of the unaligned block of memory, before padding + size + userblock + header
    void* base_ptr;
    // Size of the allocation. Technically means max alloc size of ~4GiB, but a single allocation within
    // this arena shouldn't ever be that large anyway. This is guaranteed by an assertion during allocation.
    u32 size;
    // The alignment of the allocation.
    u16 alignment;
    // The tag of the allocation.
    u16 tag;
} alloc_header;

// The storage size in bytes of a node's user memory block size
#define KSIZE_STORAGE sizeof(u32)

struct memory_stats {
    // The total number of allocations.
    u64 alloc_count;
    // The total memory allocated.
    u64 total_allocated;
    // The total amount of space allocated.
    u64 total_size;
    // The number of allocations per tag.
    u64 tagged_allocations[MEMORY_TAG_MAX_TAGS];

    // The number of new allocations per tag since the last report was gathered.
    u64 new_tagged_allocations[MEMORY_TAG_MAX_TAGS];
    // The number of new deallocations per tag since the last report was gathered.
    u64 new_tagged_deallocations[MEMORY_TAG_MAX_TAGS];
};

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
typedef struct custom_allocator {
    // Freelist to track non-specialized allocations.
    freelist list;
    void* freelist_block;
    void* memory_block;
} custom_allocator;
#endif

typedef struct memory_system_state {
    memory_system_configuration config;
    struct memory_stats stats;

    // Required memory for the system.
    u64 memory_requirement;

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    // Custom allocator state, if used.
    custom_allocator allocator;
#endif

    // A mutex for allocations/frees
    kmutex allocation_mutex;
} memory_system_state;

// Pointer to system state.
static memory_system_state* state_ptr;

b8 memory_system_initialize(memory_system_configuration config) {

    // The amount needed by the system state.
    u64 state_memory_requirement = sizeof(memory_system_state);

    // Figure out how much space the dynamic allocator needs.
    u64 freelist_requirement = 0;
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    // Grab the memory requirement for the free list first.
    freelist_create(config.total_alloc_size, &freelist_requirement, 0, 0);
#endif
    // Total memory requirement for the system.
    u64 memory_requirement = state_memory_requirement + freelist_requirement + config.total_alloc_size;

    // Call the platform allocator to get the memory for the whole system, including the state and freelist.
    void* block = platform_allocate(memory_requirement, true);
    if (!block) {
        KFATAL("Memory system allocation failed and the system cannot continue.");
        return false;
    }

    // The state is in the first part of the massive block of memory.
    state_ptr = (memory_system_state*)block;
    state_ptr->config = config;
    platform_zero_memory(&state_ptr->stats, sizeof(state_ptr->stats));

    state_ptr->memory_requirement = memory_requirement;
    state_ptr->stats.total_size = config.total_alloc_size;
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    // The freelist block is in the same block of memory, but after the state.
    state_ptr->allocator.freelist_block = ((void*)block + state_memory_requirement);
    // The memory block is in the same block of memory, freelist block
    state_ptr->allocator.memory_block = state_ptr->allocator.freelist_block + freelist_requirement;

    // Actually create the freelist
    freelist_create(config.total_alloc_size, &freelist_requirement, state_ptr->allocator.freelist_block, &state_ptr->allocator.list);
#endif

    // Create allocation mutex
    if (!kmutex_create(&state_ptr->allocation_mutex)) {
        KFATAL("Unable to create allocation mutex!");
        return false;
    }

    KDEBUG("Memory system successfully allocated %llu bytes.", state_ptr->stats.total_size);
    return true;
}

void memory_system_shutdown(void) {
    if (state_ptr) {
        // Destroy allocation mutex
        kmutex_destroy(&state_ptr->allocation_mutex);

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        // Destroy the freelist when used.
        freelist_destroy(&state_ptr->allocator.list);
#endif
        // Free the entire block.
        platform_free(state_ptr, state_ptr->memory_requirement);
        aligned_free(state_ptr);
    }
    state_ptr = 0;
}

void* kallocate(u64 size, memory_tag tag) {
    return kallocate_aligned(size, 1, tag);
}

void* kallocate_aligned(u64 size, u16 alignment, memory_tag tag) {
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

        // TODO: pooling based on tag type.

        // Track aligned alloc offset as part of size.
        u64 header_size = sizeof(alloc_header);
        u64 storage_size = KSIZE_STORAGE;
        u64 required_size = alignment + header_size + storage_size + size;

        // The base/unaligned memory block.
        void* ptr = 0;

#if K_USE_CUSTOM_MEMORY_ALLOCATOR
        // The size required is based on the requested size, plus the alignment, header and a u32 to hold
        // the size for quick/easy lookups.

        // NOTE: This cast will really only be an issue on allocations over ~4GiB, so... don't do that.
        KASSERT_MSG(required_size < 4294967295U, "kallocate_aligned called with required size > 4 GiB. Don't do that.");

        u64 base_offset = 0;
        if (freelist_allocate_block(&state_ptr->allocator.list, required_size, &base_offset)) {
            /*
            Memory layout:
            x bytes/void padding
            4 bytes/u32 user block size
            x bytes/void user memory block
            alloc_header
            */

            // Get the base pointer, or the unaligned memory block.
            ptr = (void*)((u64)state_ptr->allocator.memory_block + base_offset);

        } else {
            KERROR("dynamic_allocator_allocate_aligned no blocks of memory large enough to allocate from.");
            u64 available = freelist_free_space(&state_ptr->allocator.list);
            KERROR("Requested size: %llu, total space available: %llu", size, available);
            // TODO: Report fragmentation?
            return 0;
        }
#else
        // Get the base pointer, or the unaligned memory block.
        // TODO: platform aligned_alloc
        ptr = platform_allocate(required_size, true);
#endif
        // Start the alignment after enough space to hold a u32. This allows for the u32 to be stored
        // immediately before the user block, while maintaining alignment on said user block.
        u64 aligned_block_offset = get_aligned((u64)ptr + KSIZE_STORAGE, alignment);
        // Store the size just before the user data block
        u32* block_size = (u32*)(aligned_block_offset - KSIZE_STORAGE);
        *block_size = (u32)size;
        // Store the header immediately after the user block.
        alloc_header* header = (alloc_header*)(aligned_block_offset + size);
        header->base_ptr = ptr;
        header->alignment = alignment;
        header->size = required_size;
        header->tag = (u16)tag;

        // The block of memory that will be returned.
        block = (void*)aligned_block_offset;

        // Report the allocation.
        state_ptr->stats.total_allocated += required_size;
        state_ptr->stats.tagged_allocations[tag] += required_size;
        state_ptr->stats.new_tagged_allocations[tag] += required_size;
        state_ptr->stats.alloc_count++;
        kmutex_unlock(&state_ptr->allocation_mutex);
    } else {
        // If the system is not up yet, warn about it but give memory for now.
        /* KTRACE("Warning: kallocate_aligned called before the memory system is initialized."); */
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
    state_ptr->stats.alloc_count++;
    state_ptr->stats.total_allocated += size;
    state_ptr->stats.tagged_allocations[tag] += size;
    state_ptr->stats.new_tagged_allocations[tag] += size;

    kmutex_unlock(&state_ptr->allocation_mutex);
}

void* kreallocate(void* block, u64 new_size, memory_tag tag) {
    return kreallocate_aligned(block, new_size, 1, tag);
}

void* kreallocate_aligned(void* block, u64 new_size, u16 alignment, memory_tag tag) {
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    if (block) {
        // Get the original size/alignment
        u64 osize = 0;
        u16 oalignment = 0;
        memory_tag tag;
        dynamic_allocator_get_size_alignment_tag(block, &osize, &oalignment, &tag);
        void* new_block = kallocate_aligned(new_size, alignment, tag);
        kcopy_memory(new_block, block, osize);
        kfree_aligned(block);
        return new_block;
    }
    return kallocate_aligned(new_size, alignment, tag);
#else
    return _aligned_realloc(block, new_size, alignment);
#endif
}

void kreallocate_report(u64 old_size, u64 new_size, memory_tag tag) {
    kfree_report(old_size, tag);
    kallocate_report(new_size, tag);
}

void kfree(void* block) {
    kfree_aligned(block);
}

void kfree_aligned(void* block) {

    if (state_ptr) {
        // Make sure multithreaded requests don't trample each other.
        if (!kmutex_lock(&state_ptr->allocation_mutex)) {
            KFATAL("Unable to obtain mutex lock for free operation. Heap corruption is likely.");
            return;
        }

        // Extract the header and size, etc.
        u32* block_size = (u32*)((u64)block - KSIZE_STORAGE);
        alloc_header* header = (alloc_header*)((u64)block + *block_size);
        u64 required_size = header->alignment + sizeof(alloc_header) + KSIZE_STORAGE + *block_size;
        u64 offset = (u64)header->base_ptr - (u64)state_ptr->allocator.memory_block;
#if K_USE_CUSTOM_MEMORY_ALLOCATOR

        // This validation only makes sense when using the custom allocator.
        if (block < state_ptr->allocator.memory_block || block > state_ptr->allocator.memory_block + state_ptr->stats.total_size) {
            void* end_of_block = (void*)(state_ptr->allocator.memory_block + state_ptr->stats.total_size);
            KWARN("kfree_aligned trying to release block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state_ptr->allocator.memory_block, end_of_block);

            if (!freelist_free_block(&state_ptr->allocator.list, required_size, offset)) {
                // If the free failed, it's possible this is because the allocation was made
                // before this system was started up. Since this absolutely should be an exception
                // to the rule, try freeing it on the platform level. If this fails, some other
                // brand of skulduggery is afoot, and we have bigger problems on our hands.
                platform_free(block, false);
            }
        } else {
            platform_free(block, false);
        }

#else
        platform_free(block, true);
#endif
        // Update stats
        state_ptr->stats.alloc_count--;
        state_ptr->stats.total_allocated -= required_size;
        state_ptr->stats.tagged_allocations[header->tag] -= required_size;
        state_ptr->stats.new_tagged_deallocations[header->tag] += required_size;

        kmutex_unlock(&state_ptr->allocation_mutex);

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
    state_ptr->stats.alloc_count--;
    state_ptr->stats.total_allocated -= size;
    state_ptr->stats.tagged_allocations[tag] -= size;
    state_ptr->stats.new_tagged_deallocations[tag] += size;

    kmutex_unlock(&state_ptr->allocation_mutex);
}

b8 kmemory_get_size_alignment_tag(void* block, u64* out_size, u16* out_alignment, memory_tag* out_tag) {
#if K_USE_CUSTOM_MEMORY_ALLOCATOR
    // This validation only makes sense when using the custom allocator.
    if (block < state_ptr->allocator.memory_block || block > state_ptr->allocator.memory_block + state_ptr->stats.total_size) {
        void* end_of_block = (void*)(state_ptr->allocator.memory_block + state_ptr->stats.total_size);
        KWARN("kmemory_get_size_alignment_tag trying to access block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state_ptr->allocator.memory_block, end_of_block);
        return false;
    }
#endif

    *out_size = *(u32*)((u64)block - KSIZE_STORAGE);
    alloc_header* header = (alloc_header*)((u64)block + *out_size);
    *out_alignment = header->alignment;
    *out_tag = header->tag;

    return true;
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
    // Compute total usage.

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

        u64 total_space = state_ptr->stats.total_size;
        u64 used_space = state_ptr->stats.total_allocated;
        u64 free_space = total_space - used_space;

        f32 used_amount = 1.0f;
        const char* used_unit = get_unit_for_size(used_space, &used_amount);

        f32 total_amount = 1.0f;
        const char* total_unit = get_unit_for_size(total_space, &total_amount);

        f64 percent_used = (f64)(used_space) / total_space;

        i32 length = snprintf(buffer + offset, 8000, "Total memory usage: %.2f%s of %.2f%s (%.2f%%)\n", used_amount, used_unit, total_amount, total_unit, percent_used);
        offset += length;
    }

    return string_duplicate(buffer);
}

u64 get_memory_alloc_count(void) {
    if (state_ptr) {
        return state_ptr->stats.alloc_count;
    }
    return 0;
}
