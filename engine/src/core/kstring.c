#include "core/kstring.h"

#include <ctype.h>  // isspace
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "math/transform.h"

#ifndef _MSC_VER
#include <strings.h>
#endif

u64 string_length(const char* str) {
    return strlen(str);
}

u32 string_utf8_length(const char* str) {
    u32 length = 0;
    for (u32 i = 0; i < __UINT32_MAX__; ++i, ++length) {
        i32 c = (i32)str[i];
        if (c == 0) {
            break;
        }
        if (c >= 0 && c < 127) {
            // Normal ascii character, don't increment again.
            // i += 0; // Basically doing this.
        } else if ((c & 0xE0) == 0xC0) {
            // Double-byte character, increment once more.
            i += 1;
        } else if ((c & 0xF0) == 0xE0) {
            // Triple-byte character, increment twice more.
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte character, increment thrice more.
            i += 3;
        } else {
            // NOTE: Not supporting 5 and 6-byte characters; return as invalid UTF-8.
            KERROR("kstring string_utf8_length() - Not supporting 5 and 6-byte characters; Invalid UTF-8.");
            return 0;
        }
    }

    return length;
}

b8 bytes_to_codepoint(const char* bytes, u32 offset, i32* out_codepoint, u8* out_advance) {
    i32 codepoint = (i32)bytes[offset];
    if (codepoint >= 0 && codepoint < 0x7F) {
        // Normal single-byte ascii character.
        *out_advance = 1;
        *out_codepoint = codepoint;
        return true;
    } else if ((codepoint & 0xE0) == 0xC0) {
        // Double-byte character
        codepoint = ((bytes[offset + 0] & 0b00011111) << 6) +
                    (bytes[offset + 1] & 0b00111111);
        *out_advance = 2;
        *out_codepoint = codepoint;
        return true;
    } else if ((codepoint & 0xF0) == 0xE0) {
        // Triple-byte character
        codepoint = ((bytes[offset + 0] & 0b00001111) << 12) +
                    ((bytes[offset + 1] & 0b00111111) << 6) +
                    (bytes[offset + 2] & 0b00111111);
        *out_advance = 3;
        *out_codepoint = codepoint;
        return true;
    } else if ((codepoint & 0xF8) == 0xF0) {
        // 4-byte character
        codepoint = ((bytes[offset + 0] & 0b00000111) << 18) +
                    ((bytes[offset + 1] & 0b00111111) << 12) +
                    ((bytes[offset + 2] & 0b00111111) << 6) +
                    (bytes[offset + 3] & 0b00111111);
        *out_advance = 4;
        *out_codepoint = codepoint;
        return true;
    } else {
        // NOTE: Not supporting 5 and 6-byte characters; return as invalid UTF-8.
        *out_advance = 0;
        *out_codepoint = 0;
        KERROR("kstring bytes_to_codepoint() - Not supporting 5 and 6-byte characters; Invalid UTF-8.");
        return false;
    }
}

char* string_duplicate(const char* str) {
    u64 length = string_length(str);
    char* copy = kallocate(length + 1, MEMORY_TAG_STRING);
    kcopy_memory(copy, str, length);
    copy[length] = 0;
    return copy;
}

void string_free(char* str) {
    if (str) {
        u64 size = 0;
        u16 alignment = 0;
        if (kmemory_get_size_alignment(str, &size, &alignment)) {
            kfree_aligned(str, size, alignment, MEMORY_TAG_STRING);
        } else {
            // TODO: report failure?
        }
    } else {
        // TODO: report null ptr?
    }
}

// Case-sensitive string comparison. True if the same, otherwise false.
b8 strings_equal(const char* str0, const char* str1) {
    return strcmp(str0, str1) == 0;
}

// Case-insensitive string comparison. True if the same, otherwise false.
b8 strings_equali(const char* str0, const char* str1) {
#if defined(__GNUC__)
    return strcasecmp(str0, str1) == 0;
#elif (defined _MSC_VER)
    return _strcmpi(str0, str1) == 0;
#endif
}

b8 strings_nequal(const char* str0, const char* str1, u64 length) {
    return strncmp(str0, str1, length) == 0;
}

b8 strings_nequali(const char* str0, const char* str1, u64 length) {
#if defined(__GNUC__)
    return strncasecmp(str0, str1, length) == 0;
#elif (defined _MSC_VER)
    return _strnicmp(str0, str1, length) == 0;
#endif
}

i32 string_format(char* dest, const char* format, ...) {
    if (dest) {
        __builtin_va_list arg_ptr;
        va_start(arg_ptr, format);
        i32 written = string_format_v(dest, format, arg_ptr);
        va_end(arg_ptr);
        return written;
    }
    return -1;
}

i32 string_format_v(char* dest, const char* format, void* va_listp) {
    if (dest) {
        // Big, but can fit on the stack.
        char buffer[32000];
        i32 written = vsnprintf(buffer, 32000, format, va_listp);
        buffer[written] = 0;
        kcopy_memory(dest, buffer, written + 1);

        return written;
    }
    return -1;
}

char* string_empty(char* str) {
    if (str) {
        str[0] = 0;
    }

    return str;
}

char* string_copy(char* dest, const char* source) {
    return strcpy(dest, source);
}

char* string_ncopy(char* dest, const char* source, i64 length) {
    return strncpy(dest, source, length);
}

char* string_trim(char* str) {
    while (isspace((unsigned char)*str)) {
        str++;
    }
    if (*str) {
        char* p = str;
        while (*p) {
            p++;
        }
        while (isspace((unsigned char)*(--p)))
            ;

        p[1] = '\0';
    }

    return str;
}

void string_mid(char* dest, const char* source, i32 start, i32 length) {
    if (length == 0) {
        return;
    }
    i32 src_length = (i32)string_length(source);
    if (start >= src_length) {
        dest[0] = 0;
        return;
    }
    if (length > 0) {
        for (i32 i = start, j = 0; j < length && source[i]; ++i, ++j) {
            dest[j] = source[i];
        }
        dest[start + length] = 0;
    } else {
        // If a negative value is passed, proceed to the end of the string.
        u64 j = 0;
        for (u64 i = start; source[i]; ++i, ++j) {
            dest[j] = source[i];
        }
        dest[start + j] = 0;
    }
}

i32 string_index_of(const char* str, char c) {
    if (!str) {
        return -1;
    }
    u32 length = string_length(str);
    if (length > 0) {
        for (u32 i = 0; i < length; ++i) {
            if (str[i] == c) {
                return i;
            }
        }
    }

    return -1;
}

b8 string_to_transform(const char* str, transform* out_transform) {
    if (!str || !out_transform) {
        return false;
    }

    kzero_memory(out_transform, sizeof(transform));
    f32 values[7] = {0};

    i32 count = sscanf(
        str,
        "%f %f %f %f %f %f %f %f %f %f",
        &out_transform->position.x, &out_transform->position.y, &out_transform->position.z,
        &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6]);

    if (count == 10) {
        // Treat as quat, load directly.
        out_transform->rotation.x = values[0];
        out_transform->rotation.y = values[1];
        out_transform->rotation.z = values[2];
        out_transform->rotation.w = values[3];

        // Set scale
        out_transform->scale.x = values[4];
        out_transform->scale.y = values[5];
        out_transform->scale.z = values[6];
    } else if (count == 9) {
        quat x_rot = quat_from_axis_angle((vec3){1.0f, 0, 0}, deg_to_rad(values[0]), true);
        quat y_rot = quat_from_axis_angle((vec3){0, 1.0f, 0}, deg_to_rad(values[1]), true);
        quat z_rot = quat_from_axis_angle((vec3){0, 0, 1.0f}, deg_to_rad(values[2]), true);
        out_transform->rotation = quat_mul(x_rot, quat_mul(y_rot, z_rot));
        
        // Set scale
        out_transform->scale.x = values[3];
        out_transform->scale.y = values[4];
        out_transform->scale.z = values[5];
    } else {
        KWARN("Format error: invalid transform provided. Identity transform will be used.");
        *out_transform = transform_create();
        return false;
    }

    out_transform->is_dirty = true;

    return true;
}

b8 string_to_mat4(const char* str, mat4* out_mat) {
    if (!str || !out_mat) {
        return false;
    }

    kzero_memory(out_mat, sizeof(mat4));
    i32 result = sscanf(str, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                        &out_mat->data[0],
                        &out_mat->data[1],
                        &out_mat->data[2],
                        &out_mat->data[3],
                        &out_mat->data[4],
                        &out_mat->data[5],
                        &out_mat->data[6],
                        &out_mat->data[7],
                        &out_mat->data[8],
                        &out_mat->data[9],
                        &out_mat->data[10],
                        &out_mat->data[11],
                        &out_mat->data[12],
                        &out_mat->data[13],
                        &out_mat->data[14],
                        &out_mat->data[15]);
    return result != -1;
}

b8 string_to_vec4(const char* str, vec4* out_vector) {
    if (!str || !out_vector) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec4));
    i32 result = sscanf(str, "%f %f %f %f", &out_vector->x, &out_vector->y, &out_vector->z, &out_vector->w);
    return result != -1;
}

b8 string_to_vec3(const char* str, vec3* out_vector) {
    if (!str || !out_vector) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec3));
    i32 result = sscanf(str, "%f %f %f", &out_vector->x, &out_vector->y, &out_vector->z);
    return result != -1;
}

b8 string_to_vec2(const char* str, vec2* out_vector) {
    if (!str || !out_vector) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec2));
    i32 result = sscanf(str, "%f %f", &out_vector->x, &out_vector->y);
    return result != -1;
}

b8 string_to_f32(const char* str, f32* f) {
    if (!str || !f) {
        return false;
    }

    *f = 0;
    i32 result = sscanf(str, "%f", f);
    return result != -1;
}

b8 string_to_f64(const char* str, f64* f) {
    if (!str || !f) {
        return false;
    }

    *f = 0;
    i32 result = sscanf(str, "%lf", f);
    return result != -1;
}

b8 string_to_i8(const char* str, i8* i) {
    if (!str || !i) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%hhi", i);
    return result != -1;
}

b8 string_to_i16(const char* str, i16* i) {
    if (!str || !i) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%hi", i);
    return result != -1;
}

b8 string_to_i32(const char* str, i32* i) {
    if (!str || !i) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%i", i);
    return result != -1;
}

b8 string_to_i64(const char* str, i64* i) {
    if (!str || !i) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%lli", i);
    return result != -1;
}

b8 string_to_u8(const char* str, u8* u) {
    if (!str || !u) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%hhu", u);
    return result != -1;
}

b8 string_to_u16(const char* str, u16* u) {
    if (!str || !u) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%hu", u);
    return result != -1;
}

b8 string_to_u32(const char* str, u32* u) {
    if (!str || !u) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%u", u);
    return result != -1;
}

b8 string_to_u64(const char* str, u64* u) {
    if (!str || !u) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%llu", u);
    return result != -1;
}

b8 string_to_bool(const char* str, b8* b) {
    if (!str || !b) {
        return false;
    }

    *b = strings_equal(str, "1") || strings_equali(str, "true");
    return *b;
}

u32 string_split(const char* str, char delimiter, char*** str_darray, b8 trim_entries, b8 include_empty) {
    if (!str || !str_darray) {
        return 0;
    }

    char* result = 0;
    u32 trimmed_length = 0;
    u32 entry_count = 0;
    u32 length = string_length(str);
    char buffer[16384] = {0};  // If a single entry goes beyond this, well... just don't do that.
    u32 current_length = 0;
    // Iterate each character until a delimiter is reached.
    for (u32 i = 0; i < length; ++i) {
        char c = str[i];

        // Found delimiter, finalize string.
        if (c == delimiter) {
            buffer[current_length] = 0;
            result = buffer;
            trimmed_length = current_length;
            // Trim if applicable
            if (trim_entries && current_length > 0) {
                result = string_trim(result);
                trimmed_length = string_length(result);
            }
            // Add new entry
            if (trimmed_length > 0 || include_empty) {
                char* entry = kallocate(sizeof(char) * (trimmed_length + 1), MEMORY_TAG_STRING);
                if (trimmed_length == 0) {
                    entry[0] = 0;
                } else {
                    string_ncopy(entry, result, trimmed_length);
                    entry[trimmed_length] = 0;
                }
                char** a = *str_darray;
                darray_push(a, entry);
                *str_darray = a;
                entry_count++;
            }

            // Clear the buffer.
            kzero_memory(buffer, sizeof(char) * 16384);
            current_length = 0;
            continue;
        }

        buffer[current_length] = c;
        current_length++;
    }

    // At the end of the string. If any chars are queued up, read them.
    result = buffer;
    trimmed_length = current_length;
    // Trim if applicable
    if (trim_entries && current_length > 0) {
        result = string_trim(result);
        trimmed_length = string_length(result);
    }
    // Add new entry
    if (trimmed_length > 0 || include_empty) {
        char* entry = kallocate(sizeof(char) * (trimmed_length + 1), MEMORY_TAG_STRING);
        if (trimmed_length == 0) {
            entry[0] = 0;
        } else {
            string_ncopy(entry, result, trimmed_length);
            entry[trimmed_length] = 0;
        }
        char** a = *str_darray;
        darray_push(a, entry);
        *str_darray = a;
        entry_count++;
    }

    return entry_count;
}

void string_cleanup_split_array(char** str_darray) {
    if (str_darray) {
        u32 count = darray_length(str_darray);
        // Free each string.
        for (u32 i = 0; i < count; ++i) {
            u32 len = string_length(str_darray[i]);
            kfree(str_darray[i], sizeof(char) * (len + 1), MEMORY_TAG_STRING);
        }

        // Clear the darray
        darray_clear(str_darray);
    }
}

void string_append_string(char* dest, const char* src, const char* append) {
    sprintf(dest, "%s%s", src, append);
}

void string_append_int(char* dest, const char* source, i64 i) {
    sprintf(dest, "%s%lli", source, i);
}

void string_append_float(char* dest, const char* source, f32 f) {
    sprintf(dest, "%s%f", source, f);
}

void string_append_bool(char* dest, const char* source, b8 b) {
    sprintf(dest, "%s%s", source, b ? "true" : "false");
}

void string_append_char(char* dest, const char* source, char c) {
    sprintf(dest, "%s%c", source, c);
}

void string_directory_from_path(char* dest, const char* path) {
    u64 length = strlen(path);
    for (i32 i = length; i >= 0; --i) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            strncpy(dest, path, i + 1);
            return;
        }
    }
}

void string_filename_from_path(char* dest, const char* path) {
    u64 length = strlen(path);
    for (i32 i = length; i >= 0; --i) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            strcpy(dest, path + i + 1);
            return;
        }
    }
}

void string_filename_no_extension_from_path(char* dest, const char* path) {
    u64 length = strlen(path);
    u64 start = 0;
    u64 end = 0;
    for (i32 i = length; i >= 0; --i) {
        char c = path[i];
        if (end == 0 && c == '.') {
            end = i;
        }
        if (start == 0 && (c == '/' || c == '\\')) {
            start = i + 1;
            break;
        }
    }

    string_mid(dest, path, start, end - start);
}

// ----------------------
// kstring implementation
// ----------------------

/**
 * @brief
 *
 * @param string
 * @param length The string length not including the null terminator.
 */
void kstring_ensure_allocated(kstring* string, u32 length) {
    if (string) {
        if (string->allocated < length + 1) {
            char* new_data = kallocate(sizeof(char) * length + 1, MEMORY_TAG_STRING);
            if (string->data) {
                // Copy over data if there is data to copy.
                if (string->length > 0) {
                    string_ncopy(new_data, string->data, string->length);
                }
                // Clean up old data
                kfree(string->data, sizeof(char) * string->length + 1, MEMORY_TAG_STRING);
            }

            string->data = new_data;
            string->length = length;
            string->allocated = length + 1;
        }
    }
}

void kstring_create(kstring* out_string) {
    if (!out_string) {
        KERROR("kstring_create requires a valid pointer to a string.");
        return;
    }

    kzero_memory(out_string, sizeof(kstring));

    kstring_ensure_allocated(out_string, 0);
    out_string->data[0] = 0;  // Null terminator.
}

void kstring_from_cstring(const char* source, kstring* out_string) {
    if (!out_string) {
        KERROR("kstring_from_cstring requires a valid pointer to a string.");
        return;
    }

    u32 source_length = string_length(source);
    kzero_memory(out_string, sizeof(kstring));

    kstring_ensure_allocated(out_string, source_length);

    string_ncopy(out_string->data, source, source_length);
    out_string->data[source_length] = 0;
}

void kstring_destroy(kstring* string) {
    if (string) {
        kfree(string->data, sizeof(char) * string->allocated, MEMORY_TAG_STRING);
        kzero_memory(string, sizeof(kstring));
    }
}

u32 kstring_length(const kstring* string) {
    return string ? string->length : 0;
}

u32 kstring_utf8_length(const kstring* string) {
    return string ? string_utf8_length(string->data) : 0;
}

void kstring_append_str(kstring* string, const char* s) {
    if (string && s) {
        u32 length = string_length(s);
        kstring_ensure_allocated(string, string->length + length);
        string_ncopy(string->data + string->length, s, length);
        string->data[string->length + length] = 0;
        string->length = string->length + length;
    }
}

void kstring_append_kstring(kstring* string, const kstring* other) {
    if (string && other) {
        kstring_ensure_allocated(string, string->length + other->length);
        string_ncopy(string->data + string->length, other->data, other->length);
        string->data[string->length + other->length] = 0;
        string->length = string->length + other->length;
    }
}
