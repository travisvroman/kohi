#include "freelist.h"

#include "core/kmemory.h"
#include "core/logger.h"

typedef struct freelist_node {
    u64 offset;
    u64 size;
    struct freelist_node* next;
} freelist_node;

typedef struct internal_state {
    u64 total_size;
    u64 max_entries;
    freelist_node* head;
    freelist_node* nodes;
} internal_state;

freelist_node* get_node(freelist* list);
void return_node(freelist* list, freelist_node* node);

void freelist_create(u64 total_size, u64* memory_requirement, void* memory, freelist* out_list) {
    // Enough space to hold state, plus array for all nodes.
    u64 max_entries = (total_size / sizeof(void*));  //NOTE: This might have a remainder, but that's ok.
    *memory_requirement = sizeof(internal_state) + (sizeof(freelist_node) * max_entries);
    if (!memory) {
        return;
    }

    // If the memory required is too small, should warn about it being wasteful to use.
    u64 mem_min = (sizeof(internal_state) + sizeof(freelist_node)) * 8;
    if (total_size < mem_min) {
        KWARN(
            "Freelists are very inefficient with amounts of memory less than %iB; it is recommended to not use this structure in this case.",
            mem_min);
    }

    out_list->memory = memory;

    // The block's layout is head* first, then array of available nodes.
    kzero_memory(out_list->memory, *memory_requirement);
    internal_state* state = out_list->memory;
    state->nodes = (void*)(out_list->memory + sizeof(internal_state));
    state->max_entries = max_entries;
    state->total_size = total_size;

    state->head = &state->nodes[0];
    state->head->offset = 0;
    state->head->size = total_size;
    state->head->next = 0;

    // Invalidate the offset and size for all but the first node. The invalid
    // value will be checked for when seeking a new node from the list.
    for (u64 i = 1; i < state->max_entries; ++i) {
        state->nodes[i].offset = INVALID_ID;
        state->nodes[i].size = INVALID_ID;
    }
}

void freelist_destroy(freelist* list) {
    if (list && list->memory) {
        // Just zero out the memory before giving it back.
        internal_state* state = list->memory;
        kzero_memory(list->memory, sizeof(internal_state) + sizeof(freelist_node) * state->max_entries);
        list->memory = 0;
    }
}

b8 freelist_allocate_block(freelist* list, u64 size, u64* out_offset) {
    if (!list || !out_offset || !list->memory) {
        return false;
    }
    internal_state* state = list->memory;
    freelist_node* node = state->head;
    freelist_node* previous = 0;
    while (node) {
        if (node->size == size) {
            // Exact match. Just return the node.
            *out_offset = node->offset;
            freelist_node* node_to_return = 0;
            if (previous) {
                previous->next = node->next;
                node_to_return = node;
            } else {
                // This node is the head of the list. Reassign the head
                // and return the previous head node.
                node_to_return = state->head;
                state->head = node->next;
            }
            return_node(list, node_to_return);
            return true;
        } else if (node->size > size) {
            // Node is larger. Deduct the memory from it and move the offset
            // by that amount.
            *out_offset = node->offset;
            node->size -= size;
            node->offset += size;
            return true;
        }

        previous = node;
        node = node->next;
    }

    u64 free_space = freelist_free_space(list);
    KWARN("freelist_find_block, no block with enough free space found (requested: %lluB, available: %lluB).", size, free_space);
    return false;
}

b8 freelist_free_block(freelist* list, u64 size, u64 offset) {
    if (!list || !list->memory || !size) {
        return false;
    }
    internal_state* state = list->memory;
    freelist_node* node = state->head;
    freelist_node* previous = 0;
    while (node) {
        if (node->offset == offset) {
            // Can just be appended to this node.
            node->size += size;

            // Check if this then connects the range between this and the next
            // node, and if so, combine them and return the second node..
            if (node->next && node->next->offset == node->offset + node->size) {
                node->size += node->next->size;
                freelist_node* next = node->next;
                node->next = node->next->next;
                return_node(list, next);
            }
            return true;
        } else if (node->offset > offset) {
            // Iterated beyond the space to be freed. Need a new node.
            freelist_node* new_node = get_node(list);
            new_node->offset = offset;
            new_node->size = size;

            // If there is a previous node, the new node should be inserted between this and it.
            if (previous) {
                previous->next = new_node;
                new_node->next = node;
            } else {
                // Otherwise, the new node becomes the head.
                new_node->next = node;
                state->head = new_node;
            }

            // Double-check next node to see if it can be joined.
            if (new_node->next && new_node->offset + new_node->size == new_node->next->offset) {
                new_node->size += new_node->next->size;
                freelist_node* rubbish = new_node->next;
                new_node->next = rubbish->next;
                return_node(list, rubbish);
            }

            // Double-check previous node to see if the new_node can be joined to it.
            if (previous && previous->offset + previous->size == new_node->offset) {
                previous->size += new_node->size;
                freelist_node* rubbish = new_node;
                previous->next = rubbish->next;
                return_node(list, rubbish);
            }

            return true;
        }

        previous = node;
        node = node->next;
    }

    KWARN("Unable to find block to be freed. Corruption possible?");
    return false;
}

void freelist_clear(freelist* list) {
    if (!list || !list->memory) {
        return;
    }

    internal_state* state = list->memory;
    // Invalidate the offset and size for all but the first node. The invalid
    // value will be checked for when seeking a new node from the list.
    for (u64 i = 1; i < state->max_entries; ++i) {
        state->nodes[i].offset = INVALID_ID;
        state->nodes[i].size = INVALID_ID;
    }

    // Reset the head to occupy the entire thing.
    state->head->offset = 0;
    state->head->size = state->total_size;
    state->head->next = 0;
}

u64 freelist_free_space(freelist* list) {
    if (!list || !list->memory) {
        return 0;
    }

    u64 running_total = 0;
    internal_state* state = list->memory;
    freelist_node* node = state->head;
    while (node) {
        running_total += node->size;
        node = node->next;
    }

    return running_total;
}

freelist_node* get_node(freelist* list) {
    internal_state* state = list->memory;
    for (u64 i = 1; i < state->max_entries; ++i) {
        if (state->nodes[i].offset == INVALID_ID) {
            return &state->nodes[i];
        }
    }

    // Return nothing if no nodes are available.
    return 0;
}

void return_node(freelist* list, freelist_node* node) {
    node->offset = INVALID_ID;
    node->size = INVALID_ID;
    node->next = 0;
}
