#include "kasset_importer_registry.h"

#include "assets/kasset_utils.h"
#include "containers/darray.h"
#include "kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

typedef struct importer_asset_type_lookup {
    // darray of importers for a type.
    kasset_importer* importers;
} importer_asset_type_lookup;

typedef struct importer_registry_state {
    importer_asset_type_lookup types[KASSET_TYPE_MAX];
} importer_registry_state;

static importer_registry_state* state_ptr = 0;

b8 kasset_importer_registry_initialize(void) {
    state_ptr = kallocate(sizeof(importer_registry_state), MEMORY_TAG_ENGINE);

    return true;
}

void kasset_importer_registry_shutdown(void) {
    if (state_ptr) {
        for (u32 i = 0; i < KASSET_TYPE_MAX; ++i) {
            if (state_ptr->types[i].importers) {
                u32 count = darray_length(state_ptr->types[i].importers);
                for (u32 j = 0; j < count; ++j) {
                    kasset_importer* importer = &state_ptr->types[i].importers[j];
                    importer->import = 0;
                    if (importer->source_type) {
                        string_free(importer->source_type);
                        importer->source_type = 0;
                    }
                }
                darray_destroy(state_ptr->types[i].importers);
            }
        }
        kfree(state_ptr, sizeof(importer_registry_state), MEMORY_TAG_ENGINE);
        state_ptr = 0;
    }
}

b8 kasset_importer_registry_register(kasset_type type, const char* source_type, kasset_importer importer) {
    if (!state_ptr) {
        KERROR("Failed to register importer - import registry not yet initialized.");
        return false;
    }

    if (!source_type) {
        KERROR("Source type not defined while trying to register importer. Registration failed.");
        return false;
    }

    importer.source_type = string_duplicate(source_type);

    if (!importer.import) {
        KERROR("Function pointer 'import' not defined while trying to register importer. Registration failed.");
        return false;
    }

    if (!state_ptr->types[type].importers) {
        state_ptr->types[type].importers = darray_create(kasset_importer);
    }

    darray_push(state_ptr->types[type].importers, importer);

    return true;
}

const kasset_importer* kasset_importer_registry_get_for_source_type(kasset_type type, const char* source_type) {
    if (!state_ptr) {
        KERROR("Failed to get importer - import registry not yet initialized.");
        return 0;
    }

    if (!state_ptr->types[type].importers) {
        const char* asset_type_str = kasset_type_to_string(type);
        KERROR("No importers exist for type '%s'.", asset_type_str);
        string_free(asset_type_str);
        return 0;
    }

    u32 count = darray_length(state_ptr->types[type].importers);
    for (u32 i = 0; i < count; ++i) {
        kasset_importer* importer = &state_ptr->types[type].importers[i];
        if (strings_equali(importer->source_type, source_type)) {
            return importer;
        }
    }

    const char* asset_type_str = kasset_type_to_string(type);
    KERROR("No importer found for target type '%s' and source type '%s'.", asset_type_str, source_type);
    string_free(asset_type_str);
    return 0;
}
