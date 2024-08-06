#include "kasset_kson_serializer.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core_render_types.h"
#include "logger.h"
#include "parsers/kson_parser.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

const char* kasset_kson_serialize(const kasset* asset) {
    if (asset->type != KASSET_TYPE_KSON) {
        KERROR("kasset_kson_serialize requires a kson asset to serialize.");
        return 0;
    }

    kasset_kson* typed_asset = (kasset_kson*)asset;
    return kson_tree_to_string(&typed_asset->tree);
}

b8 kasset_kson_deserialize(const char* file_text, kasset* out_asset) {
    if (!file_text || !out_asset) {
        KERROR("kasset_kson_deserialize requires valid pointers to file_text and out_asset.");
        return false;
    }

    if (out_asset->type != KASSET_TYPE_KSON) {
        KERROR("kasset_kson_serialize requires a kson asset to serialize.");
        return 0;
    }

    kasset_kson* typed_asset = (kasset_kson*)out_asset;
    if (!kson_tree_from_string(file_text, &typed_asset->tree)) {
        KERROR("Failed to parse kson string. See logs for details.");
        return false;
    }

    return true;
}
