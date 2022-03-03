#include "core/kstring.h"
#include "core/kmemory.h"
#include "containers/darray.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>  // isspace

#ifndef _MSC_VER
#include <strings.h>
#endif

u64 string_length(const char* str) {
    return strlen(str);
}

char* string_duplicate(const char* str) {
    u64 length = string_length(str);
    char* copy = kallocate(length + 1, MEMORY_TAG_STRING);
    kcopy_memory(copy, str, length + 1);
    return copy;
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
    u64 src_length = string_length(source);
    if (start >= src_length) {
        dest[0] = 0;
        return;
    }
    if (length > 0) {
        for (u64 i = start, j = 0; j < length && source[i]; ++i, ++j) {
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

i32 string_index_of(char* str, char c) {
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

b8 string_to_vec4(char* str, vec4* out_vector) {
    if (!str) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec4));
    i32 result = sscanf(str, "%f %f %f %f", &out_vector->x, &out_vector->y, &out_vector->z, &out_vector->w);
    return result != -1;
}

b8 string_to_vec3(char* str, vec3* out_vector) {
    if (!str) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec3));
    i32 result = sscanf(str, "%f %f %f", &out_vector->x, &out_vector->y, &out_vector->z);
    return result != -1;
}

b8 string_to_vec2(char* str, vec2* out_vector) {
    if (!str) {
        return false;
    }

    kzero_memory(out_vector, sizeof(vec2));
    i32 result = sscanf(str, "%f %f", &out_vector->x, &out_vector->y);
    return result != -1;
}

b8 string_to_f32(char* str, f32* f) {
    if (!str) {
        return false;
    }

    *f = 0;
    i32 result = sscanf(str, "%f", f);
    return result != -1;
}

b8 string_to_f64(char* str, f64* f) {
    if (!str) {
        return false;
    }

    *f = 0;
    i32 result = sscanf(str, "%lf", f);
    return result != -1;
}

b8 string_to_i8(char* str, i8* i) {
    if (!str) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%hhi", i);
    return result != -1;
}

b8 string_to_i16(char* str, i16* i) {
    if (!str) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%hi", i);
    return result != -1;
}

b8 string_to_i32(char* str, i32* i) {
    if (!str) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%i", i);
    return result != -1;
}

b8 string_to_i64(char* str, i64* i) {
    if (!str) {
        return false;
    }

    *i = 0;
    i32 result = sscanf(str, "%lli", i);
    return result != -1;
}

b8 string_to_u8(char* str, u8* u) {
    if (!str) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%hhu", u);
    return result != -1;
}

b8 string_to_u16(char* str, u16* u) {
    if (!str) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%hu", u);
    return result != -1;
}

b8 string_to_u32(char* str, u32* u) {
    if (!str) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%u", u);
    return result != -1;
}

b8 string_to_u64(char* str, u64* u) {
    if (!str) {
        return false;
    }

    *u = 0;
    i32 result = sscanf(str, "%llu", u);
    return result != -1;
}

b8 string_to_bool(char* str, b8* b) {
    if (!str) {
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
    char buffer[16384];  // If a single entry goes beyond this, well... just don't do that.
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