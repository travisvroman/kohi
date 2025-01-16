
#include "assets/kasset_types.h"

KAPI const char* kasset_system_font_serialize(const kasset* asset);

KAPI b8 kasset_system_font_deserialize(const char* file_text, kasset* out_asset);
