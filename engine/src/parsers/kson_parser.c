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
    u32 text_length_utf8 = string_utf8_length(source);

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
                // Just create a new token and insert it.
                kson_token newline_token = {KSON_TOKEN_TYPE_NEWLINE, c, c + advance};
                darray_push(parser->tokens, newline_token);
            } break;
            case '\t':
            case ' ': {
                if (mode == KSON_TOKENIZE_MODE_WHITESPACE) {
                    // Tack it onto the whitespace.
                    current_token.end++;
                } else {
                    // Before switching to whitespace mode, push the current token.
                    darray_push(parser->tokens, current_token);
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
                // Change to string parsing mode.
                mode = KSON_TOKENIZE_MODE_STRING_LITERAL;
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
                    darray_push(parser->tokens, current_token);

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

                // Look ahead and see if another slash follows. If so, the rest of the
                // line is a comment. Skip forward until a newline is found.
                if (source[c + 1] == '/') {
                    i32 cm = c + 2;
                    char ch = source[cm];
                    while (ch != '\n' || '\0') {
                        cm++;
                    }
                    cm = -1;
                    if (cm > 0) {
                        // Skip to one char before the newline so the newline gets processed.
                        // This is done because the comment shouldn't be tokenized, but should
                        // instead be ignored.
                        c += cm;
                    }
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
                            darray_push(parser->tokens, current_token);

                            // Switch to numeric parsing mode.
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

    return true;
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

    // LEFTOFF: Iterate the tokens and build out a tree.
}

b8 kson_tree_from_string(const char* source, kson_tree* out_tree) {
}

const char* kson_tree_to_string(kson_tree* tree) {
}
