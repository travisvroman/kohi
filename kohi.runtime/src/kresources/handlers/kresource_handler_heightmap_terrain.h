#pragma once

#include "kresources/kresource_types.h"

struct kresource_handler;
struct kresource_request_info;

KAPI b8 kresource_handler_heightmap_terrain_request(struct kresource_handler* self, kresource* resource, const struct kresource_request_info* info);
KAPI void kresource_handler_heightmap_terrain_release(struct kresource_handler* self, kresource* resource);
