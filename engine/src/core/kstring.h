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

struct scene_xform_config;

/**
 * @brief Gets the length of the given string.
 * @param str The string whose length to obtain.
 * @returns The length of the string.
 */
KAPI u64 string_length(const char* str);

/**
 * @brief Gets the length of a string in UTF-8 (potentially multibyte) characters.
 *
 * @param str The string to examine.
 * @return The UTF-8 length of the string.
 */
KAPI u32 string_utf8_length(const char* str);

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
KAPI void string_free(char* str);

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
 * @brief Case-sensitive string comparison for a number of characters.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param length The maximum number of characters to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequal(const char* str0, const char* str1, u64 length);

/**
 * @brief Case-insensitive string comparison for a number of characters.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param length The maximum number of characters to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequali(const char* str0, const char* str1, u64 length);

/**
 * @brief Performs string formatting to dest given format string and parameters.
 * @param dest The destination for the formatted string.
 * @param format The format string to use for the operation
 * @param ... The format arguments.
 * @returns The length of the newly-formatted string.
 */
KAPI i32 string_format(char* dest, const char* format, ...);

/**
 * @brief Performs variadic string formatting to dest given format string and va_list.
 * @param dest The destination for the formatted string.
 * @param format The string to be formatted.
 * @param va_list The variadic argument list.
 * @returns The size of the data written.
 */
KAPI i32 string_format_v(char* dest, const char* format, void* va_list);

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
 * @brief Copies the string in source to dest up to the given length. Does not perform any allocations.
 * @param dest The destination string.
 * @param source The source string.
 * @param length The maximum length to be copied.
 * @returns A pointer to the destination string.
 */
KAPI char* string_ncopy(char* dest, const char* source, i64 length);

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
 * @brief Attempts to parse a transform from the provided string.
 * If the string contains 10 elements, rotation is parsed as quaternion.
 * If it contains 9 elements, rotation is parsed as euler angles and is
 * converted to quaternion. Anything else is invalid.
 *
 * @param str The string to parse from.
 * @param out_transform A pointer to the transform to write to.
 * @return True if parsed successfully, otherwise false.
 */
KAPI b8 string_to_transform(const char* str, transform* out_transform);

/**
 * @brief Attempts to parse a xform config (_NOT_ an actual xform) from the provided string.
 * If the string contains 10 elements, rotation is parsed as quaternion.
 * If it contains 9 elements, rotation is parsed as euler angles and is
 * converted to quaternion. Anything else is invalid.
 *
 * @param str The string to parse from.
 * @param out_xform A pointer to the xform to write to.
 * @return True if parsed successfully, otherwise false.
 */
KAPI b8 string_to_xform_config(const char* str, struct scene_xform_config* out_xform);

/**
 * @brief Attempts to parse a 4x4 matrix from the provided string.
 *
 * @param str The string to parse from. Should be space delimited. (i.e "1.0 1.0 ... 1.0")
 * @param out_mat A pointer to the matrix to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_mat4(const char* str, mat4* out_mat);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0 4.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec4(const char* str, vec4* out_vector);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec3(const char* str, vec3* out_vector);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec2(const char* str, vec2* out_vector);

/**
 * @brief Attempts to parse a 32-bit floating-point number from the provided string.
 *
 * @param str The string to parse from. Should *not* be postfixed with 'f'.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f32(const char* str, f32* f);

/**
 * @brief Attempts to parse a 64-bit floating-point number from the provided string.
 *
 * @param str The string to parse from.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f64(const char* str, f64* f);

/**
 * @brief Attempts to parse an 8-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i8(const char* str, i8* i);

/**
 * @brief Attempts to parse a 16-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i16(const char* str, i16* i);

/**
 * @brief Attempts to parse a 32-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i32(const char* str, i32* i);

/**
 * @brief Attempts to parse a 64-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i64(const char* str, i64* i);

/**
 * @brief Attempts to parse an 8-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u8(const char* str, u8* u);

/**
 * @brief Attempts to parse a 16-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u16(const char* str, u16* u);

/**
 * @brief Attempts to parse a 32-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u32(const char* str, u32* u);

/**
 * @brief Attempts to parse a 64-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u64(const char* str, u64* u);

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
 * @brief Splits the given string by the delimiter provided and stores in the
 * provided darray. Optionally trims each entry. NOTE: A string allocation
 * occurs for each entry, and must be freed by the caller.
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
KAPI void string_cleanup_split_array(char** str_darray);

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
 * @brief Attempts to extract an array length from a given string. Ex: a string of sampler2D[4] will return True and set out_length to 4.
 * @param str The string to examine.
 * @param out_length A pointer to hold the length, if extracted successfully.
 * @returns True if an array length was found and parsed; otherwise false.
 */
KAPI b8 string_parse_array_length(const char* str, u32* out_length);

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
