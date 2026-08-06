/**
 * @file kstring.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains a basic C-string and length-based string handling library.
 * @version 2.0
 * @date 2026-07-26
 *
 * Updates:
 * Version	| Date       | Author			| Description
 * ========================================================================
 * 1.0		| 2022-01-10 | Travis Vroman	| Original implementation of c-string based library.
 * 2.0		| 2026-07-26 | Travis Vroman	| Added initial implementation (length-based strings)
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <stdarg.h>

#include "defines.h"
#include "math/math_types.h"

KAPI char *_kstrchr (const char *str, i32 c);
KAPI char *_kstrstr (const char *haystack, const char *needle);
KAPI i32 _kisdigit (int c);
KAPI i32 _kisalpha (i32 c);

#define USE_STB_SPRINTF 1
#ifdef USE_STB_SPRINTF
#	include "vendor/stb_printf.h"
#	define ksnprintf stbsp_snprintf
#	define ksprintf stbsp_sprintf
#	define kvsnprintf stbsp_vsnprintf
#	define kvsprintf stbsp_vsprintf
// Not STB-provided, but still required to get rid of some std lib things.
#	define kstrchr _kstrchr
#	define kstrstr _kstrstr
#	define kisdigit _kisdigit
#	define kisalpha _kisalpha
#else
#	include <stdio.h>
#	define ksnprintf snprintf
#	define ksprintf sprintf
#	define kvsnprintf vsnprintf
#	define kvsprintf vsprintf
#	define kstrchr strchr
#	define kstrstr strstr
#	define kisdigit isdigit
#	define kisalpha isalpha
#endif

typedef unsigned short kwchar;

/**
 * @brief Gets the number of bytes of the given string, minus the null terminator.
 *
 * NOTE: For strings without a null terminator, use string_nlength instead.
 *
 * @param str The string whose length to obtain.
 * @returns The length of the string.
 */
KAPI u64 string_length (const char *str);

/**
 * @brief Gets the length of a string in UTF-8 (potentially multibyte) characters, minus the null terminator.
 *
 * NOTE: For strings without a null terminator, use string_utf8_nlength instead.
 *
 * @param str The string to examine.
 * @return The UTF-8 length of the string.
 */
KAPI u32 string_utf8_length (const char *str);

/**
 * @brief Gets the number of bytes of the given string, minus the null terminator, but at most max_len.
 * This function only ever looks at the bytes pointed to in str up until, but never beyond, max_len - 1.
 *
 * @param str The string whose length to obtain.
 * @param max_len The maximum number of bytes to examine in the string.
 * @returns The length of the string, at most max_len.
 */
KAPI u64 string_nlength (const char *str, u32 max_len);

/**
 * @brief Gets the number of characters (multibyte = 1 character) of a string in UTF-8 (potentially multibyte) characters, minus the null terminator, but at most max_len.
 * This function only ever looks at the characters pointed to in str up until, but never beyond, max_len - 1.
 *
 * @param str The string to examine.
 * @param max_len The maximum number of characters to examine in the string.
 * @return The number of multibyte characters in the string, at most max_len.
 */
KAPI u32 string_utf8_nlength (const char *str, u32 max_len);

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
KAPI b8 bytes_to_codepoint (const char *bytes, u32 offset, i32 *out_codepoint, u8 *out_advance);

/**
 * @brief Indicates if the provided character is considered whitespace.
 *
 * @param c The character to examine.
 * @return True if whitespace; otherwise false.
 */
KAPI b8 char_is_whitespace (char c);

/**
 * @brief Indicates if the provided codepoint is considered whitespace.
 *
 * @param codepoint The codepoint to examine.
 * @return True if whitespace; otherwise false.
 */
KAPI b8 codepoint_is_whitespace (i32 codepoint);

/**
 * @brief Duplicates the provided string. Note that this allocates new memory,
 * which should be freed by the caller.
 * @param str The string to be duplicated.
 * @returns A pointer to a newly-created character array (string).
 */
KAPI char *string_duplicate (const char *str);

/**
 * @brief Frees the memory of the given string.
 *
 * @param str The string to be freed.
 */
KAPI void string_free (const char *str);

KAPI i64 kstr_ncmp (const char *str0, const char *str1, u32 max_len);

KAPI i64 kstr_ncmpi (const char *str0, const char *str1, u32 max_len);

/**
 * @brief Case-sensitive string comparison.
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @returns True if the same, otherwise false.
 */
KAPI b8 strings_equal (const char *str0, const char *str1);

/**
 * @brief Case-insensitive string comparison.
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @returns True if the same, otherwise false.
 */
KAPI b8 strings_equali (const char *str0, const char *str1);

/**
 * @brief Case-sensitive string comparison, where comparison stops at max_len.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param max_len The maximum number of bytes to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequal (const char *str0, const char *str1, u32 max_len);

/**
 * @brief Case-insensitive string comparison, where comparison stops at max_len.
 *
 * @param str0 The first string to be compared.
 * @param str1 The second string to be compared.
 * @param max_len The maximum number of bytes to be compared.
 * @return True if the same, otherwise false.
 */
KAPI b8 strings_nequali (const char *str0, const char *str1, u32 max_len);

/**
 * @brief Performs string formatting against the given format string and parameters.
 *
 * Accepts custom format options:
 *   Vector types - %V#[D][.N], where # is number of elements, D optionally converts radians
 *     to degrees before printing, and .N is the number of places after the decimal point, clamped to 8.
 *     Ex: %V3D.4 displays a vec3, converts to degrees and uses 4 decimal places.
 *   KNames - %k, pass a kname argument and kname_string_get is called automatically.
 *   KStrings - %K, pass a kstring argument and kstring_cstr is called automatically.
 * NOTE: that this performs a dynamic allocation and should be freed by the caller.
 *
 * @param format The format string to use for the operation
 * @param ... The format arguments.
 * @returns The newly-formatted string (dynamically allocated).
 */
KAPI char *string_format (const char *format, ...);

/**
 * @brief Performs variadic string formatting against the given format string and va_list.
 * NOTE: that this performs a dynamic allocation and should be freed by the caller.
 *
 * @param format The string to be formatted.
 * @param va_listp The variadic argument list.
 * @returns The newly-formatted string (dynamically allocated).
 */
KAPI char *string_format_v (const char *format, va_list va_listp);

/**
 * @brief Performs string formatting to dest given format string and parameters.
 * @deprecated
 * @note This version of the function is unsafe. Use string_format() instead.
 * @param dest The destination for the formatted string.
 * @param format The format string to use for the operation
 * @param ... The format arguments.
 * @returns The length of the newly-formatted string.
 */
KDEPRECATED("This version of string format is legacy, and unsafe. Use string_nformat() or string_format() instead.")
KAPI i32 string_format_unsafe (char *dest, const char *format, ...);

/**
 * @brief Performs variadic string formatting to dest given format string and va_list.
 * @deprecated
 * @note This version of the function is unsafe. Use string_format() instead.
 * @param dest The destination for the formatted string.
 * @param format The string to be formatted.
 * @param va_list The variadic argument list.
 * @returns The size of the data written.
 */
KDEPRECATED("This version of string format variadic is legacy, and unsafe. Use string_nformat_v() or string_format_v() instead.")
KAPI i32 string_format_v_unsafe (char *dest, const char *format, void *va_list);

/**
 * @brief Performs string formatting to dest given format string up to max_len length in bytes and parameters.
 *
 * @param dest A pointer to the destination buffer. Must be at least max_len large.
 * @param max_len The maximum number of bytes to be output.
 * @param format The string to be formatted.
 * @param ... The format arguments.
 * @returns The size of the data written. -1 if failed.
 */
KAPI i32 string_nformat (char *dest, u32 max_len, const char *format, ...);

/**
 * @brief Performs variadic string formatting to dest given format string up to max_len length in bytes and va_list.
 *
 * @param dest A pointer to the destination buffer. Must be at least max_len large.
 * @param max_len The maximum number of bytes to be output.
 * @param format The string to be formatted.
 * @param va_list The variadic argument list.
 * @returns The size of the data written. -1 if failed.
 */
KAPI i32 string_nformat_v (char *dest, u32 max_len, const char *format, void *va_list);

/**
 * @brief Empties the provided string by setting the first character to 0.
 *
 * @param str The string to be emptied.
 * @return A pointer to str.
 */
KAPI char *string_empty (char *str);

/**
 * @brief Copies the string in source to dest. Does not perform any allocations.
 * @param dest The destination string.
 * @param source The source string.
 * @returns A pointer to the destination string.
 */
KAPI char *string_copy (char *dest, const char *source);

/**
 * @brief Copies the bytes in the source buffer into the dest buffer up to the given length. Does not perform any allocations.
 * Any remaining length after a 0 terminator will be zero-padded unless max_len is U32_MAX.
 *
 * @param dest A pointer to the destination buffer. Must be at least max_len large.
 * @param source A constant pointer to the source buffer.
 * @param length The maximum number of bytes to be copied.
 * @returns A pointer to the destination string.
 */
KAPI char *string_ncopy (char *dest, const char *source, u32 max_len);

/**
 * @brief Performs an in-place trim of the provided string.
 * This removes all whitespace from both ends of the string.
 *
 * Done by placing zeroes in the string at relevant points.
 * @param str The string to be trimmed.
 * @returns A pointer to the trimmed string.
 */
KAPI char *string_trim (char *str);

/**
 * @brief Gets a substring of the source string between start and length or to the end of the string.
 * If length is negative, goes to the end of the string.
 *
 * Done by placing zeroes in the string at relevant points.
 * @param str The string to be trimmed.
 */
KAPI void string_mid (char *dest, const char *source, i32 start, i32 length);

/**
 * @brief Returns the index of the first occurance of c in str; otherwise -1.
 *
 * @param str The string to be scanned.
 * @param c The character to search for.
 * @return The index of the first occurance of c; otherwise -1 if not found.
 */
KAPI i32 string_index_of (const char *str, char c);

/**
 * @brief Returns the index of the last occurance of c in str; otherwise -1.
 *
 * @param str The string to be scanned.
 * @param c The character to search for.
 * @return The index of the last occurance of c; otherwise -1 if not found.
 */
KAPI i32 string_last_index_of (const char *str, char c);

/**
 * @brief Returns the index of the first occurance of str_1 in str_0; otherwise -1.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return The index of the first occurance of str_1; otherwise -1 if not found.
 */
KAPI i32 string_index_of_str (const char *str_0, const char *str_1);

/**
 * @brief Returns the index of the first occurance of str_1 in str_0; otherwise -1. Case-insensitive.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return The index of the first occurance of str_1; otherwise -1 if not found.
 */
KAPI i32 string_index_of_stri (const char *str_0, const char *str_1);

/**
 * @brief Indicates if str_0 starts with str_1. Case-sensitive.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return True if str_0 starts with str_1; otherwise false.
 */
KAPI b8 string_starts_with (const char *str_0, const char *str_1);

/**
 * @brief Indicates if str_0 starts with str_1. Case-insensitive.
 *
 * @param str_0 The string to be scanned.
 * @param str_1 The substring to search for.
 * @return True if str_0 starts with str_1; otherwise false.
 */
KAPI b8 string_starts_withi (const char *str_0, const char *str_1);

KAPI void string_insert_char_at (char *dest, const char *src, u32 pos, char c);
KAPI void string_insert_str_at (char *dest, const char *src, u32 pos, const char *str);
KAPI void string_remove_at (char *dest, const char *src, u32 pos, u32 length);

/**
 * @brief Replaces the first instance of char find with the one provided (replace).
 * Done in place.
 *
 * @param str The string to be operated on.
 * @param find The character to search for.
 * @param replace The character to replace find with.
 * @return Index of the replaced char, or -1 if not found.
 */
KAPI i32 string_replace_char (char *str, char find, char replace);

/**
 * @brief Replaces all instances of char find with the one provided (replace).
 * Done in place.
 *
 * @param str The string to be operated on.
 * @param find The character to search for.
 * @param replace The character to replace find with.
 * @return Count of instances found and replaced.
 */
KAPI u32 string_replace_char_all (char *str, char find, char replace);

/**
 * @brief Attempts to parse a 4x4 matrix from the provided string.
 *
 * @param str The string to parse from. Should be space delimited. (i.e "1.0 1.0 ... 1.0")
 * @param out_mat A pointer to the matrix to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_mat4 (const char *str, mat4 *out_mat);

/**
 * @brief Creates a string representation of the provided matrix.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param m The matrix to convert to string.
 * @return The string representation of the matrix.
 */
KAPI const char *mat4_to_string (mat4 m);

/**
 * @brief Attempts to parse a rect_2di from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1 2 3 4")
 * @param out_vector A pointer to the rect to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_rect_2di (const char *str, rect_2di *rect);

/**
 * @brief Creates a string representation of the provided rectangle.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The rectangle to convert to string.
 * @return The string representation of the rectangle.
 */
KAPI const char *rect_2di_to_string (rect_2di rect);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0 4.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec4 (const char *str, vec4 *out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char *vec4_to_string (vec4 v);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0 3.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec3 (const char *str, vec3 *out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char *vec3_to_string (vec3 v);

/**
 * @brief Attempts to parse a vector from the provided string.
 *
 * @param str The string to parse from. Should be space-delimited. (i.e. "1.0 2.0")
 * @param out_vector A pointer to the vector to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_vec2 (const char *str, vec2 *out_vector);

/**
 * @brief Creates a string representation of the provided vector.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param v The vector to convert to string.
 * @return The string representation of the vector.
 */
KAPI const char *vec2_to_string (vec2 v);

/**
 * @brief Attempts to parse a 32-bit floating-point number from the provided string.
 *
 * @param str The string to parse from. Should *not* be postfixed with 'f'.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f32 (const char *str, f32 *f);

/**
 * @brief Creates a string representation of the provided float.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param f The float to convert to string.
 * @return The string representation of the provided float.
 */
KAPI const char *f32_to_string (f32 f);

/**
 * @brief Attempts to parse a 64-bit floating-point number from the provided string.
 *
 * @param str The string to parse from.
 * @param f A pointer to the float to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_f64 (const char *str, f64 *f);

/**
 * @brief Creates a string representation of the provided 64-bit float.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param f The 64-bit float to convert to string.
 * @return The string representation of the provided 64-bit float.
 */
KAPI const char *f64_to_string (f64 f);

/**
 * @brief Attempts to parse an 8-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i8 (const char *str, i8 *i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *i8_to_string (i8 i);

/**
 * @brief Attempts to parse a 16-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i16 (const char *str, i16 *i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *i16_to_string (i16 i);

/**
 * @brief Attempts to parse a 32-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i32 (const char *str, i32 *i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *i32_to_string (i32 i);

/**
 * @brief Attempts to parse a 64-bit signed integer from the provided string.
 *
 * @param str The string to parse from.
 * @param i A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_i64 (const char *str, i64 *i);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param i The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *i64_to_string (i64 i);

/**
 * @brief Attempts to parse an 8-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u8 (const char *str, u8 *u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *u8_to_string (u8 u);

/**
 * @brief Attempts to parse a 16-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u16 (const char *str, u16 *u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *u16_to_string (u16 u);

/**
 * @brief Attempts to parse a 32-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u32 (const char *str, u32 *u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *u32_to_string (u32 u);

/**
 * @brief Attempts to parse a 64-bit unsigned integer from the provided string.
 *
 * @param str The string to parse from.
 * @param u A pointer to the int to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_u64 (const char *str, u64 *u);

/**
 * @brief Creates a string representation of the provided integer.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param u The integer to create a string from.
 * @return The string representation of the provided integer.
 */
KAPI const char *u64_to_string (u64 u);

/**
 * @brief Attempts to parse a boolean from the provided string.
 * "true" or "1" are considered true; anything else is false.
 *
 * @param str The string to parse from. "true" or "1" are considered true; anything else is false.
 * @param b A pointer to the boolean to write to.
 * @return True if parsed successfully; otherwise false.
 */
KAPI b8 string_to_bool (const char *str, b8 *b);

/**
 * @brief Creates a string representation of the provided boolean, i.e. "false" for false/0 and
 * "true" for everything else.
 * NOTE: string is dynamically allocated, so the caller should free it.
 *
 * @param b The boolean to create a string from.
 * @return The string representation of the provided boolean.
 */
KAPI const char *bool_to_string (b8 b);

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
 * @param escape_strings If a double-quote is run across, don't split delimiter inside it.
 * @return The number of entries yielded by the split operation.
 */
KAPI u32 string_split (const char *str, char delimiter, char ***str_darray, b8 trim_entries, b8 include_empty, b8 escape_strings);

/**
 * @brief Cleans up string allocations in str_darray, but does not
 * free the darray itself.
 *
 * @param str_darray The darray to be cleaned up.
 */
KAPI void string_cleanup_split_darray (char **str_darray);

/**
 * @brief Cleans up string allocations in str_array and frees the array itself.
 *
 * NOTE: Not for use with darrays! Use string_cleanup_split_darray() instead or memory will be leaked.
 *
 * @param str_array The array to be cleaned up and freed.
 * @param length The number of string elements in the array.
 */
KAPI void string_cleanup_array (const char **str_array, u32 length);

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
KAPI u32 string_nsplit (const char *str, char delimiter, u32 max_count, char **str_array, b8 trim_entries, b8 include_empty);

/**
 * @brief Cleans up string allocations in the fixed-size str_array, but does not
 * free the array itself.
 *
 * @param str_darray The fixed-size array to be cleaned up.
 * @param max_count The number of entries (and thus the size) of the fixed-size array.
 */
KAPI void string_cleanup_split_array (char **str_array, u32 max_count);

/**
 * Appends append to source and returns a new string.
 * @param dest The destination string.
 * @param source The string to be appended to.
 * @param append The string to append to source.
 * @returns A new string containing the concatenation of the two strings.
 */
KAPI void string_append_string (char *dest, const char *source, const char *append);

/**
 * @brief Appends the supplied integer to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param i The integer to be appended.
 */
KAPI void string_append_int (char *dest, const char *source, i64 i);

/**
 * @brief Appends the supplied float to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param f The float to be appended.
 */
KAPI void string_append_float (char *dest, const char *source, f32 f);

/**
 * @brief Appends the supplied boolean (as either "true" or "false") to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param b The boolean to be appended.
 */
KAPI void string_append_bool (char *dest, const char *source, b8 b);

/**
 * @brief Appends the supplied character to source and outputs to dest.
 *
 * @param dest The destination for the string.
 * @param source The string to be appended to.
 * @param c The character to be appended.
 */
KAPI void string_append_char (char *dest, const char *source, char c);

/**
 * @brief Joins the array of strings given with the provided delimiter. The delimiter is not
 * used after the final entry.
 *
 * NOTE: This function dynamically allocates string memory. The string should be freed by the caller.
 *
 * @param strings The array of strings to be joined.
 * @param count The number of strings to be joined.
 * @param delimiter The delimiter character to join with.
 *
 * @returns The joined string. Should be freed by the caller.
 */
KAPI char *string_join (const char **strings, u32 count, char delimiter);

/**
 * @brief Extracts the directory from a full file path.
 *
 * @param path The full path to extract from.
 * @return The the directory.
 */
KAPI const char *string_directory_from_path (const char *path);

/**
 * @brief Extracts the filename (including file extension) from a full file path.
 *
 * NOTE: This function dynamically allocates string memory. The string should be freed by the caller.
 *
 * @param path The full path to extract from.
 * @return The filename with extension.
 */
KAPI const char *string_filename_from_path (const char *path);

/**
 * @brief Extracts the filename (excluding file extension) from a full file path.
 *
 * NOTE: This function dynamically allocates string memory. The string should be freed by the caller.
 *
 * @param path The full path to extract from.
 * @return The filename without extension.
 */
KAPI const char *string_filename_no_extension_from_path (const char *path);

/**
 * @brief Attempts to get the file extension from the given path. Allocates a new string which should be freed.
 *
 * NOTE: This function dynamically allocates string memory. The string should be freed by the caller.
 *
 * @param path The full path to extract from.
 * @param include_dot Indicates if the '.' should be included in the output.
 * @returns The extension on success; otherwise 0.
 */
KAPI const char *string_extension_from_path (const char *path, b8 include_dot);

/**
 * @brief Attempts to extract an array length from a given string. Ex: a string of sampler2D[4] will return True and set out_length to 4.
 * @param str The string to examine.
 * @param out_length A pointer to hold the length, if extracted successfully.
 * @returns True if an array length was found and parsed; otherwise false.
 */
KAPI b8 string_parse_array_length (const char *str, u32 *out_length);

KAPI b8 string_line_get (const char *source_str, u16 max_line_length, u32 start_from, char **out_buffer, u32 *out_line_length, u8 *out_addl_advance);

/** Indicates if provided codepoint is lower-case. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_lower (i32 codepoint);
/** Indicates if provided codepoint is upper-case. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_upper (i32 codepoint);
/** Indicates if provided codepoint is alpha-numeric. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_alpha (i32 codepoint);
/** Indicates if provided codepoint is numeric. Regular ASCII and western European high-ascii characters only. */
KAPI b8 codepoint_is_numeric (i32 codepoint);
/** Indicates if the given codepoint is considered to be a space. Includes ' ', \f \r \n \t and \v. */
KAPI b8 codepoint_is_space (i32 codepoint);

/**
 * Converts string in-place to uppercase. Regular ASCII and western European high-ascii characters only.
 */
KAPI void string_to_lower (char *str);
/**
 * Converts string in-place to uppercase. Regular ASCII and western European high-ascii characters only.
 */
KAPI void string_to_upper (char *str);

// ----------------------
// KString implementation
// ----------------------

#define KSTRING_DEFAULT_BUF_SIZE 20

/**
 * @brief A kstring is a managed string for higher-level logic to use. It is
 * safer and, in some cases quicker than a typical cstring because it maintains
 * length/allocation information and doesn't have to use strlen on most of its
 * internal operations.
 */
typedef struct kstring {
	/** @brief The current length of the string in bytes. This is _not_ character length! */
	u32 length;
	/** @brief The amount of currently allocated memory. Always accounts for a null terminator. */
	u32 allocated;
	/**
	 * @brief The raw string data. All data is stored as narrow strings, or multibyte.
	 * Functions that require wide strings can convert them thusly.
	 * NOTE: If data == base_buffer, then no allocation exists.
	 */
	char *data;

	// For short strings, just use this instead of a fresh allocation.
	char base_buffer[KSTRING_DEFAULT_BUF_SIZE];
} kstring;

KAPI kstring kstring_create (void);
KAPI kstring kstring_from_cstring (const char *source);
KAPI kstring kstring_duplicate (kstring source);
KAPI void kstring_destroy (kstring *string);

/**
 * @brief Allocates and returns new c-string with the contents of the given kstring.
 *
 * NOTE: Returns allocated "" on empty, which still must be freed.
 *
 * @returns Newly-allocated c-string with the contents of the given kstring.
 */
KAPI char *kstring_cstr (kstring source);

/**
 * @brief Indicates if kstrings a and b are the same. Case-sensitive.
 */
KAPI b8 kstrings_equal (kstring a, kstring b);
/**
 * @brief Indicates if kstrings a and b are the same. Case-insensitive.
 */
KAPI b8 kstrings_equali (kstring a, kstring b);

/**
 * @brief Indicates if the content of kstring a and c-string b are the same. Case-sensitive.
 */
KAPI b8 kstring_cstr_equal (kstring a, const char *b);
/**
 * @brief Indicates if the content of kstring a and c-string b are the same. Case-sensitive.
 */
KAPI b8 kstring_cstr_equali (kstring a, const char *b);

/**
 * @brief Returns the length of the provided kstring's content in bytes.
 * NOTE: For strings containing multibyte characters, use kstring_utf8_length() instead.
 */
KAPI u32 kstring_length (kstring string);
/**
 * @brief Returns the length of the provided kstring's content in characters (not bytes!), including multibyte characters.
 */
KAPI u32 kstring_utf8_length (kstring string);

/**
 * @brief Gets the index of the first occurance of char c within the given kstring. -1 if not found.
 */
KAPI i32 kstring_index_of_char (kstring string, char c);
/**
 * @brief Gets the index of the first occurance of kstring text within the given kstring. -1 if not found.
 */
KAPI i32 kstring_index_of_kstring (kstring string, kstring text);
/**
 * @brief Gets the index of the first occurance of c-string text within the given kstring. -1 if not found.
 */
KAPI i32 kstring_index_of_cstr (kstring string, const char *text);

/**
 * @brief Appends raw data to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_data (kstring string, const void *data, u32 length);

/**
 * @brief Appends the content of the provided c-string to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_cstr (kstring string, const char *s);
/**
 * @brief Appends the content of the provided kstring "other" to and returns a copy of the given kstring "string".
 * Original string not modified.
 */
KAPI kstring kstring_append_kstring (kstring string, kstring other);

/**
 * @brief Appends the provided char to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_char (kstring string, char c);
/**
 * @brief Appends "true" or "false" to and returns a copy of the given kstring.
 */
KAPI kstring kstring_append_bool (kstring string, b8 b);

/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_i8 (kstring string, i8 i);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_i16 (kstring string, i16 i);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_i32 (kstring string, i32 i);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_i64 (kstring string, i64 i);

/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_u8 (kstring string, u8 u);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_u16 (kstring string, u16 u);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_u32 (kstring string, u32 u);
/**
 * @brief Appends the string representation of the provided number to and returns a copy of the given kstring.
 * Original string not modified.
 */
KAPI kstring kstring_append_u64 (kstring string, u64 u);

/**
 * @brief Appends the string representation of the provided floating-point number to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float value instead of rounding it.
 */
KAPI kstring kstring_append_f32 (kstring string, f32 f, u8 decimal_places);
/**
 * @brief Appends the string representation of the provided floating-point number to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float value instead of rounding it.
 */
KAPI kstring kstring_append_f64 (kstring string, f64 f, u8 decimal_places);

/**
 * @brief Appends the string representation of the provided vector to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float values instead of rounding them.
 */
KAPI kstring kstring_append_vec2 (kstring string, vec2 v, u8 decimal_places);
/**
 * @brief Appends the string representation of the provided vector to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float values instead of rounding them.
 */
KAPI kstring kstring_append_vec3 (kstring string, vec3 v, u8 decimal_places);
/**
 * @brief Appends the string representation of the provided vector to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float values instead of rounding them.
 */
KAPI kstring kstring_append_vec4 (kstring string, vec4 v, u8 decimal_places);

/**
 * @brief Appends the string representation of the provided matrix to and returns a copy of the given kstring.
 * Original string not modified. NOTE: this function truncates the float values instead of rounding them.
 */
KAPI kstring kstring_append_mat4 (kstring string, mat4 m, u8 decimal_places);

/**
 * @brief Performs a left trim on and returns a copy of the given string.
 * Original string not modified.
 */
KAPI kstring kstring_ltrim (kstring string);
/**
 * @brief Performs a right trim on and returns a copy of the given string.
 * Original string not modified.
 */
KAPI kstring kstring_rtrim (kstring string);
/**
 * @brief Performs both a left and a right trim on and returns a copy of the given string.
 * Original string not modified.
 */
KAPI kstring kstring_trim (kstring string);
/**
 * @brief Returns a substring of the orignal string provided as a copy. Start and length are
 * automatically bounds checked and clamped to a valid range.
 * Original string not modified.
 */
KAPI kstring kstring_substr (kstring string, u32 start, u32 length);

/**
 * @brief Performs string formatting against the given format string and parameters.
 *
 * Accepts custom format options:
 *   Vector types - %V#[D][.N], where # is number of elements, D optionally converts radians
 *     to degrees before printing, and .N is the number of places after the decimal point, clamped to 8.
 *     Ex: %V3D.4 displays a vec3, converts to degrees and uses 4 decimal places.
 *   KNames - %k, pass a kname argument and kname_string_get is called automatically.
 *   KStrings - %K, pass a kstring argument and kstring_cstr is called automatically.
 *
 * @param fmt The format string to use for the operation
 * @param ... The format arguments.
 * @returns The newly-formatted kstring.
 */
KAPI kstring kstring_format (const char *fmt, ...);
