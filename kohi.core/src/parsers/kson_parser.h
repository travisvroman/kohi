#ifndef _KSON_H_
#define _KSON_H_

#include "defines.h"
#include "math/math_types.h"
#include "strings/kname.h"
#include "strings/kstring_id.h"

typedef enum kson_token_type {
    KSON_TOKEN_TYPE_UNKNOWN,
    KSON_TOKEN_TYPE_WHITESPACE,
    KSON_TOKEN_TYPE_COMMENT,
    KSON_TOKEN_TYPE_IDENTIFIER,
    KSON_TOKEN_TYPE_OPERATOR_EQUAL,
    KSON_TOKEN_TYPE_OPERATOR_MINUS,
    KSON_TOKEN_TYPE_OPERATOR_PLUS,
    KSON_TOKEN_TYPE_OPERATOR_SLASH,
    KSON_TOKEN_TYPE_OPERATOR_ASTERISK,
    KSON_TOKEN_TYPE_OPERATOR_DOT,
    KSON_TOKEN_TYPE_STRING_LITERAL,
    KSON_TOKEN_TYPE_NUMERIC_LITERAL,
    KSON_TOKEN_TYPE_BOOLEAN,
    KSON_TOKEN_TYPE_CURLY_BRACE_OPEN,
    KSON_TOKEN_TYPE_CURLY_BRACE_CLOSE,
    KSON_TOKEN_TYPE_BRACKET_OPEN,
    KSON_TOKEN_TYPE_BRACKET_CLOSE,
    KSON_TOKEN_TYPE_NEWLINE,
    KSON_TOKEN_TYPE_EOF
} kson_token_type;

typedef struct kson_token {
    kson_token_type type;
    u32 start;
    u32 end;
#ifdef KOHI_DEBUG
    const char* content;
#endif
} kson_token;

typedef struct kson_parser {
    const char* file_content;
    u32 position;

    // darray
    kson_token* tokens;
} kson_parser;

typedef enum kson_property_type {
    // TODO: Do we want to support undefined/null types. If so, pick one and just use that, no defining both.
    KSON_PROPERTY_TYPE_UNKNOWN,
    KSON_PROPERTY_TYPE_INT,
    KSON_PROPERTY_TYPE_FLOAT,
    KSON_PROPERTY_TYPE_STRING,
    KSON_PROPERTY_TYPE_OBJECT,
    KSON_PROPERTY_TYPE_ARRAY,
    KSON_PROPERTY_TYPE_BOOLEAN,
} kson_property_type;

struct kson_property;

typedef enum kson_object_type {
    KSON_OBJECT_TYPE_OBJECT,
    KSON_OBJECT_TYPE_ARRAY
} kson_object_type;

// An object which can contain properties. Objects
// represent both "object" types as well as "array"
// types. These types are identical with one key
// difference: An object's properties are required to
// be named, whereas array properties are unnamed.
typedef struct kson_object {
    kson_object_type type;
    // darray
    struct kson_property* properties;
} kson_object;

// An alias to represent kson arrays, which are really just
// kson_objects that contain properties without names.
typedef kson_object kson_array;

// Represents a property value for a kson property.
typedef union kson_property_value {
    // Signed 64-bit int value.
    i64 i;
    // 32-bit float value.
    f32 f;
    // String value.
    const char* s;
    // Array or object value.
    kson_object o;
    // Boolean value.
    b8 b;
} kson_property_value;

// Represents a singe property for a kson object or array.
typedef struct kson_property {
    // The type of property.
    kson_property_type type;
    // The name of the property. If this belongs to an array, it should be INVALID_KSTRING_ID.
    kstring_id name;
    // The property value.
    kson_property_value value;
} kson_property;

// Represents a hierarchy of kson objects.
typedef struct kson_tree {
    // The root object, which always must exist.
    kson_object root;
} kson_tree;

/**
 * @brief Gets the given property type as a constant string. NOTE: Caller should *NOT* attempt to free this string.
 *
 * @param type The KSON property type.
 * @returns A constant string representation of the property type. NOTE: Caller should *NOT* attempt to free this string.
 */
KAPI const char* kson_property_type_to_string(kson_property_type type);

/**
 * @brief Creates a kson parser. Note that it is generally recommended to use the
 * kson_tree_from_string() and kson_tree_to_string() functions instead of invoking
 * this manually, as these also handle cleanup of the parser object.
 *
 * @param out_parser A pointer to hold the newly-created parser.
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_parser_create(kson_parser* out_parser);

/**
 * @brief Destroys the provided parser.
 *
 * @param parser A pointer to the parser to be destroyed.
 */
KAPI void kson_parser_destroy(kson_parser* parser);

/**
 * @brief Uses the given parser to tokenize the provided source string.
 * Note that it is recommended to use the kson_tree_from_string() function instead.
 *
 * @param parser A pointer to the parser to use. Required. Must be a valid parser.
 * @param source A constant pointer to the source string to tokenize.
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_parser_tokenize(kson_parser* parser, const char* source);

/**
 * @brief Uses the given parser to build a kson_tree using the tokens previously
 * parsed. This means that kson_parser_tokenize() must have been called and completed
 * successfully for this function to work. It is recommended to use kson_tree_from_string() instead.
 *
 * @param parser A pointer to the parser to use. Required. Must be a valid parser that has already had kson_parser_tokenize() called against it successfully.
 * @param out_tree A pointer to hold the generated kson_tree. Required.
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_parser_parse(kson_parser* parser, kson_tree* out_tree);

/**
 * @brief Takes the provided source and tokenizes, then parses it in order to create a tree of kson_objects.
 *
 * @param source A pointer to the source string to be tokenized and parsed. Required.
 * @param out_tree A pointer to hold the generated kson_tree. Required.
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_tree_from_string(const char* source, kson_tree* out_tree);

/**
 * Takes the provided kson_tree and writes it to a kson-formatted string.
 *
 * @param tree A pointer to the kson_tree to use. Required.
 * @returns A string on success; otherwise false.
 */
KAPI const char* kson_tree_to_string(kson_tree* tree);

/**
 * @brief Cleans up the given kson object and its properties recursively.
 *
 * @param obj A pointer to the object to be cleaned up. Required.
 */
KAPI void kson_object_cleanup(kson_object* obj);

/**
 * @brief Performs cleanup operations on the given tree, freeing memory and resources held by it.
 *
 * @param tree A pointer to the tree to cleanup. Required.
 */
KAPI void kson_tree_cleanup(kson_tree* tree);

/**
 * @brief Adds an unnamed signed 64-bit integer value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_int(kson_array* array, i64 value);

/**
 * @brief Adds an unnamed floating-point value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_float(kson_array* array, f32 value);

/**
 * @brief Adds an unnamed boolean value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_boolean(kson_array* array, b8 value);

/**
 * @brief Adds an unnamed string value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set. Required. Must not be null.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_string(kson_array* array, const char* value);

/**
 * @brief Adds an unnamed mat4 value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_mat4(kson_array* array, mat4 value);

/**
 * @brief Adds an unnamed vec4 value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_vec4(kson_array* array, vec4 value);

/**
 * @brief Adds an unnamed vec3 value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_vec3(kson_array* array, vec3 value);

/**
 * @brief Adds an unnamed vec2 value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_vec2(kson_array* array, vec2 value);

/**
 * @brief Adds an unnamed kname value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_kname(kson_array* array, kname value);

/**
 * @brief Adds an unnamed object value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_object(kson_array* array, kson_object value);

/**
 * @brief Adds an unnamed empty object value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_object_empty(kson_array* array);

/**
 * @brief Adds an unnamed array value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_array(kson_array* array, kson_array value);

/**
 * @brief Adds an unnamed empty array value to the provided array.
 *
 * @param array A pointer to the array to add the property to.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_value_add_array_empty(kson_array* array);

// Object functions.

/**
 * @brief Adds a named signed 64-bit integer value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_int(kson_object* object, const char* name, i64 value);

/**
 * @brief Adds a named floating-point value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_float(kson_object* object, const char* name, f32 value);

/**
 * @brief Adds a named boolean value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_boolean(kson_object* object, const char* name, b8 value);

/**
 * @brief Adds a named string value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set. Required. Must not be null.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_string(kson_object* object, const char* name, const char* value);

/**
 * @brief Adds a named mat4 value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_mat4(kson_object* object, const char* name, mat4 value);

/**
 * @brief Adds a named vec4 value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_vec4(kson_object* object, const char* name, vec4 value);

/**
 * @brief Adds a named vec3 value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_vec3(kson_object* object, const char* name, vec3 value);

/**
 * @brief Adds a named vec2 value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_vec2(kson_object* object, const char* name, vec2 value);

/**
 * @brief Adds a named kname value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_kname(kson_object* object, const char* name, kname value);

/**
 * @brief Adds a named object value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_object(kson_object* object, const char* name, kson_object value);

/**
 * @brief Adds a named empty object value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_object_empty(kson_object* object, const char* name);

/**
 * @brief Adds a named array value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_array(kson_object* object, const char* name, kson_array value);

/**
 * @brief Adds a named empty array value to the provided object.
 *
 * @param object A pointer to the object to add the property to.
 * @param name A constant pointer to the name to be used. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_value_add_array_empty(kson_object* object, const char* name);

/**
 * @brief Obtains the length of the given array.
 *
 * @param array The array to retrieve the length of.
 * @param count A pointer to hold the array element count,
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_array_element_count_get(kson_array* array, u32* out_count);

/**
 * @brief Obtains the element type at the provided index of the given array. Fails if out of range.
 *
 * @param array The array to retrieve the type from.
 * @param index The index into the array to check the type of. Must be in range.
 * @param count A pointer to hold the array element type,
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_array_element_type_at(kson_array* array, u32 index, kson_property_type* out_type);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a signed integer. Fails if out of range.
 * or on type mismatch.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_int(const kson_array* array, u32 index, i64* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a floating-point number. Fails if out of range.
 * or on type mismatch.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_float(const kson_array* array, u32 index, f32* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a boolean. Fails if out of range.
 * or on type mismatch.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_bool(const kson_array* array, u32 index, b8* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a string. Fails if out of range
 * or on type mismatch.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_string(const kson_array* array, u32 index, const char** out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a mat4. Fails if out of range
 * or on type mismatch (these are stored as strings).
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_mat4(const kson_array* array, u32 index, mat4* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a vec4. Fails if out of range
 * or on type mismatch (these are stored as strings).
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_vec4(const kson_array* array, u32 index, vec4* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a vec3. Fails if out of range
 * or on type mismatch (these are stored as strings).
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_vec3(const kson_array* array, u32 index, vec3* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a vec2. Fails if out of range
 * or on type mismatch (these are stored as strings).
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_vec2(const kson_array* array, u32 index, vec2* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as a kname. Fails if out of range
 * or on type mismatch. knames are always stored as strings.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_kname(const kson_array* array, u32 index, kname* out_value);

/**
 * @brief Attempts to retrieve the array element's value at the provided index as an object. Fails if out of range.
 * or on type mismatch.
 *
 * @param array A constant pointer to the array to search. Required.
 * @param index The array index to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_array_element_value_get_object(const kson_array* array, u32 index, kson_object* out_value);

/**
 * Obtains the type of the property with the given name. Fails if the name is not found.
 *
 * @param object The object to retrieve the type from.
 * @param name The name of the property to retrieve.
 * @param out_type A pointer to hold the object property type,
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_object_property_type_get(const kson_object* object, const char* name, kson_property_type* out_type);

/**
 * Obtains the count of properties of the given object.
 *
 * @param object The object to retrieve the property count of.
 * @param out_count A pointer to hold the object property count,
 * @returns True on success; otherwise false.
 */
KAPI b8 kson_object_property_count_get(const kson_object* object, u32* out_count);

/**
 * @brief Attempts to retrieve the given object's property value type by name. Fails if not found.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_type A pointer to hold the object property's type.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_type_get(const kson_object* object, const char* name, kson_property_type* out_type);

/**
 * @brief Attempts to retrieve the given object's property value by name as a signed integer. Fails if not found
 * or on type mismatch.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_int(const kson_object* object, const char* name, i64* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a floating-point number. Fails if not found
 * or on type mismatch.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_float(const kson_object* object, const char* name, f32* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a boolean. Fails if not found
 * or on type mismatch.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_bool(const kson_object* object, const char* name, b8* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a string. Fails if not found
 * or on type mismatch. NOTE: This function always allocates new memory, so the string should be released afterward.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_string(const kson_object* object, const char* name, const char** out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a mat4. Fails if not found
 * or on type mismatch (these are always stored as strings).
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_mat4(const kson_object* object, const char* name, mat4* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a vec4. Fails if not found
 * or on type mismatch (these are always stored as strings).
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_vec4(const kson_object* object, const char* name, vec4* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a vec3. Fails if not found
 * or on type mismatch (these are always stored as strings).
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_vec3(const kson_object* object, const char* name, vec3* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a vec2. Fails if not found
 * or on type mismatch (these are always stored as strings).
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_vec2(const kson_object* object, const char* name, vec2* out_value);

/**
 * @brief Attempts to retrieve the given object's property value by name as a kname. Fails if not found
 * or on type mismatch. knames are always stored as thier original text format.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_kname(const kson_object* object, const char* name, kname* out_value);

/**
 * @brief Attempts to retrieve the a copy given object's property value by name as an object. Fails if not found
 * or on type mismatch.
 *
 * @param object A constant pointer to the object to search. Required.
 * @param name The property name to search for. Required.
 * @param out_value A pointer to hold a copy of the object property's value.
 * @return True on success; otherwise false.
 */
KAPI b8 kson_object_property_value_get_object(const kson_object* object, const char* name, kson_object* out_value);

/**
 * Creates and returns a new property of the object type.
 * @param name The name of the property. Pass 0 if later adding to an array.
 * @returns The newly created object property.
 */
KAPI kson_property kson_object_property_create(const char* name);

/**
 * Creates and returns a new property of the array type.
 * @param name The name of the property. Pass 0 if later adding to an array.
 * @returns The newly created array property.
 */
KAPI kson_property kson_array_property_create(const char* name);

/** @brief Creates and returns a new kson object. */
KAPI kson_object kson_object_create(void);

/** @brief Creates and returns a new kson array. */
KAPI kson_array kson_array_create(void);

#endif
