#include "khashmap.h"
#include "memory/kmemory.h"

#define KHASHMAP_INITIAL_CAPACITY 64
#define KHASHMAP_LOAD_FACTOR 0.7
#define KHASHMAP_TOMBSTONE INVALID_ID

static u64 hash_u64(u64 key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccd;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53;
    key ^= key >> 33;
    return key;
}

static void khashmap_resize(khashmap* map) {
    u32 new_capacity = map->capacity * 2;
    khashmap_entry* new_entries = KALLOC_TYPE_CARRAY(khashmap_entry, new_capacity);

    for (u32 i = 0; i < map->capacity; ++i) {
        if (map->entries[i].key && map->entries[i].value != KHASHMAP_TOMBSTONE) {
            u64 key = map->entries[i].key;
            u32 value = map->entries[i].value;

            u32 index = hash_u64(key) % new_capacity;
            while (new_entries[index].key) {
                index = (index + 1) % new_capacity;
            }
            new_entries[index].key = key;
            new_entries[index].value = value;
        }

        KFREE_TYPE_CARRAY(map->entries, khashmap_entry, map->capacity);
        map->entries = new_entries;
        map->capacity = new_capacity;
        map->tombstone_count = 0;
    }
}

b8 khashmap_create(khashmap* map) {
    if (!map) {
        return false;
    }

    map->capacity = KHASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->tombstone_count = 0;
    map->entries = KALLOC_TYPE_CARRAY(khashmap_entry, map->capacity);

    return true;
}

void khashmap_destroy(khashmap* map) {
    if (map) {
        KFREE_TYPE_CARRAY(map->entries, khashmap_entry, map->capacity);
        map->entries = 0;
        map->capacity = 0;
        map->count = 0;
        map->tombstone_count = 0;
    }
}

b8 khashmap_set(khashmap* map, u64 key, u32 value) {
    if (!map) {
        return false;
    }

    if ((map->count + map->tombstone_count) >= (map->capacity * KHASHMAP_LOAD_FACTOR)) {
        khashmap_resize(map);
    }

    u32 index = hash_u64(key) % map->capacity;
    u32 tombstone_index = KHASHMAP_TOMBSTONE;

    while (map->entries[index].key) {
        if (map->entries[index].key == key) {
            // Update existing entry
            map->entries[index].value = value;
            return true;
        }
        if (map->entries[index].value == KHASHMAP_TOMBSTONE && tombstone_index == KHASHMAP_TOMBSTONE) {
            // Store first tombstone location.
            tombstone_index = index;
        }

        index = (index + 1) % map->capacity;
    }

    if (tombstone_index != KHASHMAP_TOMBSTONE) {
        // Use a tombstone slot
        index = tombstone_index;
        map->tombstone_count--;
    } else {
        map->count++;
    }

    map->entries[index].key = key;
    map->entries[index].value = value;

    return true;
}

b8 khashmap_get(const khashmap* map, u64 key, u32* out_value) {
    if (!map) {
        return false;
    }

    u32 index = hash_u64(key) % map->capacity;

    while (map->entries[index].key) {
        if (map->entries[index].key == key && map->entries[index].value != KHASHMAP_TOMBSTONE) {
            *out_value = map->entries[index].value;
            return true;
        }

        index = (index + 1) % map->capacity;
    }
    // not found.
    return false;
}

b8 khashmap_remove(khashmap* map, u64 key) {
    if (!map) {
        return false;
    }

    u32 index = hash_u64(key) % map->capacity;

    while (map->entries[index].key) {
        if (map->entries[index].key == key) {
            map->entries[index].value = KHASHMAP_TOMBSTONE;
            map->tombstone_count++;
            return true;
        }

        index = (index + 1) % map->capacity;
    }

    // Not found, so not removed.
    return false;
}
