#pragma once

#include "kresources/kresource_types.h"

struct kresource_handler;

KAPI b8 kresource_handler_texture_request(struct kresource_handler* self, kresource* resource, kresource_request_info info);
KAPI void kresource_handler_texture_release(struct kresource_handler* self, kresource* resource);
