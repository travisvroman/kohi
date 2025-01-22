
#pragma once

#include "kresources/kresource_types.h"

struct kresource_handler;
struct kresource_request_info;

KAPI b8 kresource_handler_text_request(struct kresource_handler* self, kresource* resource, const struct kresource_request_info* info);
KAPI void kresource_handler_text_release(struct kresource_handler* self, kresource* resource);
KAPI b8 kresource_handler_text_handle_hot_reload(struct kresource_handler* self, kresource* resource, kasset* asset, u32 file_watch_id);
