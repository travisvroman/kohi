/**
 * @file kvar.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A file that contains the KVar system. KVars are global variables
 * that are dynamically created and set/used within the engine and/or
 * application, and are accessible from anywhere.
 * @version 2.0
 * @date 2024-04-2
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 * 
 */
#pragma once

#include "defines.h"

struct kvar_state;

typedef enum kvar_types {
    KVAR_TYPE_INT,
    KVAR_TYPE_FLOAT,
    KVAR_TYPE_STRING
} kvar_types;

typedef union kvar_value {
    i32 i;
    f32 f;
    char* s;
} kvar_value;

typedef struct kvar_change {
    const char* name;
    kvar_types old_type;
    kvar_types new_type;
    kvar_value new_value;
} kvar_change;

/**
 * @brief Initializes the KVar system. KVars are global variables
 * that are dynamically created and set/used within the engine and/or
 * application, and are accessible from anywhere. Like any other system,
 * this should be called twice, once to obtain the memory requirement
 * (where memory = 0), and a second time with an allocated block of
 * memory.
 * 
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param memory An allocated block of memory the size of memory_requirement.
 * @param config A pointer to config, if required.
 * @return b8 True on success; otherwise false.
 */
b8 kvar_system_initialize(u64* memory_requirement, struct kvar_state* memory, void* config);
/**
 * @brief Shuts down the KVar system.
 * 
 * @param state The system state.
 */
void kvar_system_shutdown(struct kvar_state* state);

/**
 * @brief Attempts to obtain a variable value with the given
 * name and return its value as an integer. Also attempts conversion
 * if the variable is a type other than int, but this conversion can fail.
 * 
 * @param name The name of the variable.
 * @param out_value A pointer to hold the variable.
 * @return True if the variable was found and the value was successfully converted/returned; otherwise false.
 */
KAPI b8 kvar_i32_get(const char* name, i32* out_value);

/**
 * @brief Attempts to set the value as an integer of an existing variable with
 * the given name. Creates if the variable does not yet exist.
 * 
 * @param name The name of the variable.
 * @param description Description of the variable. Optional. If updating existing, description will be overwritten unless 0 is passed.
 * @param value The value to be set.
 * @return True if found and set, otherwise false.
 */
KAPI b8 kvar_i32_set(const char* name, const char* desc, i32 value);

/**
 * @brief Attempts to obtain a variable value with the given
 * name and return its value as a float. Also attempts conversion
 * if the variable is a type other than float, but this conversion can fail.
 * 
 * @param name The name of the variable.
 * @param out_value A pointer to hold the variable.
 * @return True if the variable was found and the value was successfully converted/returned; otherwise false.
 */
KAPI b8 kvar_f32_get(const char* name, f32* out_value);

/**
 * @brief Attempts to set the value as a float of an existing variable with
 * the given name. Creates if the variable does not yet exist.
 * 
 * @param name The name of the variable.
 * @param description Description of the variable. Optional. If updating existing, description will be overwritten unless 0 is passed.
 * @param value The value to be set.
 * @return True if found and set, otherwise false.
 */
KAPI b8 kvar_f32_set(const char* name, const char* desc, f32 value);

/**
 * @brief Attempts to obtain a variable value with the given
 * name and return its value as a string. Also attempts conversion
 * if the variable is a type other than string, but this conversion can fail.
 * Return value is dynamically allocated and must be freed by the caller.
 * 
 * @param name The name of the variable.
 * @return A copy of the value as a string, or 0 if the operation fails. Must be freed by the caller.
 */
KAPI const char* kvar_string_get(const char* name);

/**
 * @brief Attempts to set the value as a string of an existing variable with
 * the given name. Creates if the variable does not yet exist.
 * 
 * @param name The name of the variable.
 * @param description Description of the variable. Optional. If updating existing, description will be overwritten unless 0 is passed.
 * @param value The value to be set.
 * @return True if found and set, otherwise false.
 */
KAPI b8 kvar_string_set(const char* name, const char* desc, const char* value);
