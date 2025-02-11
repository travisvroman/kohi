#pragma once

#include "defines.h"

typedef struct khashmap_entry {
    u64 key;
    u32 value;
} khashmap_entry;

typedef struct khashmap {
    khashmap_entry* entries;
    u32 capacity;
    u32 count;
    u32 tombstone_count;
} khashmap;

KAPI b8 khashmap_create(khashmap* map);

KAPI void khashmap_destroy(khashmap* map);

KAPI b8 khashmap_set(khashmap* map, u64 key, u32 value);

KAPI b8 khashmap_get(const khashmap* map, u64 key, u32* out_value);

KAPI b8 khashmap_remove(khashmap* map, u64 key);
