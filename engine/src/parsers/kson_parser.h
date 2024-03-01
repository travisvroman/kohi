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

struct kson_object;
typedef struct kson_property {
    kson_property_type type;
    char* name;
    union {
        i64 i;
        f32 f;
        const char* s;
        struct kson_object* o;
        b8 b;
    } value;
} kson_property;

typedef enum kson_object_type {
    KSON_OBJECT_TYPE_OBJECT,
    KSON_OBJECT_TYPE_ARRAY
} kson_object_type;

typedef struct kson_object {
    kson_object_type type;
    // darray
    kson_property* properties;
} kson_object;

typedef struct kson_tree {
    kson_object root;
} kson_tree;

KAPI b8 kson_parser_create(kson_parser* out_parser);
KAPI void kson_parser_destroy(kson_parser* parser);
KAPI b8 kson_parser_tokenize(kson_parser* parser, const char* source);
KAPI b8 kson_parser_parse(kson_parser* parser, kson_tree* out_tree);

KAPI b8 kson_tree_from_string(const char* source, kson_tree* out_tree);

KAPI const char* kson_tree_to_string(kson_tree* tree);

KAPI void kson_tree_cleanup(kson_tree* tree);

#endif
