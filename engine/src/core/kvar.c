#include "kvar.h"

#include "core/kmemory.h"
#include "core/logger.h"
#include "core/kstring.h"
#include "core/event.h"

#include "core/console.h"

typedef struct kvar_int_entry {
    const char* name;
    i32 value;
} kvar_int_entry;

#define KVAR_INT_MAX_COUNT 200

typedef struct kvar_system_state {
    // Integer kvars.
    kvar_int_entry ints[KVAR_INT_MAX_COUNT];
} kvar_system_state;

static kvar_system_state* state_ptr;

static void kvar_console_commands_register(void);

b8 kvar_initialize(u64* memory_requirement, void* memory, void* config) {
    *memory_requirement = sizeof(kvar_system_state);

    if (!memory) {
        return true;
    }

    state_ptr = memory;

    kzero_memory(state_ptr, sizeof(kvar_system_state));

    kvar_console_commands_register();

    return true;
}
void kvar_shutdown(void* state) {
    if (state_ptr) {
        kzero_memory(state_ptr, sizeof(kvar_system_state));
    }
}

b8 kvar_int_create(const char* name, i32 value) {
    if (!state_ptr || !name) {
        return false;
    }

    for (u32 i = 0; i < KVAR_INT_MAX_COUNT; ++i) {
        kvar_int_entry* entry = &state_ptr->ints[i];
        if (entry->name && strings_equali(entry->name, name)) {
            KERROR("A int kvar named '%s' already exists.", name);
            return false;
        }
    }

    for (u32 i = 0; i < KVAR_INT_MAX_COUNT; ++i) {
        kvar_int_entry* entry = &state_ptr->ints[i];
        if (!entry->name) {
            entry->name = string_duplicate(name);
            entry->value = value;
            return true;
        }
    }

    KERROR("kvar_create_int could not find a free slot to store an entry in.");
    return false;
}

b8 kvar_int_get(const char* name, i32* out_value) {
    if (!state_ptr || !name) {
        return false;
    }

    for (u32 i = 0; i < KVAR_INT_MAX_COUNT; ++i) {
        kvar_int_entry* entry = &state_ptr->ints[i];
        if (entry->name && strings_equali(name, entry->name)) {
            *out_value = entry->value;
            return true;
        }
    }

    KERROR("kvar_get_int could not find a kvar named '%s'.", name);
    return false;
}

b8 kvar_int_set(const char* name, i32 value) {
    if (!state_ptr || !name) {
        return false;
    }

    for (u32 i = 0; i < KVAR_INT_MAX_COUNT; ++i) {
        kvar_int_entry* entry = &state_ptr->ints[i];
        if (entry->name && strings_equali(name, entry->name)) {
            entry->value = value;
            // TODO: also pass type?
            event_context context = {0};
            string_ncopy(context.data.c, name, 16);
            event_fire(EVENT_CODE_KVAR_CHANGED, 0, context);
            return true;
        }
    }

    KERROR("kvar_set_int could not find a kvar named '%s'.", name);
    return false;
}

static void kvar_console_command_int_create(console_command_context context) {
    if (context.argument_count != 2) {
        KERROR("kvar_console_command_int_create requires a context arg count of 2.");
        return;
    }

    const char* name = context.arguments[0].value;
    const char* val_str = context.arguments[1].value;
    i32 value = 0;
    if (!string_to_i32(val_str, &value)) {
        KERROR("Failed to convert argument 1 to i32: '%s'.", val_str);
        return;
    }

    if (!kvar_int_create(name, value)) {
        KERROR("Failed to create int kvar.");
    }
}

static void kvar_console_command_int_print(console_command_context context) {
    if (context.argument_count != 1) {
        KERROR("kvar_console_command_int_print requires a context arg count of 1.");
        return;
    }

    const char* name = context.arguments[0].value;
    i32 value = 0;
    if (!kvar_int_get(name, &value)) {
        KERROR("Failed to find kvar called '%s'.", name);
        return;
    }

    char val_str[50] = {0};
    string_format(val_str, "%i", value);

    console_write_line(LOG_LEVEL_INFO, val_str);
}

static void kvar_console_command_int_set(console_command_context context) {
    if (context.argument_count != 2) {
        KERROR("kvar_console_command_int_set requires a context arg count of 2.");
        return;
    }

    const char* name = context.arguments[0].value;
    const char* val_str = context.arguments[1].value;
    i32 value = 0;
    if (!string_to_i32(val_str, &value)) {
        KERROR("Failed to convert argument 1 to i32: '%s'.", val_str);
        return;
    }

    if (!kvar_int_set(name, value)) {
        KERROR("Failed to set int kvar called '%s' because it doesn't exist.", name);
    }

    char out_str[500] = {0};
    string_format(out_str, "%s = %i", name, value);
    console_write_line(LOG_LEVEL_INFO, val_str);
}

static void kvar_console_command_print_all(console_command_context context) {
    // Int kvars
    for (u32 i = 0; i < KVAR_INT_MAX_COUNT; ++i) {
        kvar_int_entry* entry = &state_ptr->ints[i];
        if (entry->name) {
            char val_str[500] = {0};
            string_format(val_str, "%s = %i", entry->name, entry->value);
            console_write_line(LOG_LEVEL_INFO, val_str);
        }
    }

    // TODO: Other variable types.
}

static void kvar_console_commands_register(void) {
    console_command_register("kvar_create_int", 2, kvar_console_command_int_create);
    console_command_register("kvar_print_int", 1, kvar_console_command_int_print);
    console_command_register("kvar_set_int", 2, kvar_console_command_int_set);
    console_command_register("kvar_print_all", 0, kvar_console_command_print_all);
}
