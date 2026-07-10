#include "memory/allocators/dynamic_allocator.h"

#include "containers/freelist.h"
#include "debug/kassert.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

typedef struct dynamic_allocator_state {
	u64 total_size;
	freelist list;
	void *freelist_block;
	void *memory_block;
} dynamic_allocator_state;

#if MEM_DEBUG_TRACE
#	define MEM_GUARD_MAGIC_0 0xF00DF00DF00DF00D
#	define MEM_GUARD_MAGIC_1 0xBAD0BAD0BAD0BAD0

typedef struct memory_guard {
	u64 magic0;
	u64 magic1;
} memory_guard;
#endif

typedef struct alloc_header {
	void *start;
#if KOHI_DEBUG
	char file[256];
	u32 line;
#endif
	u16 alignment;
	u8 tag;
	u8 _pad;
} alloc_header;

// The storage size in bytes of a node's user memory block size
#define KSIZE_STORAGE sizeof(u32)

b8 dynamic_allocator_create (u64 total_size, u64 *memory_requirement, void *memory, dynamic_allocator *out_allocator) {
	if (total_size < 1) {
		KERROR("dynamic_allocator_create cannot have a total_size of 0. Create failed.");
		return false;
	}
	if (!memory_requirement) {
		KERROR("dynamic_allocator_create requires memory_requirement to exist. Create failed.");
		return false;
	}
	u64 freelist_requirement = 0;
	// Grab the memory requirement for the free list first.
	freelist_create(total_size, &freelist_requirement, 0, 0);

	*memory_requirement = freelist_requirement + sizeof(dynamic_allocator_state) + total_size;

	// If only obtaining requirement, boot out.
	if (!memory) {
		return true;
	}

	// Memory layout:
	// state
	// freelist block
	// memory block
	out_allocator->memory = memory;
	dynamic_allocator_state *state = out_allocator->memory;
	state->total_size = total_size;
	state->freelist_block = (void *)(out_allocator->memory + sizeof(dynamic_allocator_state));
	state->memory_block = (void *)(state->freelist_block + freelist_requirement);

	// Actually create the freelist
	freelist_create(total_size, &freelist_requirement, state->freelist_block, &state->list);

	kzero_memory(state->memory_block, total_size);
	return true;
}

b8 dynamic_allocator_destroy (dynamic_allocator *allocator) {
	if (allocator) {
		dynamic_allocator_state *state = allocator->memory;
		freelist_destroy(&state->list);
		kzero_memory(state->memory_block, state->total_size);
		state->total_size = 0;
		allocator->memory = 0;
		return true;
	}

	KWARN("dynamic_allocator_destroy requires a pointer to an allocator. Destroy failed.");
	return false;
}

void *dynamic_allocator_allocate (dynamic_allocator *allocator, u64 size, u8 tag, const char *file, u32 line) {
	return dynamic_allocator_allocate_aligned(allocator, size, 1, tag, file, line);
}

void *dynamic_allocator_allocate_aligned (dynamic_allocator *allocator, u64 size, u16 alignment, u8 tag, const char *file, u32 line) {
	if (allocator && size && alignment) {
		dynamic_allocator_state *state = allocator->memory;

		// The size required is based on the requested size, plus the alignment, header and a u32 to hold
		// the size for quick/easy lookups.
		u64 header_size = sizeof(alloc_header);
		u64 storage_size = KSIZE_STORAGE;
		u64 required_size = alignment + header_size + storage_size + size;
#if MEM_DEBUG_TRACE
		// Account for memory guards.
		required_size += (sizeof(memory_guard) * 2);
#endif

		// NOTE: This cast will really only be an issue on allocations over ~4GiB, so... don't do that.
		KASSERT_MSG(required_size < 4294967295U, "dynamic_allocator_allocate_aligned called with required size > 4 GiB. Don't do that.");

		u64 base_offset = 0;
		if (freelist_allocate_block(&state->list, required_size, &base_offset)) {
			/*
			Memory layout:
			x bytes/void padding
			4 bytes/u32 user block size
		#if MEM_DEBUG_TRACE
			sizeof(memory_guard) Before guard
		#endif
			x bytes/void user memory block
		#if MEM_DEBUG_TRACE
			sizeof(memory_guard) After guard
		#endif
			alloc_header

			*/

			// Get the base pointer, or the unaligned memory block.
			void *ptr = (void *)((u64)state->memory_block + base_offset);
			// Start the alignment after enough space to hold a u32. This allows for the u32 to be stored
			// immediately before the user block, while maintaining alignment on said user block.
			u64 aligned_block_offset = get_aligned((u64)ptr + KSIZE_STORAGE, alignment);
			KASSERT(aligned_block_offset >= (u64)state->memory_block);
			KASSERT(aligned_block_offset < ((u64)state->memory_block + state->total_size));
			// Store the size just before the user data block
			u32 *block_size = (u32 *)(aligned_block_offset - KSIZE_STORAGE);
			*block_size = (u32)size;
			KASSERT_MSG(size, "dynamic_allocator_allocate_aligned got a size of 0. Memory corruption likely as this should always be nonzero.");

#if MEM_DEBUG_TRACE
			{
				// If tracing, store a new guard just before the user data, but after the size.
				memory_guard before_guard = {
					.magic0 = MEM_GUARD_MAGIC_0,
					.magic1 = MEM_GUARD_MAGIC_1};
				u8 *source = (u8 *)&before_guard;
				u8 *target = (u8 *)aligned_block_offset;
				for (u64 i = 0; i < sizeof(before_guard); ++i) {
					target[i] = source[i];
				}

				// Move the aligned block offset to account for the guard.
				aligned_block_offset += sizeof(before_guard);
			}
#endif

			u64 after_guard_offset = 0;
#if MEM_DEBUG_TRACE
			{
				// If tracing, store a new guard just after the user data, but before the header data.
				memory_guard after_guard = {
					.magic0 = MEM_GUARD_MAGIC_0,
					.magic1 = MEM_GUARD_MAGIC_1};
				u8 *source = (u8 *)&after_guard;
				u8 *target = (u8 *)(aligned_block_offset + size);
				for (u64 i = 0; i < sizeof(after_guard); ++i) {
					target[i] = source[i];
				}

				after_guard_offset = sizeof(after_guard);
			}
#endif

			// Store the header immediately after the user block (or guard, if tracing).
			alloc_header *header = (alloc_header *)(aligned_block_offset + after_guard_offset + size);
			header->start = ptr;
			KASSERT_MSG(header->start, "dynamic_allocator_allocate_aligned got a null pointer (0x0). Memory corruption likely as this should always be nonzero.");
			header->alignment = alignment;
			KASSERT_MSG(header->alignment, "dynamic_allocator_allocate_aligned got an alignment of 0. Memory corruption likely as this should always be nonzero.");
			header->tag = tag;
#if KOHI_DEBUG
			string_ncopy(header->file, file, 255);
			header->file[255] = 0;
			header->line = line;
#endif

			return (void *)aligned_block_offset;
		} else {
			KERROR("dynamic_allocator_allocate_aligned no blocks of memory large enough to allocate from.");
			u64 available = freelist_free_space(&state->list);
			KERROR("Requested size: %llu, total space available: %llu", size, available);
			// TODO: Report fragmentation?
			return 0;
		}
	}

	KERROR("dynamic_allocator_allocate_aligned requires a valid allocator, size and alignment.");
	return 0;
}

b8 dynamic_allocator_free (dynamic_allocator *allocator, void *block, u64 size, u8 tag) {
	return dynamic_allocator_free_aligned(allocator, block, tag);
}

b8 dynamic_allocator_free_aligned (dynamic_allocator *allocator, void *block, u8 tag) {
	if (!allocator || !block) {
		KERROR("dynamic_allocator_free_aligned requires both a valid allocator (0x%p) and a block (0x%p) to be freed.", allocator, block);
		return false;
	}

	validate_block(block);

	dynamic_allocator_state *state = allocator->memory;
	if (block < state->memory_block || block > state->memory_block + state->total_size) {
		void *end_of_block = (void *)(state->memory_block + state->total_size);
		KWARN("dynamic_allocator_free_aligned trying to release block (0x%p) outside of allocator range (0x%p)-(0x%p)", block, state->memory_block, end_of_block);
		return false;
	}

	u64 guard_offset = 0;
#if MEM_DEBUG_TRACE
	guard_offset = sizeof(memory_guard);
#endif

	u32 *block_size = (u32 *)((u64)block - guard_offset - KSIZE_STORAGE);

	alloc_header *header = (alloc_header *)((u64)block + guard_offset + *block_size);
	u64 required_size = header->alignment + sizeof(alloc_header) + KSIZE_STORAGE + *block_size;

#if MEM_DEBUG_TRACE
	// Take guards into account on free.
	required_size += (sizeof(memory_guard) * 2);
#endif

	u64 offset = (u64)header->start - (u64)state->memory_block;
	if (!freelist_free_block(&state->list, required_size, offset)) {
		KERROR("dynamic_allocator_free_aligned failed.");
		return false;
	}

	return true;
}

b8 dynamic_allocator_get_size_alignment (dynamic_allocator *allocator, void *block, u64 *out_size, u16 *out_alignment, u8 *out_tag) {
	dynamic_allocator_state *state = allocator->memory;
	if (block < state->memory_block || block >= ((void *)((u8 *)state->memory_block) + state->total_size)) {
		// Not owned by this block.
		return false;
	}

	validate_block(block);

	u64 guard_offset = 0;
#if MEM_DEBUG_TRACE
	guard_offset = sizeof(memory_guard);
#endif

	// Get the header.
	*out_size = *(u32 *)((u64)block - guard_offset - KSIZE_STORAGE);
	KASSERT_MSG(*out_size, "dynamic_allocator_get_size_alignment found an out_size of 0. Memory corruption likely.");
	alloc_header *header = (alloc_header *)(((u64)block) + guard_offset + *out_size);
	*out_alignment = header->alignment;
	KASSERT_MSG(header->start, "dynamic_allocator_get_size_alignment found a header->start of 0. Memory corruption likely as this should always be at least 1.");
	KASSERT_MSG(header->alignment, "dynamic_allocator_get_size_alignment found a header->alignment of 0. Memory corruption likely as this should always be at least 1.");
	*out_tag = header->tag;
	return true;
}

#if KOHI_DEBUG
const char *dynamic_allocator_get_file (dynamic_allocator *allocator, void *block) {
	dynamic_allocator_state *state = allocator->memory;
	if (block < state->memory_block || block >= ((void *)((u8 *)state->memory_block) + state->total_size)) {
		// Not owned by this block.
		return false;
	}

	validate_block(block);

	u64 guard_offset = 0;
#	if MEM_DEBUG_TRACE
	guard_offset = sizeof(memory_guard);
#	endif

	// Get the header.
	u32 size = *(u32 *)((u64)block - guard_offset - KSIZE_STORAGE);
	alloc_header *header = (alloc_header *)((u64)block + size + guard_offset);

	return header->file;
}
u32 dynamic_allocator_get_line (dynamic_allocator *allocator, void *block) {
	dynamic_allocator_state *state = allocator->memory;
	if (block < state->memory_block || block >= ((void *)((u8 *)state->memory_block) + state->total_size)) {
		// Not owned by this block.
		return false;
	}

	validate_block(block);

	u64 guard_offset = 0;
#	if MEM_DEBUG_TRACE
	guard_offset = sizeof(memory_guard);
#	endif

	// Get the header.
	u32 size = *(u32 *)((u64)block - guard_offset - KSIZE_STORAGE);
	alloc_header *header = (alloc_header *)((u64)block + guard_offset + size);

	return header->line;
}
#endif

u64 dynamic_allocator_free_space (dynamic_allocator *allocator) {
	dynamic_allocator_state *state = allocator->memory;
	if (!state) {
		return 0;
	}
	return freelist_free_space(&state->list);
}

u64 dynamic_allocator_total_space (dynamic_allocator *allocator) {
	dynamic_allocator_state *state = allocator->memory;
	if (!state) {
		return 0;
	}
	return state->total_size;
}

u64 dynamic_allocator_header_size (void) {
	// Enough space for a header and size storage.
	return sizeof(alloc_header) + KSIZE_STORAGE;
}

#if MEM_DEBUG_TRACE
void _validate_block (void *block) {
	u64 guard_offset = 0;
	guard_offset = sizeof(memory_guard);

	u32 *block_size = (u32 *)((u64)block - guard_offset - KSIZE_STORAGE);

	// Get the header.
	alloc_header *header = (alloc_header *)((u64)block + *block_size + guard_offset);
	if (header) {
	}

	// If tracing, verify the guards weren't clobbered.
	memory_guard *before_guard = (memory_guard *)(((u8 *)block) - sizeof(memory_guard));
	KASSERT(before_guard->magic0 == MEM_GUARD_MAGIC_0);
	KASSERT(before_guard->magic1 == MEM_GUARD_MAGIC_1);

	memory_guard *after_guard = (memory_guard *)(((u8 *)block) + *block_size);
	KASSERT(after_guard->magic0 == MEM_GUARD_MAGIC_0);
	KASSERT(after_guard->magic1 == MEM_GUARD_MAGIC_1);
}
#endif
