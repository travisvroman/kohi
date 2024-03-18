#ifndef _KSON_H_
#define _KSON_H_

#include "defines.h"

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
    // The name of the property. If this belongs to an array, it should be null.
    char* name;
    // The property value.
    kson_property_value value;
} kson_property;

// Represents a hierarchy of kson objects.
typedef struct kson_tree {
    // The root object, which always must exist.
    kson_object root;
} kson_tree;

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

KAPI b8 kson_array_element_count_get(kson_array* array, u32* out_count);
KAPI b8 kson_array_element_type_at(kson_array* array, u32 index, kson_property_type* out_type);

KAPI b8 kson_array_element_value_get_int(const kson_array* array, u32 index, i64* out_value);
KAPI b8 kson_array_element_value_get_float(const kson_array* array, u32 index, f64* out_value);
KAPI b8 kson_array_element_value_get_bool(const kson_array* array, u32 index, b8* out_value);
KAPI b8 kson_array_element_value_get_string(const kson_array* array, u32 index, char** out_value);
KAPI b8 kson_array_element_value_get_object(const kson_array* array, u32 index, kson_object* out_value);

KAPI b8 kson_object_property_type_get(const kson_object* object, const char* name, kson_property_type* out_type);
KAPI b8 kson_object_property_count_get(const kson_object* object, u32* out_count);

KAPI b8 kson_object_property_value_get_int(const kson_object* object, const char* name, i64* out_value);
KAPI b8 kson_object_property_value_get_float(const kson_object* object, const char* name, f64* out_value);
KAPI b8 kson_object_property_value_get_bool(const kson_object* object, const char* name, b8* out_value);
KAPI b8 kson_object_property_value_get_string(const kson_object* object, const char* name, char** out_value);
KAPI b8 kson_object_property_value_get_object(const kson_object* object, const char* name, kson_object* out_value);

#endif
