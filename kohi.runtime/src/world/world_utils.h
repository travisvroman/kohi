#pragma once

#include <defines.h>

#include "world/world_types.h"

#define kentity_pack(type, type_index, reserved_0, reserved_1) (kentity) PACK_U64_U16S(type, type_index, reserved_0, reserved_1)

#define kentity_unpack_type(entity) (kentity_type) UNPACK_U64_U16_AT(entity, 0)
#define kentity_unpack_type_index(entity) UNPACK_U64_U16_AT(entity, 1)
#define kentity_unpack_reserved(entity) UNPACK_U64_U16_AT(entity, 2)
#define kentity_unpack_reserved2(entity) UNPACK_U64_U16_AT(entity, 3)

#define kentity_unpack(entity, out_type, out_type_index, out_reserved, out_reserved2) UNPACK_U64_U16S(entity, out_type, out_type_index, out_reserved, out_reserved2)

KAPI kentity_type kentity_type_from_string (const char *str);

KAPI const char *kentity_type_to_string (kentity_type type);

KAPI b8 kentity_type_ignores_scale (kentity_type type);

KAPI kshape_type kshape_type_from_string (const char *str);

KAPI const char *kshape_type_to_string (kshape_type type);

KAPI kscene_volume_type scene_volume_type_from_string (const char *str);

KAPI const char *scene_volume_type_to_string (kscene_volume_type type);
