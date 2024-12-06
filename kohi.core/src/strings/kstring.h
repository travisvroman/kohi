/**
 * @file kstring.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains a basic C string handling library.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"
#include "math/math_types.h"

/**
 * @brief Gets the number of bytes of the given string, minus the null terminator.
 *
 * NOTE: For strings without a null terminator, use string_nlength instead.
 *
 * @param str The string whose length to obtain.
 * @returns The length of the string.
 */
KAPI u64 string_length(const char* str);

/**
 * @brief Gets the length of a string in UTF-8 (potentially multibyte) characters, minus the null terminator.
 *
 * NOTE: For strings without a null terminator, use string_utf8_nlength instead.
 *
 * @param str The string to examine.
 * @return The UTF-8 length of the string.
 */
KAPI u32 string_utf8_length(const char* str);

/**
 * @brief Gets the number of bytes of the given string, minus the null terminator, but at most max_len.
 * This function only ever looks at the bytes pointed to in str up until, but never beyond, max_len - 1.
 *
 * @param str The string whose length to obtain.
 * @param max_len The maximum number of bytes to examine in the string.
 * @returns The length of the string, at most max_len.
 */
KAPI u64 string_nlength(const char* str, u32 max_len);

/**
 * @brief Gets the number of characters (multibyte = 1 character) of a string in UTF-8 (potentially multibyte) characters, minus the null terminator, but at most max_len.
 * This function only ever looks at the characters pointed to in str up until, but never beyond, max_len - 1.
 *
 * @param str The string to examine.
 * @param max_len The maximum number of characters to examine in the string.
 * @return The number of multibyte characters in the string, at most max_len.
 */
KAPI u32 string_utf8_nlength(const char* str, u32 max_len);

/**
 * @brief Obtains bytes needed from the byte array to form a UTF-8 codepoint,
 * also providing how many bytes the current character is.
 *
 * @param bytes The byte array to choose from.
 * @param offset The offset in bytes to start from.
 * @param out_codepoint A pointer to hold the UTF-8 codepoint.
 * @param out_advance A pointer to hold the advance, or how many bytes the codepoint takes.
 * @return True on success; otherwise false for invalid/unsupported UTF-8.
 */
KAPI b8 bytes_to_codepoint(const char* bytes, u32 offset, i32* out_codepoint, u8* out_advance);

/**
 * @brief Indicates if the provided character is considered whitespace.
 *
 * @param c The character to examine.
 * @return True if whitespace; otherwise false.
 */
KAPI b8 char_is_whitespace(char c);

/**
 * @brief Indicates if the provided codepoint is considered whitespace.
 *
 * @param codepoint The codepoint to examine.
 * @return True if whitespace; otherwise false.
 */
KAPI b8 codepoint_is_whitespace(i32 codepoint);

/**
 * @brief Duplicates the provided string. Note that this allocates new memory,
 * which should be freed by the caller.
 * @param str The string to be duplicated.
 * @returns A pointer to a newly-created character array (string).
 */
KAPI char* string_duplicate(const char* str);

/**
 * @brief Frees the memory of the given string.
 *
 * @param str The string to be freed.
 */
KAPI void string_free(const char* str);

/**
 * @brief Case-sensitive string comparison.
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @returns True if the same, otherwise false.
 */
KAPI b8 strings_equal(const char* str0, const char* str1);

/**
 * @brief Case-insensitive string comparison.
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @returns True if the same, otherwise false.
 */
KAPI b8 strings_equali(const char* str0, const char* str1);

/**
 * @brief Case-sensitive string comparison, where comparison stops at max_len.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param max_len The maximum number of bytes to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequal(const char* str0, const char* str1, u32 max_len);

/**
 * @brief Case-insensitive string comparison, where comparison stops at max_len.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param max_len The maximum number of bytes to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequali(const char* str0, const char* str1, u32 max_len);

/**
 * @brief Performs string formatting against the given format string and parameters.
 * NOTE: that this performs a dynamic allocation and should be freed by the caller.
 *
 * @param format The format string to use for the operation
 * @param ... The format arguments.
 * @returns The newly-formatted string (dynamically allocated).
 */
KAPI char* string_format(const char* format, ...);

/**
 * @brief Performs variadic string formatting against the given format string and va_list.
 * NOTE: that this performs a dynamic allocation and should be freed by the caller.
 *
 * @param format The string to be formatted.
 * @param va_list The variadic argument list.
 * @returns The newly-formatted string (dynamically allocated).
 */
KAPI char* string_format_v(const char* format, void* va_list);

/**
 * @brief Performs string formatting to dest given format string and parameters.
 * @deprecated
 * @note This version of the function is unsafe. Use string_format() instead.
 * @param dest The destination for the formatted string.
 * @param format The format string to use for the operation
 * @param ... The format arguments.
 * @returns The length of the newly-formatted string.
 */
KDEPRECATED("This version of string format is legacy, and unsafe. Use string_format() instead.")
KAPI i32 string_format_unsafe(char* dest, const char* format, ...);

/**
 * @brief Performs variadic string formatting to dest given format string and va_list.
 * @deprecated
 * @note This version of the function is unsafe. Use string_format() instead.
 * @param dest The destination for the formatted string.
 * @param format The string to be formatted.
 * @param va_list The variadic argument list.
 * @returns The size of the data written.
 */
KDEPRECATED("This version of string format variadic is legacy, and unsafe. Use string_format_v() instead.")
KAPI i32 string_format_v_unsafe(char* dest, const char* format, void* va_list);

/**
 * @brief Empties the provided string by setting the first character to 0.
 *
 * @param str The string to be emptied.
 * @return A pointer to str.
 */
KAPI char* string_empty(char* str);

/**
 * @brief Copies the string in source to dest. Does not perform any allocations.
 * @param dest The destination string.
 * @param source The source string.
 * @returns A pointer to the destination string.
 */
KAPI char* string_copy(char* dest, const char* source);

/**
 * @brief Copies the bytes in the source buffer into the dest buffer up to the given length. Does not perform any allocations.
 * Any remaining length after a 0 terminator will be zero-padded unless max_len is U32_MAX.
 *
 * @param dest A pointer to the destination buffer. Must be at least max_len large.
 * @param source A constant pointer to the source buffer.
 * @param length The maximum number of bytes to be copied.
 * @returns A pointer to the destination string.
 */
KAPI char* string_ncopy(char* dest, const char* source, u32 max_len);

/**
 * @brief Performs an in-place trim of the provided string.
 * This removes all whitespace from both ends of the string.
 *
 * Done by placing zeroes in the string at relevant points.
 * @param str The string to be trimmed.
 * @returns A pointer to the trimmed string.
 */
KAPI char* string_trim(char* str);

/**
 * @brief Gets a substring of the source string between start and length or to the end of the string.
 * If length is negative, goes to the end of the string.
 *
 * Done by placing zeroes in the string at relevant points.
 * @param str The string to be trimmed.
 */
KAPI void string_mid(char* dest, const char* source, i32 start, i32 length);

/**
 * @brief Returns the index of the first occurance of c in str; otherwise -1.
 *
 * @param str The string to be scanned.
 * @param c The character to search for.
 * @return The index of the first occurance of c; otherwise -1 if not found.
 */
KAPI i32 string_index_of(const char* str, char c);

/**
 * @brief Returns the index of the last occurance of c in str; otherwise -1.
 *
 * @param str The string to be scanned.
 * @param c The character to search for.
 * @return The index of the last occurance of c; otherwise -1 if not found.
 */
KAPI i32 string_last_index_of(const char* str, char c);

/**
 * @brief Returns the index of the first occurance of str_1 in str_0; otherwise -1.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return The index of the first occurance of str_1; otherwise -1 if not found.
 */
KAPI i32 string_index_of_str(const char* str_0, const char* str_1);

/**
 * @brief Indicates if str_0 starts with str_1. Case-sensitive.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return True if str_0 starts with str_1; otherwise false.
 */
KAPI b8 string_starts_with(const char* str_0, const char* str_1);

/**
 * @brief Indicates if str_0 starts with str_1. Case-insensitive.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return True if str_0 starts with str_1; otherwise false.
 */
KAPI b8 string_starts_withi(const char* str_0, const char* str_1);

KAPI void string_insert_char_at(char* dest, const char* src, u32 pos, char c);
KAPI void string_insert_str_at(char* dest, const char* src, u32 pos, const char* str);
KAPI void string_remove_at(char* dest, const char* src, u32 pos, u32 length);

/**
 * @brief Attempts to parse a 4x4 matrix from the provided string.
 *
 * @param str The string to parse from. Should be space delimited. (i.e "1.0 1.0 ... 1.0")
 * @param out_mat A pointer to the matrix to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_mat4(const char* str, mat4* out_mat);

/**
 * @brief Creates a string representation of the provided matrix.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param m The matrix to convert to string.
 * @return The string representation of the matrix.
 */
KAPI const char* mat4_to_string(mat4 m);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0 4.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec4(const char* str, vec4* out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char* vec4_to_string(vec4 v);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec3(const char* str, vec3* out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char* vec3_to_string(vec3 v);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec2(const char* str, vec2* out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char* vec2_to_string(vec2 v);

/**
 * @brief Attempts to parse a 32-bit floating-point number from the provided string.
 *
 * @param str The string to parse from. Should *not* be postfixed with 'f'.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f32(const char* str, f32* f);

/**
 * @brief Creates a string representation of the provided float.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param f The float to convert to string.
 * @return The string representation of the provided float.
 */
KAPI const char* f32_to_string(f32 f);

/**
 * @brief Attempts to parse a 64-bit floating-point number from the provided string.
 *
 * @param str The string to parse from.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f64(const char* str, f64* f);

/**
 * @brief Creates a string representation of the provided 64-bit float.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param f The 64-bit float to convert to string.
 * @return The string representation of the provided 64-bit float.
 */
KAPI const char* f64_to_string(f64 f);

/**
 * @brief Attempts to parse an 8-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i8(const char* str, i8* i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* i8_to_string(i8 i);

/**
 * @brief Attempts to parse a 16-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i16(const char* str, i16* i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* i16_to_string(i16 i);

/**
 * @brief Attempts to parse a 32-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i32(const char* str, i32* i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* i32_to_string(i32 i);

/**
 * @brief Attempts to parse a 64-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i64(const char* str, i64* i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* i64_to_string(i64 i);

/**
 * @brief Attempts to parse an 8-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u8(const char* str, u8* u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* u8_to_string(u8 u);

/**
 * @brief Attempts to parse a 16-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u16(const char* str, u16* u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* u16_to_string(u16 u);

/**
 * @brief Attempts to parse a 32-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u32(const char* str, u32* u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* u32_to_string(u32 u);

/**
 * @brief Attempts to parse a 64-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u64(const char* str, u64* u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char* u64_to_string(u64 u);

/**
 * @brief Attempts to parse a boolean from the provided string.
 * "true" or "1" are considered true; anything else is false.
 *
 * @param str The string to parse from. "true" or "1" are considered true; anything else is false.
 * @param b A pointer to the boolean to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_bool(const char* str, b8* b);

/**
 * @brief Creates a string representation of the provided boolean, i.e. "false" for false/0 and
 * "true" for everything else.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param b The boolean to create a string from.
 * @return The string representation of the provided boolean.
 */
KAPI const char* bool_to_string(b8 b);

/**
 * @brief Splits the given string by the delimiter provided and stores in the
 * provided darray. Optionally trims each entry.
 * NOTE: A string allocation occurs for each entry, and MUST be freed by the caller.
 *
 * @param str The string to be split.
 * @param delimiter The character to split by.
 * @param str_darray A pointer to a darray of char arrays to hold the entries. NOTE: must be a darray.
 * @param trim_entries Trims each entry if true.
 * @param include_empty Indicates if empty entries should be included.
 * @return The number of entries yielded by the split operation.
 */
KAPI u32 string_split(const char* str, char delimiter, char*** str_darray, b8 trim_entries, b8 include_empty);

/**
 * @brief Cleans up string allocations in str_darray, but does not
 * free the darray itself.
 *
 * @param str_darray The darray to be cleaned up.
 */
KAPI void string_cleanup_split_darray(char** str_darray);

/**
 * @brief Splits the given string by the delimiter provided and stores in the
 * provided fixed-size array. Optionally trims each entry.
 * NOTE: A string allocation occurs for each entry, and MUST be freed by the caller.
 *
 * @param str The string to be split.
 * @param delimiter The character to split by.
 * @param max_count The maximum number of entries to split.
 * @param str_darray A fixed-size array of char arrays. Must be large enough to hold max_count entries.
 * @param trim_entries Trims each entry if true.
 * @param include_empty Indicates if empty entries should be included.
 * @return The number of entries yielded by the split operation.
 */
KAPI u32 string_nsplit(const char* str, char delimiter, u32 max_count, char** str_array, b8 trim_entries, b8 include_empty);

/**
 * @brief Cleans up string allocations in the fixed-size str_array, but does not
 * free the array itself.
 *
 * @param str_darray The fixed-size array to be cleaned up.
 * @param max_count The number of entries (and thus the size) of the fixed-size array.
 */
KAPI void string_cleanup_split_array(char** str_array, u32 max_count);

/**
 * Appends append to source and returns a new string.
 * @param dest The destination string.
 * @param source The string to be appended to.
 * @param append The string to append to source.
 * @returns A new string containing the concatenation of the two strings.
 */
KAPI void string_append_string(char* dest, const char* source, const char* append);

/**
 * @brief Appends the supplied integer to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param i The integer to be appended.
 */
KAPI void string_append_int(char* dest, const char* source, i64 i);

/**
 * @brief Appends the supplied float to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param f The float to be appended.
 */
KAPI void string_append_float(char* dest, const char* source, f32 f);

/**
 * @brief Appends the supplied boolean (as either "true" or "false") to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param b The boolean to be appended.
 */
KAPI void string_append_bool(char* dest, const char* source, b8 b);

/**
 * @brief Appends the supplied character to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param c The character to be appended.
 */
KAPI void string_append_char(char* dest, const char* source, char c);

/**
 * @brief Extracts the directory from a full file path.
 *
 * @param dest The destination for the path.
 * @param path The full path to extract from.
 */
KAPI void string_directory_from_path(char* dest, const char* path);

/**
 * @brief Extracts the filename (including file extension) from a full file path.
 *
 * @param dest The destination for the filename.
 * @param path The full path to extract from.
 */
KAPI void string_filename_from_path(char* dest, const char* path);

/**
 * @brief Extracts the filename (excluding file extension) from a full file path.
 *
 * @param dest The destination for the filename.
 * @param path The full path to extract from.
 */
KAPI void string_filename_no_extension_from_path(char* dest, const char* path);

/**
 * @brief Attempts to get the file extension from the given path. Allocates a new string which should be freed.
 *
 * @param path The full path to extract from.
 * @param include_dot Indicates if the '.' should be included in the output.
 * @returns The extension on success; otherwise 0.
 */
KAPI const char* string_extension_from_path(const char* path, b8 include_dot);

/**
 * @brief Attempts to extract an array length from a given string. Ex: a string of sampler2D[4] will return True and set out_length to 4.
 * @param str The string to examine.
 * @param out_length A pointer to hold the length, if extracted successfully.
 * @returns True if an array length was found and parsed; otherwise false.
 */
KAPI b8 string_parse_array_length(const char* str, u32* out_length);

KAPI b8 string_line_get(const char* source_str, u16 max_line_length, u32 start_from, char** out_buffer, u32* out_line_length);

/** Indicates if provided codepoint is lower-case. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_lower(i32 codepoint);
/** Indicates if provided codepoint is upper-case. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_upper(i32 codepoint);
/** Indicates if provided codepoint is alpha-numeric. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_alpha(i32 codepoint);
/** Indicates if provided codepoint is numeric. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_numeric(i32 codepoint);
/** Indicates if the given codepoint is considered to be a space. Includes ' ', \f \r \n \t and \v. */
KAPI b8 codepoint_is_space(i32 codepoint);

/**
 * Converts string in-place to uppercase. Regular ASCII and western European high-ascii characters only.
 */
KAPI void string_to_lower(char* str);
/**
 * Converts string in-place to uppercase. Regular ASCII and western European high-ascii characters only.
 */
KAPI void string_to_upper(char* str);

// ----------------------
// KString implementation
// ----------------------

/**
 * @brief A kstring is a managed string for higher-level logic to use. It is
 * safer and, in some cases quicker than a typical cstring because it maintains
 * length/allocation information and doesn't have to use strlen on most of its
 * internal operations.
 */
typedef struct kstring {
    /** @brief The current length of the string in bytes. */
    u32 length;
    /** @brief The amount of currently allocated memory. Always accounts for a null terminator. */
    u32 allocated;
    /** @brief The raw string data. */
    char* data;
} kstring;

KAPI void kstring_create(kstring* out_string);
KAPI void kstring_from_cstring(const char* source, kstring* out_string);
KAPI void kstring_destroy(kstring* string);

KAPI u32 kstring_length(const kstring* string);
KAPI u32 kstring_utf8_length(const kstring* string);

KAPI void kstring_append_str(kstring* string, const char* s);
KAPI void kstring_append_kstring(kstring* string, const kstring* other);
