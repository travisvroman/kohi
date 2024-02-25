#include "kson_parser.h"

#include "containers/darray.h"
#include "containers/stack.h"
#include "core/asserts.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"

b8 kson_parser_create(kson_parser* out_parser) {
    if (!out_parser) {
        KERROR("kson_parser_create requires valid pointer to out_parser, ya dingus.");
        return false;
    }

    out_parser->position = 0;
    out_parser->tokens = darray_create(kson_token);
    out_parser->file_content = 0;

    return true;
}
void kson_parser_destroy(kson_parser* parser) {
    if (parser) {
        if (parser->file_content) {
            string_free((char*)parser->file_content);
            parser->file_content = 0;
        }
        if (parser->tokens) {
            darray_destroy(parser->tokens);
            parser->tokens = 0;
        }
        parser->position = 0;
    }
}

typedef enum kson_tokenize_mode {
    KSON_TOKENIZE_MODE_UNKNOWN,
    KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER,
    KSON_TOKENIZE_MODE_WHITESPACE,
    KSON_TOKENIZE_MODE_STRING_LITERAL,
    KSON_TOKENIZE_MODE_NUMERIC_LITERAL,
    KSON_TOKENIZE_MODE_BOOLEAN,
    KSON_TOKENIZE_MODE_OPERATOR,
} kson_tokenize_mode;

// Resets both the current token type and the tokenize mode to unknown.
#define RESET_CURRENT_TOKEN_AND_MODE()                \
    {                                                 \
        current_token.type = KSON_TOKEN_TYPE_UNKNOWN; \
        current_token.start = 0;                      \
        current_token.end = 0;                        \
                                                      \
        mode = KSON_TOKENIZE_MODE_UNKNOWN;            \
    }

// Pushes the current token, if not of unknown type.
#define PUSH_CURRENT_TOKEN()                                 \
    {                                                        \
        if (current_token.type != KSON_TOKEN_TYPE_UNKNOWN) { \
            darray_push(parser->tokens, current_token);      \
        }                                                    \
    }

b8 kson_parser_tokenize(kson_parser* parser, const char* source) {
    if (!parser) {
        KERROR("kson_parser_tokenize requires valid pointer to out_parser, ya dingus.");
        return false;
    }
    if (!source) {
        KERROR("kson_parser_tokenize requires valid pointer to source, ya dingus.");
        return false;
    }

    // Ensure the parser's tokens array is empty.
    darray_clear(parser->tokens);

    u32 char_length = string_length(source);
    /* u32 text_length_utf8 = string_utf8_length(source); */

    kson_tokenize_mode mode = KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER;
    kson_token current_token = {0};
    // The previous codepoint.
    i32 prev_codepoint = 0;
    // The codepoint from 2 iterations ago.
    i32 prev_codepoint2 = 0;

    b8 eof_reached = false;

    // Take the length in chars and get the correct codepoint from it.
    i32 codepoint = 0;
    for (u32 c = 0; c < char_length; prev_codepoint2 = prev_codepoint, prev_codepoint = codepoint) {
        if (eof_reached) {
            break;
        }

        codepoint = source[c];
        // How many bytes to advance.
        u8 advance = 1;
        // NOTE: UTF-8 codepoint handling.
        if (!bytes_to_codepoint(source, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
            // Handle string literal parsing.
            // End the string if only if the previous codepoint was not a backslash OR the codepoint
            // previous codepoint was a backslash AND the one before that was also a backslash. I.e.
            // it needs to be confirmed that the backslash is not already escaped and that the quote is
            // also not escaped.
            if (codepoint == '"' && (prev_codepoint != '\\' || prev_codepoint2 == '\\')) {
                // Terminate the string, push the token onto the array, and revert modes.
                PUSH_CURRENT_TOKEN();
                RESET_CURRENT_TOKEN_AND_MODE();
            } else {
                // Handle other characters as part of the string.
                current_token.end += advance;
            }
            // TODO: May need to handle other escape sequences read in here, like \t, \n, etc.

            // At this point, this codepoint has been handles so continue early.
            c += advance;
            continue;
        }

        // Not part of a string, identifier, numeric, etc., so try to figure out what to do next.
        switch (codepoint) {
            case '\n': {
                PUSH_CURRENT_TOKEN();

                // Just create a new token and insert it.
                kson_token newline_token = {KSON_TOKEN_TYPE_NEWLINE, c, c + advance};
                darray_push(parser->tokens, newline_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '\t':
            case '\r':
            case ' ': {
                if (mode == KSON_TOKENIZE_MODE_WHITESPACE) {
                    // Tack it onto the whitespace.
                    current_token.end++;
                } else {
                    // Before switching to whitespace mode, push the current token.
                    PUSH_CURRENT_TOKEN();
                    mode = KSON_TOKENIZE_MODE_WHITESPACE;
                    current_token.type = KSON_TOKEN_TYPE_WHITESPACE;
                    current_token.start = c;
                    current_token.end = c + advance;
                }
            } break;
            case '{': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token open_brace_token = {KSON_TOKEN_TYPE_CURLY_BRACE_OPEN, c, c + advance};
                darray_push(parser->tokens, open_brace_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '}': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token close_brace_token = {KSON_TOKEN_TYPE_CURLY_BRACE_CLOSE, c, c + advance};
                darray_push(parser->tokens, close_brace_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '[': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token open_bracket_token = {KSON_TOKEN_TYPE_BRACKET_OPEN, c, c + advance};
                darray_push(parser->tokens, open_bracket_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case ']': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token close_bracket_token = {KSON_TOKEN_TYPE_BRACKET_CLOSE, c, c + advance};
                darray_push(parser->tokens, close_bracket_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '"': {
                PUSH_CURRENT_TOKEN();

                RESET_CURRENT_TOKEN_AND_MODE();

                // Change to string parsing mode.
                mode = KSON_TOKENIZE_MODE_STRING_LITERAL;
                current_token.type = KSON_TOKEN_TYPE_STRING_LITERAL;
                current_token.start = c + advance;
                current_token.end = c + advance;
            } break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': {
                if (mode == KSON_TOKENIZE_MODE_NUMERIC_LITERAL) {
                    current_token.end++;
                } else {
                    // Push the existing token.
                    PUSH_CURRENT_TOKEN();

                    // Switch to numeric parsing mode.
                    mode = KSON_TOKENIZE_MODE_NUMERIC_LITERAL;
                    current_token.type = KSON_TOKEN_TYPE_NUMERIC_LITERAL;
                    current_token.start = c;
                    current_token.end = c + advance;
                }
            } break;
            case '-': {
                // NOTE: Always treat the minus as a minus operator regardless of how it is used (except in
                // the string case above, which is already covered). It's then up to the grammar rules as to
                // whether this then gets used to negate a numeric literal or if it is used for subtraction, etc.

                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token minus_token = {KSON_TOKEN_TYPE_OPERATOR_MINUS, c, c + advance};
                darray_push(parser->tokens, minus_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '+': {
                // NOTE: Always treat the plus as a plus operator regardless of how it is used (except in
                // the string case above, which is already covered). It's then up to the grammar rules as to
                // whether this then gets used to ensure positivity of a numeric literal or if it is used for addition, etc.

                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token plus_token = {KSON_TOKEN_TYPE_OPERATOR_PLUS, c, c + advance};
                darray_push(parser->tokens, plus_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '/': {
                PUSH_CURRENT_TOKEN();
                RESET_CURRENT_TOKEN_AND_MODE();

                // Look ahead and see if another slash follows. If so, the rest of the
                // line is a comment. Skip forward until a newline is found.
                if (source[c + 1] == '/') {
                    i32 cm = c + 2;
                    char ch = source[cm];
                    while (ch != '\n' && ch != '\0') {
                        cm++;
                        ch = source[cm];
                    }
                    if (cm > 0) {
                        // Skip to one char before the newline so the newline gets processed.
                        // This is done because the comment shouldn't be tokenized, but should
                        // instead be ignored.
                        c = cm;
                    }
                    continue;
                } else {
                    // Otherwise it should be treated as a slash operator.
                    // Create and push a new token for this.
                    kson_token slash_token = {KSON_TOKEN_TYPE_OPERATOR_SLASH, c, c + advance};
                    darray_push(parser->tokens, slash_token);
                }

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '*': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token asterisk_token = {KSON_TOKEN_TYPE_OPERATOR_ASTERISK, c, c + advance};
                darray_push(parser->tokens, asterisk_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '=': {
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token equal_token = {KSON_TOKEN_TYPE_OPERATOR_EQUAL, c, c + advance};
                darray_push(parser->tokens, equal_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '.': {
                // NOTE: Always treat this as a dot token, regardless of use. It's up to the grammar
                // rules in the parser as to whether or not it's to be used as part of a numeric literal
                // or something else.

                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token dot_token = {KSON_TOKEN_TYPE_OPERATOR_DOT, c, c + advance};
                darray_push(parser->tokens, dot_token);

                RESET_CURRENT_TOKEN_AND_MODE();
            } break;
            case '\0': {
                // Reached the end of the file.
                PUSH_CURRENT_TOKEN();

                // Create and push a new token for this.
                kson_token eof_token = {KSON_TOKEN_TYPE_EOF, c, c + advance};
                darray_push(parser->tokens, eof_token);

                RESET_CURRENT_TOKEN_AND_MODE();

                eof_reached = true;
            } break;

            default: {
                // Identifiers may be made up of upper/lowercase a-z, underscores and numbers (although
                // a number cannot be the first character of an identifier). Note that the number cases
                // are handled above as numeric literals, and can/will be combined into identifiers
                // if there are identifiers without whitespace next to numerics.
                if ((codepoint >= 'A' && codepoint <= 'z') || codepoint == '_') {
                    if (mode == KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER) {
                        // Tack onto the existing identifier.
                        current_token.end += advance;
                    } else {
                        // Check first to see if it's possibly a boolean definition.
                        const char* str = source + c;
                        u8 bool_advance = 0;
                        if (strings_nequali(str, "true", 4)) {
                            bool_advance = 4;
                        } else if (strings_nequali(str, "false", 5)) {
                            bool_advance = 5;
                        }

                        if (bool_advance) {
                            PUSH_CURRENT_TOKEN();

                            // Create and push boolean token.
                            kson_token bool_token = {KSON_TOKEN_TYPE_BOOLEAN, c, c + bool_advance};
                            darray_push(parser->tokens, bool_token);

                            RESET_CURRENT_TOKEN_AND_MODE();

                            // Move forward by the size of the token.
                            advance = bool_advance;
                        } else {
                            // Treat as the start of an identifier definition.
                            // Push the existing token.
                            PUSH_CURRENT_TOKEN();

                            // Switch to identifier parsing mode.
                            mode = KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER;
                            current_token.type = KSON_TOKEN_TYPE_IDENTIFIER;
                            current_token.start = c;
                            current_token.end = c + advance;
                        }
                    }
                } else {
                    // If any other character is come across here that isn't part of a string, it's unknown
                    // what should happen here. So, throw an error regarding this and boot if this is the
                    // case.
                    KERROR("Unexpected character '%c' at position %u. Tokenization failed.", c + advance);
                    // Clear the tokens array, as there is nothing that can be done with them in this case.
                    darray_clear(parser->tokens);
                    return false;
                }

            } break;
        }

        // Now advance c
        c += advance;
    }
    PUSH_CURRENT_TOKEN();
    // Create and push a new token for this.
    kson_token eof_token = {KSON_TOKEN_TYPE_EOF, char_length, char_length + 1};
    darray_push(parser->tokens, eof_token);

    return true;
}

#define NEXT_TOKEN()                            \
    {                                           \
        index++;                                \
        current_token = &parser->tokens[index]; \
    }

#define ENSURE_IDENTIFIER(token_string)                                                                          \
    {                                                                                                            \
        if (expect_identifier) {                                                                                 \
            KERROR("Expected identifier, instead found '%s'. Position: %u", token_string, current_token->start); \
            return false;                                                                                        \
        }                                                                                                        \
    }

static kson_token* get_last_non_whitespace_token(kson_parser* parser, u32 current_index) {
    kson_token* t = &parser->tokens[current_index];
    while (current_index > 0 && t && t->type == KSON_TOKEN_TYPE_WHITESPACE) {
        current_index--;
        t = &parser->tokens[current_index];
    }

    // This means that the last token available in the file is whitespace.
    // Impossible to return non-whitespace token.
    if (t->type == KSON_TOKEN_TYPE_WHITESPACE) {
        return 0;
    }

    return t;
}

#define NUMERIC_LITERAL_STR_MAX_LENGTH 25

static char* string_from_kson_token(const char* file_content, const kson_token* token) {
    u32 length = token->end - token->start;
    char* mid = kallocate(sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    string_mid(mid, file_content, token->start, length);
    mid[length] = 0;

    return mid;
}

b8 kson_parser_parse(kson_parser* parser, kson_tree* out_tree) {
    if (!parser) {
        KERROR("kson_parser_parse requires a valid pointer to a parser.");
        return false;
    }
    if (!out_tree) {
        KERROR("kson_parser_parse requires a valid pointer to a tree.");
        return false;
    }

    if (!parser->tokens) {
        KERROR("Cannot parse an empty set of tokens, ya dingus!");
        return false;
    }

    kson_token* current_token = 0;

    stack scope;
    stack_create(&scope, sizeof(kson_object*));

    // The first thing expected is an identifier.
    b8 expect_identifier = true;
    b8 expect_value = false;
    b8 expect_operator = false;
    b8 expect_numeric = false;

    char numeric_literal_str[NUMERIC_LITERAL_STR_MAX_LENGTH] = {0};
    u32 numeric_literal_str_pos = 0;
    i32 numeric_decimal_pos = -1;

    u32 index = 0;
    current_token = &parser->tokens[index];

    // Setup the tree.
    out_tree->roots = darray_create(kson_object);

    // TODO: do we need a base object?
    kson_object base_object = {0};
    base_object.type = KSON_OBJECT_TYPE_OBJECT;
    base_object.properties = darray_create(kson_property);
    darray_push(out_tree->roots, base_object);

    // Set it as the current object.
    kson_object* current_object = &out_tree->roots[0];
    kson_property* current_property = 0;

    while (current_token && current_token->type != KSON_TOKEN_TYPE_EOF) {
        switch (current_token->type) {
            case KSON_TOKEN_TYPE_CURLY_BRACE_OPEN: {
                ENSURE_IDENTIFIER("{")
                // starting a block.
                kson_object obj = {0};
                obj.type = KSON_OBJECT_TYPE_OBJECT;
                obj.properties = darray_create(kson_property);
                // Push to current property
                darray_push(current_property->value.o, obj);
                // Set the object as current and push to the stack.
                u32 prop_count = darray_length(current_property->value.o);
                current_object = &current_property->value.o[prop_count - 1];

                // Add the object to the stack.
                stack_push(&scope, current_object);
            } break;
            case KSON_TOKEN_TYPE_CURLY_BRACE_CLOSE: {
                ENSURE_IDENTIFIER("}")
                // Ending a block.
                if (!stack_pop(&scope, &current_object)) {
                    KERROR("Failed to pop from scope stack.");
                    return false;
                }
            } break;
            case KSON_TOKEN_TYPE_BRACKET_OPEN: {
                ENSURE_IDENTIFIER("[")
                // starting an array.
                kson_object obj = {0};
                obj.type = KSON_OBJECT_TYPE_ARRAY;
                obj.properties = darray_create(kson_property);

                // Push to current property
                darray_push(current_property->value.o, obj);
                // Set the object as current and push to the stack.
                u32 prop_count = darray_length(current_property->value.o);
                current_object = &current_property->value.o[prop_count - 1];

                // Add the object to the stack.
                stack_push(&scope, current_object);
            } break;
            case KSON_TOKEN_TYPE_BRACKET_CLOSE: {
                ENSURE_IDENTIFIER("]")
                // Ending an array.
                if (!stack_pop(&scope, &current_object)) {
                    KERROR("Failed to pop from scope stack.");
                    return false;
                }
            } break;
            case KSON_TOKEN_TYPE_IDENTIFIER:
                char buf[512] = {0};
                string_mid(buf, parser->file_content, current_token->start, current_token->end - current_token->start);
                if (!expect_identifier) {
                    KERROR("Unexpected identifier '%s' at position %u.", buf, current_token->start);
                    return false;
                }
                // Start a new property.
                kson_property prop = {0};
                prop.type = KSON_PROPERTY_TYPE_UNKNOWN;
                prop.name = string_duplicate(buf);

                // Push the new property and set the current property to it.
                darray_push(current_object->properties, prop);
                u32 prop_count = darray_length(current_object->properties);
                current_property = &current_object->properties[prop_count - 1];

                // No longer expecting an identifier
                expect_identifier = false;
                expect_operator = true;
                break;
            case KSON_TOKEN_TYPE_WHITESPACE:
            case KSON_TOKEN_TYPE_COMMENT:
                NEXT_TOKEN();
                continue;
            case KSON_TOKEN_TYPE_UNKNOWN:
            default:
                KERROR("Unexpected and unknown token found. Parse failed.");
                return false;
            case KSON_TOKEN_TYPE_OPERATOR_EQUAL: {
                ENSURE_IDENTIFIER("=")
                // Previous token must be an identifier.
                kson_token* t = get_last_non_whitespace_token(parser, index);
                if (!t) {
                    KERROR("Unexpected token before assignment operator. Position: %u", current_token->start);
                    return false;
                } else if (t->type != KSON_TOKEN_TYPE_IDENTIFIER) {
                    KERROR("Expected identifier before assignment operator. Position: %u", current_token->start);
                    return false;
                }

                expect_operator = false;

                // The next non-whitespace token should be a value of some kind.
                expect_value = true;
            } break;
            case KSON_TOKEN_TYPE_OPERATOR_MINUS: {
                if (expect_numeric) {
                    KERROR("Already parsing a numeric, negatives are invalid within a numeric. Position: %u", current_token->start);
                    return false;
                }

                // If the next token is a numeric literal, process this as a numeric.
                // Note that a negative is only valid for the first character of a numeric literal.
                if (parser->tokens[index + 1].type == KSON_TOKEN_TYPE_NUMERIC_LITERAL ||
                    (parser->tokens[index + 1].type == KSON_TOKEN_TYPE_OPERATOR_DOT && parser->tokens[index + 2].type == KSON_TOKEN_TYPE_NUMERIC_LITERAL)) {
                    // Start of a numeric process.
                    expect_numeric = true;
                    current_property->type = KSON_PROPERTY_TYPE_NUMBER;
                    kzero_memory(numeric_literal_str, sizeof(char) * NUMERIC_LITERAL_STR_MAX_LENGTH);

                    numeric_literal_str[0] = '-';
                    numeric_literal_str_pos++;
                } else {
                    // TODO: This should be treated as a subtraction operator. Ensure previous token
                    // is valid, etc.
                    KERROR("subtraction is not supported at this time.");
                    return false;
                }
            } break;
            case KSON_TOKEN_TYPE_OPERATOR_PLUS:
                KERROR("Addition is not supported at this time.");
                return false;
                break;
            case KSON_TOKEN_TYPE_OPERATOR_DOT:
                // This could be the first in a string of tokens of a numeric literal.
                if (!expect_numeric) {
                    // Check the next token to see if it is a numeric. It must be in order for this to be part of it.
                    // Whitespace in between is not supported.
                    if (parser->tokens[index + 1].type == KSON_TOKEN_TYPE_NUMERIC_LITERAL) {
                        // Start a numeric literal.
                        numeric_literal_str[0] = '.';
                        expect_numeric = true;
                        current_property->type = KSON_PROPERTY_TYPE_NUMBER;
                        kzero_memory(numeric_literal_str, sizeof(char) * NUMERIC_LITERAL_STR_MAX_LENGTH);
                        numeric_decimal_pos = 0;
                        numeric_literal_str_pos++;
                    } else {
                        // TODO: Support named object properties such as "sponza.name".
                        KERROR("Dot property operator not supported. Position: %u", current_token->start);
                        return false;
                    }
                } else {
                    // Just verify that a decimal doesn't already exist.
                    if (numeric_decimal_pos != -1) {
                        KERROR("Cannot include more than once decimal in a numeric literal. First occurrance: %i, Position: %u", numeric_decimal_pos, current_token->start);
                        return false;
                    }

                    // Append it to the string.
                    numeric_literal_str[numeric_literal_str_pos] = '.';
                    numeric_decimal_pos = numeric_literal_str_pos;
                    numeric_literal_str_pos++;
                }
                break;
            case KSON_TOKEN_TYPE_OPERATOR_ASTERISK:
            case KSON_TOKEN_TYPE_OPERATOR_SLASH:
                KERROR("Unexpected token at position %u. Parse failed.", current_token->start);
                return false;
            case KSON_TOKEN_TYPE_NUMERIC_LITERAL: {
                if (!expect_numeric) {
                    expect_numeric = true;
                    current_property->type = KSON_PROPERTY_TYPE_NUMBER;
                    kzero_memory(numeric_literal_str, sizeof(char) * NUMERIC_LITERAL_STR_MAX_LENGTH);
                }
                u32 length = current_token->end - current_token->start;
                string_ncopy(numeric_literal_str + numeric_literal_str_pos, parser->file_content + current_token->start, length);
                numeric_literal_str_pos += length;
            } break;
            case KSON_TOKEN_TYPE_STRING_LITERAL:
                if (!expect_value) {
                    KERROR("Unexpected string token at position: %u", current_token->start);
                    return false;
                }

                current_property->value.s = string_from_kson_token(parser->file_content, current_token);

                expect_value = false;
                break;
            case KSON_TOKEN_TYPE_BOOLEAN: {
                if (!expect_value) {
                    KERROR("Unexpected boolean token at position: %u", current_token->start);
                    return false;
                }

                char* token_string = string_from_kson_token(parser->file_content, current_token);
                b8 result = string_to_bool(token_string, &current_property->value.b);
                if (!result) {
                    KERROR("Failed to parse boolean from token. Position: %u", current_token->start);
                }

                string_free(token_string);
            } break;
            case KSON_TOKEN_TYPE_NEWLINE:
                if (expect_numeric) {
                    // Terminate the numeric and set the current property's value to it.
                    // Determine whether it is a float or a int.
                    if (string_index_of(numeric_literal_str, '.')) {
                        if (!string_to_f32(numeric_literal_str, &current_property->value.f)) {
                            KERROR("Failed to parse string to float: '%s', Position: %u", numeric_literal_str, current_token->start);
                            return false;
                        }
                    } else {
                        if (!string_to_i64(numeric_literal_str, &current_property->value.i)) {
                            KERROR("Failed to parse string to signed int: '%s', Position: %u", numeric_literal_str, current_token->start);
                            return false;
                        }
                    }

                    expect_numeric = false;

                    // Current value is set, so now expect another identifier or array element.
                }

                // Don't expect a value after a newline.
                // TODO: What about arrays of values?
                expect_value = false;
                break;
            case KSON_TOKEN_TYPE_EOF: {
                b8 valid = true;
                // Verify that we are not in the middle of assignment.
                if (expect_value || expect_operator || expect_numeric) {
                    valid = false;
                }
                // Verify that the current depth is now 1 (to account for the base object).
                if (scope.element_count > 1) {
                    valid = false;
                }

                if (!valid) {
                    KERROR("Unexpected end of file at position: %u", current_token->start);
                    return false;
                }

            } break;
        }
    }

    return true;
}

b8 kson_tree_from_string(const char* source, kson_tree* out_tree) {
    // TODO: combined Tokenization and parsing into a single call here.
    KASSERT_MSG(false, "Not implemented");
    return false;
}

const char* kson_tree_to_string(kson_tree* tree) {
    // TODO: write a string based on the provided tree.
    KASSERT_MSG(false, "Not implemented");
    return false;
}
