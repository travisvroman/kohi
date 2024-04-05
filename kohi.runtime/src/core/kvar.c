#include "kvar.h"

#include "core/event.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

#include "core/console.h"

typedef struct kvar_entry {
    kvar_types type;
    const char* name;
    const char* description;
    kvar_value value;
} kvar_entry;

// Max number of kvars.
#define KVAR_MAX_COUNT 256

typedef struct kvar_state {
    kvar_entry values[KVAR_MAX_COUNT];
} kvar_state;

static kvar_state* state_ptr;

static void kvar_console_commands_register(void);

b8 kvar_system_initialize(u64* memory_requirement, struct kvar_state* memory, void* config) {
    *memory_requirement = sizeof(kvar_state);

    if (!memory) {
        return true;
    }

    state_ptr = memory;

    kzero_memory(state_ptr, sizeof(kvar_state));

    kvar_console_commands_register();

    return true;
}

void kvar_system_shutdown(struct kvar_state* state) {
    if (state) {
        // Free resources.
        for (u32 i = 0; i < KVAR_MAX_COUNT; ++i) {
            kvar_entry* entry = &state->values[i];
            if (entry->name) {
                string_free(entry->name);
            }
            // If a string, free it.
            if (entry->type == KVAR_TYPE_STRING) {
                string_free(entry->value.s);
            }
        }
        kzero_memory(state, sizeof(kvar_state));
    }
    state_ptr = 0;
}

static kvar_entry* get_entry_by_name(kvar_state* state, const char* name) {
    // Check if kvar exists with the name.
    for (u32 i = 0; i < KVAR_MAX_COUNT; ++i) {
        kvar_entry* entry = &state->values[i];
        if (entry->name && strings_equali(name, entry->name)) {
            // Name matches, return.
            return entry;
        }
    }

    // No match found. Try getting a new one.
    for (u32 i = 0; i < KVAR_MAX_COUNT; ++i) {
        kvar_entry* entry = &state->values[i];
        if (!entry->name) {
            // No name means this one is free. Set its name here.
            entry->name = string_duplicate(name);
            return entry;
        }
    }

    KERROR("Unable to find existing kvar named '%s' and cannot create new kvar because the table has no room left.", name);
    return 0;
}

static b8 kvar_entry_set_desc_value(kvar_entry* entry, kvar_types type, const char* description, void* value) {
    // If a string, value will need to be freed before proceeding.
    if (entry->type == KVAR_TYPE_STRING) {
        if (entry->value.s) {
            string_free(entry->value.s);
        }
    }

    kvar_types old_type = entry->type;

    // Update the type.
    entry->type = type;

    // Update the value.
    switch (entry->type) {
    case KVAR_TYPE_STRING:
        entry->value.s = string_duplicate(value);
        break;
    case KVAR_TYPE_FLOAT:
        entry->value.f = *((f32*)value);
        break;
    case KVAR_TYPE_INT:
        entry->value.i = *((i32*)value);
        break;
    default:
        KFATAL("Trying to set a kvar with an unknown type. This should not happen unless a new type has been added.");
        return false;
    }

    // If a description was provided, update it
    if (description) {
        if (entry->description) {
            string_free(entry->description);
        }
        entry->description = string_duplicate(description);
    }

    // Send out a notification that the variable was changed.
    event_context context = {0};
    context.data.custom_data.size = sizeof(kvar_change);
    context.data.custom_data.data = kallocate(context.data.custom_data.size, MEMORY_TAG_UNKNOWN); // FIXME: event tag
    kvar_change* change_data = context.data.custom_data.data;
    change_data->name = entry->name;
    change_data->new_type = type;
    change_data->old_type = old_type;
    change_data->new_value = entry->value;

    event_fire(EVENT_CODE_KVAR_CHANGED, 0, context);
    return true;
}

b8 kvar_i32_get(const char* name, i32* out_value) {
    if (!state_ptr || !name) {
        return false;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        KERROR("kvar_int_get could not find a kvar named '%s'.", name);
        return false;
    }

    switch (entry->type) {
    case KVAR_TYPE_INT:
        // If int, set output as-is.
        *out_value = entry->value.i;
        return true;
    case KVAR_TYPE_FLOAT:
        // For float, just cast it, but warn about truncation.
        KWARN("The kvar '%s' is of type f32 but its value was requested as i32. This will result in a truncated value. Get the value as a float instead.", name);
        *out_value = (i32)entry->value.f;
        return true;
    case KVAR_TYPE_STRING:
        if (!string_to_i32(entry->value.s, out_value)) {
            KERROR("The kvar '%s' is of type string and could not successfully be parsed to i32. Get the value as a string instead.", name);
            return false;
        }
        return true;
    default:
        KERROR("The kvar '%s' is was found but is of an unknown type. This means an unsupported type exists or indicates memory corruption.", name);
        return false;
    }
}

b8 kvar_i32_set(const char* name, const char* desc, i32 value) {
    if (!state_ptr || !name) {
        return false;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        return false;
    }

    // Create/set the kvar.
    b8 result = kvar_entry_set_desc_value(entry, KVAR_TYPE_INT, desc, &value);
    if (!result) {
        KERROR("Failed to set kvar entry for kvar named '%s'. See logs for details.", name);
    }
    return result;
}

b8 kvar_f32_get(const char* name, f32* out_value) {
    if (!state_ptr || !name) {
        return false;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        KERROR("kvar_int_get could not find a kvar named '%s'.", name);
        return false;
    }

    switch (entry->type) {
    case KVAR_TYPE_INT:
        KWARN("The kvar '%s' is of type i32 but its value was requested as f32. It is recommended to get the value as int instead.", name);
        *out_value = (f32)entry->value.i;
        return true;
    case KVAR_TYPE_FLOAT:
        *out_value = entry->value.f;
        return true;
    case KVAR_TYPE_STRING:
        if (!string_to_f32(entry->value.s, out_value)) {
            KERROR("The kvar '%s' is of type string and could not successfully be parsed to f32. Get the value as a string instead.", name);
            return false;
        }
        return true;
    default:
        KERROR("The kvar '%s' is was found but is of an unknown type. This means an unsupported type exists or indicates memory corruption.", name);
        return false;
    }
}

b8 kvar_f32_set(const char* name, const char* desc, f32 value) {
    if (!state_ptr || !name) {
        return false;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        return false;
    }

    // Create/set the kvar.
    b8 result = kvar_entry_set_desc_value(entry, KVAR_TYPE_FLOAT, desc, &value);
    if (!result) {
        KERROR("Failed to set kvar entry for kvar named '%s'. See logs for details.", name);
    }
    return result;
}

const char* kvar_string_get(const char* name) {
    if (!state_ptr || !name) {
        return 0;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        KERROR("kvar_int_get could not find a kvar named '%s'.", name);
        return 0;
    }

    switch (entry->type) {
    case KVAR_TYPE_INT:
        KWARN("The kvar '%s' is of type i32 but its value was requested as string. It is recommended to get the value as int instead.", name);
        return i32_to_string(entry->value.i);
    case KVAR_TYPE_FLOAT:
        KWARN("The kvar '%s' is of type i32 but its value was requested as string. It is recommended to get the value as float instead.", name);
        return f32_to_string(entry->value.f);
    case KVAR_TYPE_STRING:
        return string_duplicate(entry->value.s);
    default:
        KERROR("The kvar '%s' is was found but is of an unknown type. This means an unsupported type exists or indicates memory corruption.", name);
        return 0;
    }
}

b8 kvar_string_set(const char* name, const char* desc, const char* value) {
    if (!state_ptr || !name) {
        return false;
    }

    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        return false;
    }

    // Create/set the kvar.
    b8 result = kvar_entry_set_desc_value(entry, KVAR_TYPE_STRING, desc, &value);
    if (!result) {
        KERROR("Failed to set kvar entry for kvar named '%s'. See logs for details.", name);
    }
    return result;
}

static void kvar_print(kvar_entry* entry, b8 include_name) {
    if (!entry) {
        char name_equals[512] = {0};
        if (include_name) {
            string_format(name_equals, "%s = ", entry->name);
        }
        switch (entry->type) {
        case KVAR_TYPE_INT:
            KINFO("%s%i", name_equals, entry->value.i);
            break;
        case KVAR_TYPE_FLOAT:
            KINFO("%s%f", name_equals, entry->value.f);
            break;
        case KVAR_TYPE_STRING:
            KINFO("%s%s", name_equals, entry->value.s);
            break;
        default:
            KERROR("kvar '%s' has an unknown type. Possible corruption?");
            break;
        }
    }
}

static void kvar_console_command_print(console_command_context context) {
    if (context.argument_count != 1) {
        KERROR("kvar_console_command_print requires a context arg count of 1.");
        return;
    }

    const char* name = context.arguments[0].value;
    kvar_entry* entry = get_entry_by_name(state_ptr, name);
    if (!entry) {
        KERROR("Unable to find kvar named '%s'.", name);
        return;
    }

    kvar_print(entry, false);
}

static void kvar_set_by_str(const char* name, const char* value_str, const char* desc, kvar_types type) {
    switch (type) {
    case KVAR_TYPE_INT: {
        // Try to convert to int and set the value.
        i32 value = 0;
        if (!string_to_i32(value_str, &value)) {
            KERROR("Failed to convert argument 1 to i32: '%s'.", value_str);
            return;
        }
        if (!kvar_i32_set(name, desc, value)) {
            KERROR("Failed to set int kvar called '%s'. See logs for details.", name);
            return;
        }
        // Print out the result to the console.
        KINFO("%s = %i", name, value);
    } break;
    case KVAR_TYPE_FLOAT: {
        // Try to convert to float and set the value.
        f32 value = 0;
        if (!string_to_f32(value_str, &value)) {
            KERROR("Failed to convert argument 1 to f32: '%s'.", value_str);
            return;
        }
        if (!kvar_f32_set(name, desc, value)) {
            KERROR("Failed to set float kvar called '%s'. See logs for details.", name);
            return;
        }
        // Print out the result to the console.
        KINFO("%s = %f", name, value);
    } break;
    case KVAR_TYPE_STRING: {
        // Set as string.
        if (!kvar_string_set(name, desc, value_str)) {
            KERROR("Failed to set string kvar called '%s'. See logs for details.", name);
            return;
        }
        // Print out the result to the console.
        KINFO("%s = '%s'", name, value_str);
    } break;
    default:
        KERROR("Unable to set kvar of unknown type: %u", type);
        break;
    }
}

static void kvar_console_command_i32_set(console_command_context context) {
    // NOTE: argument count is verified by the console.
    const char* name = context.arguments[0].value;
    const char* val_str = context.arguments[1].value;
    // TODO: Description support.
    kvar_set_by_str(name, val_str, 0, KVAR_TYPE_INT);
}

static void kvar_console_command_f32_set(console_command_context context) {
    // NOTE: argument count is verified by the console.
    const char* name = context.arguments[0].value;
    const char* val_str = context.arguments[1].value;
    // TODO: Description support.
    kvar_set_by_str(name, val_str, 0, KVAR_TYPE_FLOAT);
}

static void kvar_console_command_string_set(console_command_context context) {
    // NOTE: argument count is verified by the console.
    const char* name = context.arguments[0].value;
    const char* val_str = context.arguments[1].value;
    // TODO: Description support.
    kvar_set_by_str(name, val_str, 0, KVAR_TYPE_STRING);
}

static void kvar_console_command_print_all(console_command_context context) {
    // All kvars
    for (u32 i = 0; i < KVAR_MAX_COUNT; ++i) {
        kvar_entry* entry = &state_ptr->values[i];
        if (entry->name) {
            char val_str[1024] = {0};
            switch (entry->type) {
            case KVAR_TYPE_INT:
                string_format(val_str, "i32 %s = %i, desc='%s'", entry->name, entry->value.i, entry->description ? entry->description : "");
                break;
            case KVAR_TYPE_FLOAT:
                string_format(val_str, "f32 %s = %f, desc='%s'", entry->name, entry->value.f, entry->description ? entry->description : "");
                break;
            case KVAR_TYPE_STRING:
                string_format(val_str, "str %s = '%s', desc='%s'", entry->name, entry->value.s, entry->description ? entry->description : "");
                break;
            default:
                // Unknown type found. Bleat about it, but try printing it out anyway.
                console_write(LOG_LEVEL_WARN, "kvar of unknown type found. Possible corruption?");
                string_format(val_str, "??? %s = ???, desc='%s'", entry->name, entry->description ? entry->description : "");
                break;
            }
            console_write(LOG_LEVEL_INFO, val_str);
        }
    }
}

static void kvar_console_commands_register(void) {
    // Print a var by name.
    console_command_register("kvar_print", 1, kvar_console_command_print);
    // Print all kvars.
    console_command_register("kvar_print_all", 0, kvar_console_command_print_all);

    // Create/set an int-type kvar by name.
    console_command_register("kvar_set_int", 2, kvar_console_command_i32_set);
    console_command_register("kvar_set_i32", 2, kvar_console_command_i32_set); // alias
    // Create/set a float-type kvar by name.
    console_command_register("kvar_set_float", 2, kvar_console_command_f32_set);
    console_command_register("kvar_set_f32", 2, kvar_console_command_f32_set); // alias
    // Create/set a string-type kvar by name.
    console_command_register("kvar_set_string", 2, kvar_console_command_string_set);
}
