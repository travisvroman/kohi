#include "kson_parser.h"

#include "containers/darray.h"
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

b8 kson_parser_tokenize(kson_parser* parser, const char* source) {
    if (!parser) {
        KERROR("kson_parser_tokenize requires valid pointer to out_parser, ya dingus.");
        return false;
    }
    if (!source) {
        KERROR("kson_parser_tokenize requires valid pointer to source, ya dingus.");
        return false;
    }

    u32 char_length = string_length(source);
    u32 text_length_utf8 = string_utf8_length(source);

    kson_tokenize_mode mode = KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER;
    kson_token current_token = {0};
    i32 prev_codepoint;

    // Take the length in chars and get the correct codepoint from it.
    i32 codepoint = 0;
    for (u32 c = 0; c < char_length; c++, prev_codepoint = codepoint) {
        codepoint = source[c];

        // Continue to next line for newline.
        if (codepoint == '\n') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                KERROR("Unexpected newline in string at position %u.", c);
                return false;
            }

            // Just create a new token and insert it.
            kson_token newline_token = {0};
            newline_token.type = KSON_TOKEN_TYPE_NEWLINE;
            newline_token.start = c;
            newline_token.end = c;
            darray_push(parser->tokens, newline_token);
            continue;
        }

        if (codepoint == '\t' || codepoint == ' ') {
            if (mode == KSON_TOKENIZE_MODE_WHITESPACE || mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                current_token.end++;
            } else {
                // Before switching to whitespace mode, push the current token.
                darray_push(parser->tokens, current_token);
                mode = KSON_TOKENIZE_MODE_WHITESPACE;
                current_token.type = KSON_TOKEN_TYPE_WHITESPACE;
                current_token.start = c;
                current_token.end = c;
            }
            continue;
        }

        if (codepoint == '{') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                current_token.end++;
            } else {
                darray_push(parser->tokens, current_token);

                // Create and push a new token for this.
                kson_token open_brace_token = {0};
                open_brace_token.type = KSON_TOKEN_TYPE_CURLY_BRACE_OPEN;
                open_brace_token.start = c;
                open_brace_token.end = c;
                darray_push(parser->tokens, open_brace_token);

                mode = KSON_TOKENIZE_MODE_UNKNOWN;
            }
            continue;
        }
        if (codepoint == '}') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                current_token.end++;
            } else {
                darray_push(parser->tokens, current_token);

                // Create and push a new token for this.
                kson_token close_brace_token = {0};
                close_brace_token.type = KSON_TOKEN_TYPE_CURLY_BRACE_CLOSE;
                close_brace_token.start = c;
                close_brace_token.end = c;
                darray_push(parser->tokens, close_brace_token);

                mode = KSON_TOKENIZE_MODE_UNKNOWN;
            }
            continue;
        }

        if (codepoint == '[') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                current_token.end++;
            } else {
                darray_push(parser->tokens, current_token);

                // Create and push a new token for this.
                kson_token open_bracket_token = {0};
                open_bracket_token.type = KSON_TOKEN_TYPE_BRACKET_OPEN;
                open_bracket_token.start = c;
                open_bracket_token.end = c;
                darray_push(parser->tokens, open_bracket_token);

                mode = KSON_TOKENIZE_MODE_UNKNOWN;
            }
            continue;
        }
        if (codepoint == ']') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                current_token.end++;
            } else {
                darray_push(parser->tokens, current_token);

                // Create and push a new token for this.
                kson_token close_bracket_token = {0};
                close_bracket_token.type = KSON_TOKEN_TYPE_BRACKET_CLOSE;
                close_bracket_token.start = c;
                close_bracket_token.end = c;
                darray_push(parser->tokens, close_bracket_token);

                mode = KSON_TOKENIZE_MODE_UNKNOWN;
            }
            continue;
        }
        if (codepoint == '"') {
            if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL) {
                if (c > 0 && prev_codepoint != '\\') {
                    // Terminate the string, push the token onto the array, and revert modes.
                    darray_push(parser->tokens, current_token);

                    mode = KSON_TOKENIZE_MODE_UNKNOWN;
                } else {
                    // If prev char was a '\', then it's an escape sequence. Continue the string.
                    current_token.end++;
                }
            } else {
                // Change to string parsing mode.
                mode = KSON_TOKENIZE_MODE_STRING_LITERAL;
                continue;
            }
        }
        // NOTE: UTF-8 codepoint handling.
        u8 advance = 0;
        if (!bytes_to_codepoint(source, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        // Append to string or identifier if parsing that token type.
        if (mode == KSON_TOKENIZE_MODE_STRING_LITERAL || mode == KSON_TOKENIZE_MODE_DEFINING_IDENTIFIER) {
            current_token.end += advance;
        }

        // LEFTOFF:
        // Handle moving into/out of defining identifier.
        // Handle '=' operator
        // handle comments
        // Decide on how xforms should be parsed (strings?)

        // Now advance c
        c += advance - 1;  // Subtracting 1 because the loop always increments once for single-byte anyway.
    }

    return true;
}
b8 kson_parser_parse(kson_parser* parser, kson_tree* out_tree) {
}

b8 kson_tree_from_string(const char* source, kson_tree* out_tree) {
}

const char* kson_tree_to_string(kson_tree* tree) {
}
