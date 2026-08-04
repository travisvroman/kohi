#include "strings/kstring.h"
#include "debug/kassert.h"
#include "math/kmath.h"
#include "strings/kname.h"
#include "strings/kstring_id.h"

#include <ctype.h>
#include <stdarg.h> // For variadic functions
#include <stdint.h>
#include <stdio.h> // vsnprintf, sscanf, sprintf

#define USE_STD_STR 1
#if USE_STD_STR
#	include <string.h>
#endif

#include "containers/darray.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"

u64 string_length (const char *str) {
	if (!str) {
		return 0;
	}
#if USE_STD_STR
	return strlen(str);
#else
	u32 length = string_nlength(str, U32_MAX);
	if (length == U32_MAX) {
		KWARN("string_length is returning U32_MAX. Is it possible the string has no null terminator?")
	}
	return length;
#endif
}

u32 string_utf8_length (const char *str) {
	u32 length = string_utf8_nlength(str, U32_MAX);
	if (length == U32_MAX) {
		KWARN("kstring string_utf8_length is returning U32_MAX. Is it possible the string has no null terminator?")
	}
	return length;
}

u64 string_nlength (const char *str, u32 max_len) {
	if (!str) {
		return 0;
	}
	u32 length = 0;
	for (; length < max_len; ++length) {
		if (!str[length]) {
			break;
		}
	}
	if (length == U32_MAX) {
		KWARN("string_nlength is returning U32_MAX. Is it possible the string has no null terminator?")
	}
	return length;
}

u32 string_utf8_nlength (const char *str, u32 max_len) {
	u32 length = 0;
	for (u64 i = 0; length < max_len; ++i, ++length) {
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
			KERROR("kstring string_utf8_nlength() - Not supporting 5 and 6-byte characters; Invalid UTF-8.");
			return 0;
		}
	}

	if (length == U32_MAX) {
		KWARN("kstring string_utf8_nlength is returning U32_MAX. Is it possible the string has no null terminator?")
	}

	return length;
}

b8 bytes_to_codepoint (const char *bytes, u32 offset, i32 *out_codepoint, u8 *out_advance) {
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

b8 char_is_whitespace (char c) {
	// Source of whitespace characters:
	switch (c) {
	case 0x0009: //  character tabulation (\t)
	case 0x000A: // line feed (\n)
	case 0x000B: // line tabulation/vertical tab (\v)
	case 0x000C: // form feed (\f)
	case 0x000D: // carriage return (\r)
	case 0x0020: // space (' ')
		return true;
	default:
		return false;
	}
}

b8 codepoint_is_whitespace (i32 codepoint) {
	// Source of whitespace characters:
	switch (codepoint) {
	case 0x0009: //  character tabulation (\t)
	case 0x000A: // line feed (\n)
	case 0x000B: // line tabulation/vertical tab (\v)
	case 0x000C: // form feed (\f)
	case 0x000D: // carriage return (\r)
	case 0x0020: // space (' ')
	case 0x0085: // next line
	case 0x00A0: // no-break space
	case 0x1680: // ogham space mark
	case 0x180E: // mongolian vowel separator
	case 0x2000: // en quad
	case 0x2001: // em quad
	case 0x2002: // en space
	case 0x2003: // em space
	case 0x2004: // three-per-em space
	case 0x2005: // four-per-em space
	case 0x2006: // six-per-em space
	case 0x2007: // figure space
	case 0x2008: // punctuation space
	case 0x2009: // thin space
	case 0x200A: // hair space
	case 0x200B: // zero width space
	case 0x200C: // zero width non-joiner
	case 0x200D: // zero width joiner
	case 0x2028: // line separator
	case 0x2029: // paragraph separator
	case 0x202F: // narrow no-break space
	case 0x205F: // medium mathematical space
	case 0x2060: // word joiner
	case 0x3000: // ideographic space
	case 0xFEFF: // zero width non-breaking space
		return true;
	default:
		return false;
	}
}

char *string_duplicate (const char *str) {
	if (!str) {
		KWARN("string_duplicate called with an empty string. 0/null will be returned.");
		return 0;
	}
	u64 length = string_length(str);
	char *copy = kallocate(length + 1, MEMORY_TAG_STRING);
	kcopy_memory(copy, str, length);
	copy[length] = 0;
	return copy;
}

void string_free (const char *str) {
	if (str) {
		kfree((char *)str);
	}
}

i64 kstr_ncmp (const char *str0, const char *str1, u32 max_len) {
	if (!str0 && !str1) {
		return 0; // Technically equal since both are null.
	} else if (!str0 && str1) {
		// Count the first string as 0 and compare against the second, non-empty string.
		return 0 - str1[0];
	} else if (str0 && !str1) {
		// Count the second string as 0. In this case, just return the value of the
		// first char of the first string as str[0] - 0 would just be str[0] anyway.
		return str0[0];
	}
#if USE_STD_STR
	return strncmp(str0, str1, max_len);
#else
	if (!str0 && !str1) {
		return 0; // Technically equal since both are null.
	} else if (!str0 && str1) {
		// Count the first string as 0 and compare against the second, non-empty string.
		return 0 - str1[0];
	} else if (str0 && !str1) {
		// Count the second string as 0. In this case, just return the value of the
		// first char of the first string as str[0] - 0 would just be str[0] anyway.
		return str0[0];
	}

	// Get string lengths, excluding null terminators.
	u32 length_0 = string_nlength(str0, max_len);
	u32 length_1 = string_nlength(str1, max_len);

	// Can only loop through the smallest string's length.
	// Ensure a max of U32_MAX also.
	u32 min_length = KMIN(KMIN(max_len, U32_MAX), KMIN(length_0, length_1));
	// LEFTOFF: This causes failures in some cases but blocks successes in others.
	// Ideally this should always take the null terminator into account in the comparison.

	u32 count = min_length;
	if (min_length < U32_MAX) {
		count += 1;
	}
	for (u32 i = 0; i < count; ++i) {
		if ((!str0[i] || !str1[i]) && i == max_len) {
			return 0;
		}
		i64 result = str0[i] - str1[i];
		if (result) {
			return result;
		}
	}

	// If at the end and no differences were found, should be safe to say they are the same.
	return 0;
#endif
}

i64 kstr_ncmpi (const char *str0, const char *str1, u32 max_len) {
	char *lower_0 = 0;
	char *lower_1 = 0;
	// Lowercase both strings and use them for comparison.
	if (str0) {
		lower_0 = string_duplicate(str0);
		string_to_lower(lower_0);
	}
	if (str1) {
		lower_1 = string_duplicate(str1);
		string_to_lower(lower_1);
	}
	i64 result = kstr_ncmp(lower_0, lower_1, max_len);
	if (lower_0) {
		string_free(lower_0);
	}
	if (lower_1) {
		string_free(lower_1);
	}
	return result;
}

// Case-sensitive string comparison. True if the same, otherwise false.
b8 strings_equal (const char *str0, const char *str1) {
	return kstr_ncmp(str0, str1, U32_MAX) == 0;
}

// Case-insensitive string comparison. True if the same, otherwise false.
b8 strings_equali (const char *str0, const char *str1) {
	return kstr_ncmpi(str0, str1, U32_MAX) == 0;
}

b8 strings_nequal (const char *str0, const char *str1, u32 max_len) {
	return kstr_ncmp(str0, str1, max_len) == 0;
}

b8 strings_nequali (const char *str0, const char *str1, u32 max_len) {
	return kstr_ncmpi(str0, str1, max_len) == 0;
}

char *string_format (const char *format, ...) {
	if (!format) {
		return 0;
	}

	__builtin_va_list arg_ptr;
	va_start(arg_ptr, format);
	char *result = string_format_v(format, arg_ptr);
	va_end(arg_ptr);
	return result;
}

static i32 append (char *buf, u32 size, u32 *pos, const char *text) {
	u32 length = string_length(text);
	u32 space = (buf && size > *pos) ? size - *pos : 0;

	if (buf && space > 1) {
		u32 to_copy = (length < space - 1) ? length : space - 1;
		kcopy_memory(buf + *pos, text, to_copy);
		buf[*pos + to_copy] = 0;
	}

	*pos += length;
	return (i32)length;
}

static i32 appendf (char *buf, u32 size, u32 *pos, const char *fmt, ...) {
	char tmp[16384];
	va_list ap;
	va_start(ap, fmt);
	i32 len = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);

	append(buf, size, pos, tmp);
	return len;
}

static const char *parse_standard_printf_format (const char *p, char *out, u32 out_size) {
	if (*p != '%') {
		return KNULL;
	}

	char *s = out;
	const char *end = out + out_size - 1;

	*s++ = *p++;

	// Flags
	while (strchr("#0-+ ", *p)) {
		if (s < end) {
			*s++ = *p;
			p++;
		}
	}

	// Width
	if (*p == '*') {
		if (s < end) {
			*s++ = *p++;
		}
	} else {
		while (isdigit((u8)*p)) {
			if (s < end) {
				*s++ = *p;
				p++;
			}
		}
	}

	// Precision
	if (*p == '.') {
		if (s < end) {
			*s++ = *p++;
		}
		if (*p == '*') {
			if (s < end) {
				*s++ = *p++;
			}
		} else {
			while (isdigit((u8)*p)) {
				if (s < end) {
					*s++ = *p;
					p++;
				}
			}
		}
	}

	// Length modifiers
	if (strchr("hljztL", *p)) {
		if (s < end) {
			*s++ = *p++;
		}
		if ((p[-1] == 'h' && *p == 'h') || (p[-1] == 'l' && *p == 'l')) {
			if (s < end) {
				*s++ = *p++;
			}
		}
	}

	// Conversion specifier
	if (isalpha((u8)*p) || *p == '%') {
		if (s < end) {
			*s++ = *p++;
		}
	} else {
		return KNULL;
	}

	*s = 0;
	return p;
}

static void va_advance_one (va_list *ap, const char *spec) {
	u32 len = string_length(spec);
	if (!len) {
		return;
	}

	char conv = spec[len - 1];

	// Does it have a length modifier?
	b8 h = strstr(spec, "h") != KNULL;
	b8 hh = strstr(spec, "hh") != KNULL;
	b8 l = strstr(spec, "l") != KNULL;
	b8 ll = strstr(spec, "ll") != KNULL;
	b8 L = strstr(spec, "L") != KNULL;
	b8 z = strstr(spec, "z") != KNULL;
	b8 t = strstr(spec, "t") != KNULL;
	b8 j = strstr(spec, "j") != KNULL;

	switch (conv) {
	case 'd':
	case 'i':
		if (hh) {
			(void)va_arg(*ap, i32);
		} else if (h) {
			(void)va_arg(*ap, i32);
		} else if (l) {
			(void)va_arg(*ap, i64);
		} else if (ll) {
			(void)va_arg(*ap, i64);
		} else if (j) {
			(void)va_arg(*ap, intmax_t);
		} else if (z) {
			(void)va_arg(*ap, u64);
		} else if (t) {
			(void)va_arg(*ap, u64);
		} else {
			(void)va_arg(*ap, i32);
		}
		break;

	case 'u':
	case 'o':
	case 'x':
	case 'X':
		if (hh) {
			(void)va_arg(*ap, i32);
		} else if (h) {
			(void)va_arg(*ap, i32);
		} else if (l) {
			(void)va_arg(*ap, i64);
		} else if (ll) {
			(void)va_arg(*ap, i64);
		} else if (j) {
			(void)va_arg(*ap, intmax_t);
		} else if (z) {
			(void)va_arg(*ap, u64);
		} else {
			(void)va_arg(*ap, i32);
		}
		break;

	case 'f':
	case 'F':
	case 'e':
	case 'E':
	case 'g':
	case 'G':
	case 'a':
	case 'A':
		if (L) {
			(void)va_arg(*ap, f64);
		} else {
			(void)va_arg(*ap, f64);
		}
		break;

	case 'c':
		(void)va_arg(*ap, i32);
		break;
	case 's':
		(void)va_arg(*ap, const char *);
		break;
	case 'p':
		(void)va_arg(*ap, void *);
		break;
	case 'n':
		(void)va_arg(*ap, i32 *);
		break;
	case '%':
		// No arguments here
		break;

	default:
		break;
	}
}

/**
 * Custom extension of printf which includes logic to print some custom data types using new specifiers.
 */
i32 vsnprintf_extended (char *buf, u32 size, const char *fmt, va_list ap_input) {
	u32 pos = 0;

	if (buf && size > 0) {
		buf[0] = 0;
	}

	va_list ap;
	va_copy(ap, ap_input);

	const char *p = fmt;

	while (*p) {
		// Regular characters
		if (*p != '%') {
			char tmp[2] = {*p++, 0};
			append(buf, size, &pos, tmp);
			continue;
		}

		// '%' literal
		if (p[1] == '%') {
			append(buf, size, &pos, "%");
			p += 2;
			continue;
		}

		// Custom vector formats (%V2, %V3, %V4 with .precision D)
		if (p[1] == 'V') {
			i32 dims = p[2] - '0'; // Should be either 2, 3 or 4
			if (dims >= 2 && dims <= 4) {
				const char *t = p + 3;

				b8 convert_deg = false;
				u8 precision = 6;

				// Check for optional 'D', specifying to convert radians to degrees.
				if (*t == 'D') {
					convert_deg = true;
					t++;
				}

				// Check for .N (precision)
				if (*t == '.' && t[1] >= '0' && t[1] <= '9') {
					precision = (u8)(t[1] - '0');
					t += 2;
				}

				if (precision > 8) {
					precision = 8;
				}

				const f32 *v = va_arg(ap, const f32 *);

				f32 vals[4] = {0, 0, 0, 0};
				for (u8 i = 0; i < dims; ++i) {
					f32 x = v[i];
					if (convert_deg) {
						x = rad_to_deg(x);
					}
					vals[i] = x;
				}

				// Build a format string.
				char fbuf[64];
				if (dims == 2) {
					snprintf(fbuf, sizeof(fbuf), "[%%.%df, %%.%df]", precision, precision);
				} else if (dims == 3) {
					snprintf(fbuf, sizeof(fbuf), "[%%.%df, %%.%df, %%.%df]", precision, precision, precision);
				} else if (dims == 4) {
					snprintf(fbuf, sizeof(fbuf), "[%%.%df, %%.%df, %%.%df, %%.%df]", precision, precision, precision, precision);
				}

				// Append
				if (dims == 2) {
					appendf(buf, size, &pos, fbuf, vals[0], vals[1]);
				} else if (dims == 3) {
					appendf(buf, size, &pos, fbuf, vals[0], vals[1], vals[2]);
				} else if (dims == 4) {
					appendf(buf, size, &pos, fbuf, vals[0], vals[1], vals[2], vals[3]);
				}

				// Move the pointer forward.
				p = t;
				continue;
			}
		}

		// 'k' for kname.
		if (p[1] == 'k') {
			// knames are u64s, so take that as the arg.
			u64 knraw = va_arg(ap, u64);
			const char *str = kname_string_get(knraw);
			if (!str) {
				str = "(null_kname)";
			}
			append(buf, size, &pos, str);
			p += 2;
			continue;
		}

		// 'K' for kstring.
		if (p[1] == 'K') {
			// kstrings are length-based, so need to get the cstring.
			kstring ksraw = va_arg(ap, kstring);
			const char *str = kstring_cstr(ksraw);
			if (!str) {
				str = "(null_kstring)";
			}
			append(buf, size, &pos, str);
			p += 2;
			continue;
		}

		// Standard printf formats
		char spec[64];
		const char *next = parse_standard_printf_format(p, spec, sizeof(spec));
		if (!next) {
			// This means input was malformed, just treat as a '%' literal.
			append(buf, size, &pos, "%");
			p++;
			continue;
		}

		// Extract the formatted result using a copy of the arg list.
		va_list tmp;
		va_copy(tmp, ap);

		char sm[16384];
		vsnprintf(sm, sizeof(sm), spec, tmp);
		va_end(tmp);

		append(buf, size, &pos, sm);

		// Advance the real list.
		va_advance_one(&ap, spec);

		p = next;
		continue;
	}

	va_end(ap);

	// Ensure null termination.
	if (buf && size > 0) {
		if (pos >= size) {
			buf[size - 1] = 0;
		} else {
			buf[pos] = 0;
		}
	}

	return (i32)pos;
}

char *string_format_v (const char *format, va_list va_listp) {
	if (!format) {
		return 0;
	}

	// Create a copy of the va_listp since vsnprintf can invalidate the elements of the list
	// while finding the required buffer length.
	va_list list_copy;
	va_copy(list_copy, va_listp);

	i32 length = vsnprintf_extended(0, 0, format, list_copy);
	char *buffer = kallocate(length + 1, MEMORY_TAG_STRING);
	if (!buffer) {
		return 0;
	}
	length = vsnprintf_extended(buffer, length + 1, format, list_copy);

	va_end(list_copy);

	buffer[length] = 0;
	return buffer;
}

// TODO: remove unsafe/deprecated
i32 string_format_unsafe (char *dest, const char *format, ...) {
	if (dest) {
		__builtin_va_list arg_ptr;
		va_start(arg_ptr, format);
		i32 written = string_format_v_unsafe(dest, format, arg_ptr);
		va_end(arg_ptr);
		return written;
	}
	return -1;
}

// TODO: remove unsafe/deprecated
i32 string_format_v_unsafe (char *dest, const char *format, void *va_listp) {
	if (dest) {
		// Big, but can fit on the stack.
		char buffer[32000] = {0};
		i32 written = vsnprintf(buffer, 32000, format, va_listp);
		buffer[written] = 0;
		kcopy_memory(dest, buffer, written + 1);

		return written;
	}
	return -1;
}

i32 string_nformat (char *dest, u32 max_len, const char *format, ...) {
	__builtin_va_list arg_ptr;
	va_start(arg_ptr, format);
	i32 written = string_nformat_v(dest, max_len, format, arg_ptr);
	va_end(arg_ptr);
	return written;
}

i32 string_nformat_v (char *dest, u32 max_len, const char *format, void *va_listp) {
	if (dest) {
		return vsnprintf(dest, max_len, format, va_listp);
	}
	return -1;
}

char *string_empty (char *str) {
	if (str) {
		str[0] = 0;
	}

	return str;
}

char *string_copy (char *dest, const char *source) {
	return string_ncopy(dest, source, U32_MAX);
}

char *string_ncopy (char *dest, const char *source, u32 max_len) {
	if (!dest) {
		KERROR("%s called without dest, which is required. 0/null will be returned.", __FUNCTION__);
		return KNULL;
	}
	if (!source) {
		KERROR("%s called without source, which is required. Unmodified dest will be returned.", __FUNCTION__);
		return dest;
	}
	// zero length is technically valid, return unmodified dest.
	if (!max_len) {
		return dest;
	}

	// Make sure to account for null terminator.
	u32 source_length = KMIN(string_length(source) + 1, max_len);
	kcopy_memory(dest, source, source_length);

	// Don't zero pad if max_len is U32_MAX.
	if (max_len != U32_MAX) {
		i64 diff = max_len - source_length;
		if (diff > 0) {
			kset_memory(dest + source_length, 0, diff);
		}
	}

	return dest;
}

char *string_trim (char *str) {
	while (codepoint_is_space(*str)) {
		str++;
	}
	if (*str) {
		char *p = str;
		while (*p) {
			p++;
		}
		while (codepoint_is_space(*(--p)))
			;

		p[1] = '\0';
	}

	return str;
}

void string_mid (char *dest, const char *source, i32 start, i32 length) {
	if (!source || !dest) {
		return;
	}
	if (length == 0) {
		KTRACE("Tried to perform mid on zero-length string.");
		dest[0] = 0;
		return;
	}
	i32 src_length = (i32)string_length(source);
	if (start >= src_length) {
		dest[0] = 0;
		return;
	}
	if (length > 0) {
		i32 j = 0;
		for (i32 i = start; j < length && source[i]; ++i, ++j) {
			dest[j] = source[i];
		}
		dest[j] = 0;
	} else {
		// If a negative value is passed, proceed to the end of the string.
		u64 j = 0;
		for (u64 i = start; source[i]; ++i, ++j) {
			dest[j] = source[i];
		}
		dest[j] = 0;
	}
}

i32 string_index_of (const char *str, char c) {
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

i32 string_last_index_of (const char *str, char c) {
	if (!str) {
		return -1;
	}
	u32 length = string_length(str);
	if (length > 0) {
		for (u32 i = length - 1; i > 0; --i) {
			if (str[i] == c) {
				return i;
			}
		}
	}

	return -1;
}

i32 string_index_of_str (const char *str_0, const char *str_1) {
	if (!str_0 || !str_1) {
		return -1;
	}
	u32 length_0 = string_length(str_0);
	u32 length_1 = string_length(str_1);
	const char *a = str_0;
	const char *b = str_1;
	if (length_1 > length_0) {
		u32 temp = length_0;
		length_0 = length_1;
		length_1 = temp;
		a = str_1;
		b = str_0;
	}
	if (length_0 > 0 && length_1 > 0) {
		for (u32 i = 0; i < length_0; ++i) {
			if (a[i] == b[0]) {
				u32 start = i;
				b8 keep_looking = false;
				for (u32 j = 0; j < length_1; ++j) {
					if (a[i + j] != b[j]) {
						keep_looking = true;
						break;
					}
				}
				if (!keep_looking) {
					return start;
				}
			}
		}
	}

	return -1;
}

i32 string_index_of_stri (const char *str_0, const char *str_1) {
	if (!str_0 || !str_1) {
		return -1;
	}
	u32 length_0 = string_length(str_0);
	u32 length_1 = string_length(str_1);
	const char *a = str_0;
	const char *b = str_1;
	if (length_1 > length_0) {
		u32 temp = length_0;
		length_0 = length_1;
		length_1 = temp;
		a = str_1;
		b = str_0;
	}
	if (length_0 > 0 && length_1 > 0) {
		for (u32 i = 0; i < length_0; ++i) {
			if (kstr_ncmpi(a + i, b, 1) == 0) {
				u32 start = i;
				b8 keep_looking = false;
				for (u32 j = 0; j < length_1; ++j) {
					if (kstr_ncmpi(a + (i + j), b + j, 1) != 0) {
						keep_looking = true;
						break;
					}
				}
				if (!keep_looking) {
					return start;
				}
			}
		}
	}

	return -1;
}

b8 string_starts_with (const char *str_0, const char *str_1) {
	if (!str_0 || !str_1) {
		return false;
	}
	u32 length_0 = string_length(str_0);
	u32 length_1 = string_length(str_1);
	if (length_0 < length_1) {
		return false;
	}

	return strings_nequal(str_0, str_1, length_1);
}

b8 string_starts_withi (const char *str_0, const char *str_1) {
	if (!str_0 || !str_1) {
		return false;
	}
	u32 length_0 = string_length(str_0);
	u32 length_1 = string_length(str_1);
	if (length_0 < length_1) {
		return false;
	}

	return strings_nequali(str_0, str_1, length_1);
}

void string_insert_char_at (char *dest, const char *src, u32 pos, char c) {
	u32 len = string_length(src);
	u32 remaining = len - pos;
	if (pos > 0) {
		kcopy_memory(dest, src, sizeof(char) * pos);
	}

	if (pos < len) {
		kcopy_memory(dest + pos + 1, src + pos, sizeof(char) * remaining);
	}
	dest[pos] = c;
}

void string_insert_str_at (char *dest, const char *src, u32 pos, const char *str) {
	u32 len = string_length(src);
	u32 ins_len = string_length(str);
	u32 remaining = len - pos;
	if (pos > 0) {
		kcopy_memory(dest, src, sizeof(char) * pos);
	}

	if (pos < len) {
		kcopy_memory(dest + pos + ins_len, src + pos, sizeof(char) * remaining);
	}

	kcopy_memory(dest + pos, str, sizeof(char) * ins_len);
}

void string_remove_at (char *dest, const char *src, u32 pos, u32 length) {
	u32 original_length = string_length(src);
	u32 remaining = original_length - pos - length;
	if (pos > 0) {
		kcopy_memory(dest, src, sizeof(char) * pos);
	}

	if (pos < original_length) {
		kcopy_memory(dest + pos, src + pos + length, sizeof(char) * remaining);
	}
	dest[original_length - length] = 0;
}

i32 string_replace_char (char *str, char find, char replace) {
	if (!str) {
		return -1;
	}

	i32 len = string_length(str);
	for (i32 i = 0; i < len; ++i) {
		if (str[i] == find) {
			str[i] = replace;
			return i;
		}
	}

	return -1;
}

u32 string_replace_char_all (char *str, char find, char replace) {
	i32 result = 0;
	u32 count = 0;
	while (result != -1) {
		result = string_replace_char(str, find, replace);
		count++;
	}

	return count;
}

b8 string_to_mat4 (const char *str, mat4 *out_mat) {
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

const char *mat4_to_string (mat4 m) {
	f32 *d = m.data;
	return string_format("%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
						 d[0],
						 d[1],
						 d[2],
						 d[3],
						 d[4],
						 d[5],
						 d[6],
						 d[7],
						 d[8],
						 d[9],
						 d[10],
						 d[11],
						 d[12],
						 d[13],
						 d[14],
						 d[15]);
}

b8 string_to_rect_2di (const char *str, rect_2di *rect) {
	if (!str || !rect) {
		return false;
	}

	kzero_memory(rect, sizeof(rect_2di));
	i32 result = sscanf(str, "%d %d %d %d", &rect->x, &rect->y, &rect->z, &rect->w);
	return result != -1;
}

const char *rect_2di_to_string (rect_2di rect) {
	return string_format("%d %d %d %d", rect.x, rect.y, rect.z, rect.w);
}

b8 string_to_vec4 (const char *str, vec4 *out_vector) {
	if (!str || !out_vector) {
		return false;
	}

	kzero_memory(out_vector, sizeof(vec4));
	i32 result = sscanf(str, "%f %f %f %f", &out_vector->x, &out_vector->y, &out_vector->z, &out_vector->w);
	return result != -1;
}

const char *vec4_to_string (vec4 v) {
	return string_format("%f %f %f %f", v.x, v.y, v.z, v.w);
}

b8 string_to_vec3 (const char *str, vec3 *out_vector) {
	if (!str || !out_vector) {
		return false;
	}

	kzero_memory(out_vector, sizeof(vec3));
	i32 result = sscanf(str, "%f %f %f", &out_vector->x, &out_vector->y, &out_vector->z);
	return result != -1;
}

const char *vec3_to_string (vec3 v) {
	return string_format("%f %f %f", v.x, v.y, v.z);
}

b8 string_to_vec2 (const char *str, vec2 *out_vector) {
	if (!str || !out_vector) {
		return false;
	}

	kzero_memory(out_vector, sizeof(vec2));
	i32 result = sscanf(str, "%f %f", &out_vector->x, &out_vector->y);
	return result != -1;
}

const char *vec2_to_string (vec2 v) {
	return string_format("%f %f", v.x, v.y);
}

b8 string_to_f32 (const char *str, f32 *f) {
	if (!str || !f) {
		return false;
	}

	*f = 0;
	i32 result = sscanf(str, "%f", f);
	return result != -1;
}

const char *f32_to_string (f32 f) {
	return string_format("%f", f);
}

b8 string_to_f64 (const char *str, f64 *f) {
	if (!str || !f) {
		return false;
	}

	*f = 0;
	i32 result = sscanf(str, "%lf", f);
	return result != -1;
}

const char *f64_to_string (f64 f) {
	return string_format("%f", f);
}

b8 string_to_i8 (const char *str, i8 *i) {
	if (!str || !i) {
		return false;
	}

	*i = 0;
	i32 result = sscanf(str, "%hhi", i);
	return result != -1;
}

const char *i8_to_string (i8 i) {
	return string_format("%hhi", i);
}

b8 string_to_i16 (const char *str, i16 *i) {
	if (!str || !i) {
		return false;
	}

	*i = 0;
	i32 result = sscanf(str, "%hi", i);
	return result != -1;
}

const char *i16_to_string (i16 i) {
	return string_format("%hi", i);
}

b8 string_to_i32 (const char *str, i32 *i) {
	if (!str || !i) {
		return false;
	}

	*i = 0;
	i32 result = sscanf(str, "%i", i);
	return result != -1;
}

const char *i32_to_string (i32 i) {
	return string_format("%i", i);
}

b8 string_to_i64 (const char *str, i64 *i) {
	if (!str || !i) {
		return false;
	}

	*i = 0;
	i32 result = sscanf(str, "%lli", i);
	return result != -1;
}

const char *i64_to_string (i64 i) {
	return string_format("%lli", i);
}

b8 string_to_u8 (const char *str, u8 *u) {
	if (!str || !u) {
		return false;
	}

	*u = 0;
	i32 result = sscanf(str, "%hhu", u);
	return result != -1;
}

const char *u8_to_string (u8 u) {
	return string_format("%hhu", u);
}

b8 string_to_u16 (const char *str, u16 *u) {
	if (!str || !u) {
		return false;
	}

	*u = 0;
	i32 result = sscanf(str, "%hu", u);
	return result != -1;
}

const char *u16_to_string (u16 u) {
	return string_format("%hu", u);
}

b8 string_to_u32 (const char *str, u32 *u) {
	if (!str || !u) {
		return false;
	}

	*u = 0;
	i32 result = sscanf(str, "%u", u);
	return result != -1;
}

const char *u32_to_string (u32 u) {
	return string_format("%u", u);
}

b8 string_to_u64 (const char *str, u64 *u) {
	if (!str || !u) {
		return false;
	}

	*u = 0;
	i32 result = sscanf(str, "%llu", u);
	return result != -1;
}

const char *u64_to_string (u64 u) {
	return string_format("%llu", u);
}

b8 string_to_bool (const char *str, b8 *b) {
	if (!str || !b) {
		return false;
	}

	*b = strings_equal(str, "1") || strings_equali(str, "true");
	return true;
}

const char *bool_to_string (b8 b) {
	return string_duplicate(b == false ? "false" : "true");
}

u32 string_split (const char *str, char delimiter, char ***str_darray, b8 trim_entries, b8 include_empty, b8 escape_strings) {
	if (!str || !str_darray) {
		return 0;
	}

	char *result = 0;
	u32 trimmed_length = 0;
	u32 entry_count = 0;
	u32 length = string_length(str);
	char buffer[16384] = {0}; // If a single entry goes beyond this, well... just don't do that.
	u32 current_length = 0;
	b8 in_string = false;
	char prev = 0;
	// Iterate each character until a delimiter is reached.
	for (u32 i = 0; i < length; ++i) {
		char c = str[i];

		// Found delimiter, finalize string.
		if (c == delimiter && (!escape_strings || !in_string)) {
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
				char *entry = kallocate(sizeof(char) * (trimmed_length + 1), MEMORY_TAG_STRING);
				if (trimmed_length == 0) {
					entry[0] = 0;
				} else {
					string_ncopy(entry, result, trimmed_length);
					entry[trimmed_length] = 0;
				}
				char **a = *str_darray;
				darray_push(a, &entry);
				*str_darray = a;
				entry_count++;
			}

			// Clear the buffer.
			kzero_memory(buffer, sizeof(char) * 16384);
			current_length = 0;
			prev = c;
			continue;
		}

		if (c == '\"' && prev != '\\') {
			in_string = !in_string;
		} else {
			buffer[current_length] = c;
			current_length++;
		}

		prev = c;
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
		char *entry = kallocate(sizeof(char) * (trimmed_length + 1), MEMORY_TAG_STRING);
		if (trimmed_length == 0) {
			entry[0] = 0;
		} else {
			string_ncopy(entry, result, trimmed_length);
			entry[trimmed_length] = 0;
		}
		char **a = *str_darray;
		darray_push(a, &entry);
		*str_darray = a;
		entry_count++;
	}

	return entry_count;
}

void string_cleanup_split_darray (char **str_darray) {
	if (str_darray) {
		u32 count = darray_length(str_darray);
		// Free each string.
		for (u32 i = 0; i < count; ++i) {
			kfree(str_darray[i]);
		}

		// Clear the darray
		darray_clear(str_darray);
	}
}

void string_cleanup_array (const char **str_array, u32 length) {
	if (str_array) {
		for (u32 i = 0; i < length; ++i) {
			if (str_array[i]) {
				string_free(str_array[i]);
			}
		}
		kfree(str_array);
	}
}

u32 string_nsplit (const char *str, char delimiter, u32 max_count, char **str_array, b8 trim_entries, b8 include_empty) {
	if (!str || !str_array) {
		return 0;
	}

	char *result = 0;
	u32 trimmed_length = 0;
	u32 entry_count = 0;
	u32 length = string_length(str);
	char buffer[16384] = {0}; // If a single entry goes beyond this, well... just don't do that.
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
				str_array[entry_count] = string_duplicate(result);
				entry_count++;
			}

			// Ensure this doesn't go beyond the allowed max count.
			if (entry_count == max_count) {
				return entry_count;
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
		trimmed_length = string_length(result);
		entry_count++;
	}

	return entry_count;
}

void string_cleanup_split_array (char **str_array, u32 max_count) {
	if (str_array) {
		// Free each string.
		for (u32 i = 0; i < max_count; ++i) {
			string_free(str_array[i]);
		}

		// Zero the array
		kzero_memory(str_array, sizeof(char *) * max_count);
	}
}

void string_append_string (char *dest, const char *src, const char *append) {
	sprintf(dest, "%s%s", src, append);
}

void string_append_int (char *dest, const char *source, i64 i) {
	sprintf(dest, "%s%lli", source, i);
}

void string_append_float (char *dest, const char *source, f32 f) {
	sprintf(dest, "%s%f", source, f);
}

void string_append_bool (char *dest, const char *source, b8 b) {
	sprintf(dest, "%s%s", source, b ? "true" : "false");
}

void string_append_char (char *dest, const char *source, char c) {
	sprintf(dest, "%s%c", source, c);
}

char *string_join (const char **strings, u32 count, char delimiter) {
	if (!strings || !count) {
		return 0;
	}
	if (delimiter == 0) {
		KERROR("string_join cannot be used with a null terminator character as the delimiter.");
		return 0;
	}

	u32 total_length = 0;
	u32 *lengths = KALLOC_TYPE_CARRAY(u32, count);
	for (u32 i = 0; i < count; ++i) {
		lengths[i] = string_length(strings[i]);
		total_length += lengths[i];
	}

	// Space for delimiters
	total_length += (count - 1);

	char *out_str = KALLOC_TYPE_CARRAY(char, total_length);
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		sprintf(out_str + offset, "%s%c", strings[i], delimiter);
		offset += lengths[i] + 1;
	}

	// Overwrite the final delimiter character with null terminator.
	out_str[total_length - 1] = 0;

	kfree(lengths);

	return out_str;
}

char *kstring_id_join (const kstring_id *strings, u32 count, char delimiter) {
	if (!strings || !count) {
		return 0;
	}
	if (delimiter == 0) {
		KERROR("string_join cannot be used with a null terminator character as the delimiter.");
		return 0;
	}

	u32 total_length = 0;
	u32 *lengths = KALLOC_TYPE_CARRAY(u32, count);
	for (u32 i = 0; i < count; ++i) {
		const char *str = kstring_id_string_get(strings[i]);
		lengths[i] = string_length(str);
		total_length += lengths[i];
	}

	// Space for delimiters
	total_length += count;

	char *out_str = kallocate(sizeof(char) * total_length, MEMORY_TAG_STRING);
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		sprintf(out_str + offset, "%s%c", kstring_id_string_get(strings[i]), delimiter);
		offset += lengths[i] + 1;
	}

	// Null-terminate the string
	out_str[total_length - 1] = 0;

	kfree(lengths);

	return out_str;
}

char *kname_join (const kname *strings, u32 count, char delimiter) {
	if (!strings || !count) {
		return 0;
	}
	if (delimiter == 0) {
		KERROR("string_join cannot be used with a null terminator character as the delimiter.");
		return 0;
	}

	u32 total_length = 0;
	u32 *lengths = KALLOC_TYPE_CARRAY(u32, count);
	for (u32 i = 0; i < count; ++i) {
		const char *str = kname_string_get(strings[i]);
		lengths[i] = string_length(str);
		total_length += lengths[i];
	}

	// Space for delimiters
	total_length += (count - 1);

	char *out_str = KALLOC_TYPE_CARRAY(char, total_length);
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		sprintf(out_str + offset, "%s%c", kname_string_get(strings[i]), delimiter);
		offset += lengths[i] + 1;
	}

	// Overwrite the final delimiter character with null terminator.
	out_str[total_length - 1] = 0;

	kfree(lengths);

	return out_str;
}

const char *string_directory_from_path (const char *path) {
	u64 length = string_length(path);
	for (i32 i = length; i >= 0; --i) {
		char c = path[i];
		if (c == '/' || c == '\\') {
			u32 new_length = i + 1; // Account for null.
			char *dest = kallocate(new_length, MEMORY_TAG_STRING);
			string_ncopy(dest, path, i);
			dest[i] = 0;
			return dest;
		}
	}

	return 0;
}

const char *string_filename_from_path (const char *path) {
	u64 length = string_length(path);
	for (i32 i = length, j = 0; i >= 0; --i, ++j) {
		char c = path[i];
		if (c == '/' || c == '\\') {
			u32 new_length = j + 1; // Account for null.
			char *dest = kallocate(new_length, MEMORY_TAG_STRING);
			string_ncopy(dest, path + i, j);
			dest[j] = 0;
			return dest;
		}
	}

	return 0;
}

const char *string_filename_no_extension_from_path (const char *path) {
	u64 length = string_length(path);
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

	u32 new_length = end - start;
	char *dest = kallocate(new_length + 1, MEMORY_TAG_STRING);
	dest[new_length] = 0;
	string_ncopy(dest, path + start, new_length);
	return dest;
}

const char *string_extension_from_path (const char *path, b8 include_dot) {
	if (!path) {
		return 0;
	}

	i32 start = string_last_index_of(path, '.');
	if (start == -1) {
		return 0;
	}
	if (!include_dot) {
		start++;
	}

	i32 length = string_length(path) - start;
	char *out_str = kallocate(sizeof(char) * (length + 1), MEMORY_TAG_STRING);
	string_mid(out_str, path, start, length);
	return out_str;
}

b8 string_parse_array_length (const char *str, u32 *out_length) {
	if (!str || !out_length) {
		return false;
	}

	i32 open_index = string_index_of(str, '[');
	i32 close_index = string_index_of(str, ']');
	if (open_index == -1 || close_index == -1) {
		return false;
	}

	// Extract text from between the brackets.
	char num_string[20] = {0};
	string_mid(num_string, str, open_index + 1, close_index - open_index);

	return string_to_u32(num_string, out_length);
}

b8 string_line_get (const char *source_str, u16 max_line_length, u32 start_from, char **out_buffer, u32 *out_line_length, u8 *out_addl_advance) {
	if (!source_str || !max_line_length || !out_line_length || !out_buffer) {
		return false;
	}
	if (!source_str[start_from]) {
		return false;
	}

	*out_addl_advance = 0;

	u32 length = 0;
	for (u32 c = start_from; source_str[c] && length < max_line_length; c++, ++length) {
		if (length == max_line_length - 1) {
			// TODO: remove debug
			KTRACE("hitting max length");
			*out_addl_advance = 0;
		}
		if (source_str[c] == '\r' && source_str[c + 1] != '\n') {
			KTRACE("rogue \\r!");
		}

		if (source_str[c] == '\n' || source_str[c] == '\r') {
			if (source_str[c] == '\r' && source_str[c + 1] == '\n') {
				*out_addl_advance = 2;
			} else {
				*out_addl_advance = 1;
			}
			*out_line_length = length;
			(*out_buffer)[length] = 0;
			return true;
		} else {
			(*out_buffer)[length] = source_str[c];
		}
	}

	*out_line_length = length;
	(*out_buffer)[length] = 0;
	return true;
}

b8 codepoint_is_lower (i32 codepoint) {
	return (codepoint >= 'a' && codepoint <= 'z') ||
		   (codepoint >= 0xE0 && codepoint <= 0xFF);
}

b8 codepoint_is_upper (i32 codepoint) {
	return (codepoint <= 'Z' && codepoint >= 'A') ||
		   (codepoint >= 0xC0 && codepoint <= 0xDF);
}

b8 codepoint_is_alpha (i32 codepoint) {
	return ((codepoint >= 'a' && codepoint <= 'z') ||
			(codepoint >= 'A' && codepoint <= 'Z') ||
			(codepoint >= 0xC0 && codepoint <= 0xFF));
}

b8 codepoint_is_numeric (i32 codepoint) {
	return (codepoint <= '9' && codepoint >= '0');
}

b8 codepoint_is_space (i32 codepoint) {
	switch (codepoint) {
	case ' ':  // regular space
	case '\n': // newline
	case '\r': // carriage return
	case '\f': // form feed
	case '\t': // horizontal tab
	case '\v': // vertical tab
		return true;
	default:
		return false;
	}
}

void string_to_lower (char *str) {
	for (u32 i = 0; str[i]; ++i) {
		if (codepoint_is_upper(str[i])) {
			str[i] += ('a' - 'A');
		}
	}
}

void string_to_upper (char *str) {
	for (u32 i = 0; str[i]; ++i) {
		if (codepoint_is_lower(str[i])) {
			str[i] += ('a' - 'A');
		}
	}
}

// ----------------------
// kstring implementation
// ----------------------

static void kstring_ensure_allocated (kstring *str, u32 byte_length) {
	if (byte_length < KSTRING_DEFAULT_BUF_SIZE) {
		return;
	}

	if (str->allocated < byte_length) {
		char *new_data = kallocate(sizeof(char) * byte_length, MEMORY_TAG_KSTRING);
		KASSERT(new_data);
		if (str->allocated) {
			kcopy_memory(new_data, str->data, str->allocated);
		}
		kfree(str->data);
		str->data = new_data;

		str->allocated = byte_length;
	}
}

kstring kstring_create (void) {
	kstring str;
	kzero_memory(&str, sizeof(kstring));
	str.data = str.base_buffer;
	return str;
}

kstring kstring_from_cstring (const char *source) {
	if (!source) {
		return kstring_create();
	}

	kstring str;
	kzero_memory(&str, sizeof(kstring));

	str.length = string_length(source);
	if (str.length <= KSTRING_DEFAULT_BUF_SIZE) {
		kcopy_memory(str.base_buffer, source, KSTRING_DEFAULT_BUF_SIZE);
	} else {
		kstring_ensure_allocated(&str, str.length);
		kcopy_memory(str.data, source, str.length);
	}

	return str;
}

kstring kstring_duplicate (kstring source) {
	kstring str;
	kzero_memory(&str, sizeof(kstring));

	str.length = source.length;
	if (str.length <= KSTRING_DEFAULT_BUF_SIZE) {
		kcopy_memory(str.base_buffer, source.base_buffer, KSTRING_DEFAULT_BUF_SIZE);
	} else {
		kstring_ensure_allocated(&str, str.length);
		kcopy_memory(str.data, source.data, str.length);
	}

	return str;
}

void kstring_destroy (kstring *string) {
	if (string) {
		if (string->allocated && string->data != string->base_buffer) {
			kfree(string->data);
		}
		kzero_memory(string, sizeof(kstring));
	}
}

char *kstring_cstr (kstring source) {
	char *out_str = KNULL;

	out_str = kallocate(source.length + 1, MEMORY_TAG_KSTRING);
	if (source.length) {
		kcopy_memory(out_str, source.data ? source.data : source.base_buffer, source.length);
	}
	out_str[source.length] = 0;

	return out_str;
}

b8 kstrings_equal (kstring a, kstring b) {
	if (a.length != b.length) {
		return false;
	}

	return strings_equal(a.data, b.data);
}

b8 kstrings_equali (kstring a, kstring b) {
	if (a.length != b.length) {
		return false;
	}

	return strings_equali(a.data, b.data);
}

b8 kstring_cstr_equal (kstring a, const char *b) {
	return strings_equal(a.data, b);
}

b8 kstring_cstr_equali (kstring a, const char *b) {
	return strings_equali(a.data, b);
}

u32 kstring_length (kstring string) {
	return string.length;
}

u32 kstring_utf8_length (kstring string) {
	// TODO: cache utf8 length?
	return string_utf8_length(string.data);
}

i32 kstring_index_of_char (kstring string, char c) {
	for (u32 i = 0; i < string.length; ++i) {
		if (string.data[i] == c) {
			return i;
		}
	}

	return -1;
}

i32 kstring_index_of_kstring (kstring string, kstring text) {
	u32 searchlen = text.length;

	i32 len = searchlen - string.length;
	for (i32 i = 0; i < len; ++i) {
		i32 j;
		for (j = 0; text.data[j]; ++j) {
			if (string.data[i + j] != text.data[j]) {
				break;
			}
		}

		if (!text.data[j]) {
			return i;
		}
	}

	return -1;
}

i32 kstring_index_of_cstr (kstring string, const char *text) {
	u32 searchlen = string_length(text);

	i32 len = searchlen - string.length;
	for (i32 i = 0; i < len; ++i) {
		i32 j;
		for (j = 0; text[j]; ++j) {
			if (string.data[i + j] != text[j]) {
				break;
			}
		}

		if (!text[j]) {
			return i;
		}
	}

	return -1;
}

kstring kstring_append_data (kstring string, const void *data, u32 length) {
	kstring out_str;
	kzero_memory(&out_str, sizeof(kstring));

	if (!length || !data) {
		return out_str;
	}

	u32 new_length = string.length + length;
	if (new_length > KSTRING_DEFAULT_BUF_SIZE) {
		kstring_ensure_allocated(&out_str, new_length);
	}

	kcopy_memory(out_str.data, string.base_buffer, string.length);
	kcopy_memory(out_str.data + string.length, data, length);
	out_str.length += length;

	return out_str;
}

kstring kstring_append_cstr (kstring string, const char *s) {
	u32 slen = string_length(s);

	return kstring_append_data(string, s, slen);
}

kstring kstring_append_kstring (kstring string, kstring other) {
	return kstring_append_data(string, other.data, other.length);
}

kstring kstring_append_char (kstring string, char c) {
	return kstring_append_data(string, &c, 1);
}

kstring kstring_append_bool (kstring string, b8 b) {
	return kstring_append_data(string, b ? "true" : "false", b ? 4 : 5);
}

static char *u64_to_cstring_internal (u64 value, char *out) {
	char temp[20];
	u32 digits = 0;

	if (value == 0) {
		out[0] = '0';
		out[1] = 0;
		return out;
	}

	while (value) {
		temp[digits++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (u32 i = 0; i < digits; ++i) {
		out[i] = temp[digits - i - 1];
	}

	out[digits] = 0;
	return out;
}

static char *i64_to_cstring_internal (i64 value, char *out) {
	if (value >= 0) {
		return u64_to_cstring_internal((u64)value, out);
	}

	out[0] = '-';
	out++;

	u64 mag = (u64)(-(value + 1)) + 1;
	u64_to_cstring_internal(mag, out);

	return out - 1;
}

static char *f64_to_cstring_internal (f64 value, char *out, u8 decimals) {
	if (value < 0.0) {
		out[0] = '-';
		out++;
		value = -value;
	}

	u64 whole = (u64)value;
	u64_to_cstring_internal(whole, out);

	while (*out) {
		++out;
	}

	if (decimals == 0) {
		return out;
	}

	*out++ = '.';

	f64 frac = value - (f64)whole;

	for (u8 i = 0; i < decimals; ++i) {
		frac *= 10.0;
		u32 digit = (u32)frac;
		*out++ = (char)('0' + digit);
		frac -= digit;
	}

	*out = 0;
	return out;
}

kstring kstring_append_i8 (kstring string, i8 i) {
	char buf[20];
	i64_to_cstring_internal(i, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_i16 (kstring string, i16 i) {
	char buf[20];
	i64_to_cstring_internal(i, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_i32 (kstring string, i32 i) {
	char buf[20];
	i64_to_cstring_internal(i, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_i64 (kstring string, i64 i) {
	char buf[20];
	i64_to_cstring_internal(i, buf);

	return kstring_from_cstring(buf);
}

kstring kstring_append_u8 (kstring string, u8 u) {
	char buf[20];
	u64_to_cstring_internal(u, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_u16 (kstring string, u16 u) {
	char buf[20];
	u64_to_cstring_internal(u, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_u32 (kstring string, u32 u) {
	char buf[20];
	u64_to_cstring_internal(u, buf);

	return kstring_from_cstring(buf);
}
kstring kstring_append_u64 (kstring string, u64 u) {
	char buf[20];
	u64_to_cstring_internal(u, buf);

	return kstring_from_cstring(buf);
}

kstring kstring_append_f32 (kstring string, f32 f, u8 decimal_places) {
	char buf[20];
	f64_to_cstring_internal(f, buf, decimal_places);

	return kstring_from_cstring(buf);
}

kstring kstring_append_f64 (kstring string, f64 f, u8 decimal_places) {
	char buf[20];
	f64_to_cstring_internal(f, buf, decimal_places);

	return kstring_from_cstring(buf);
}

static char *kstring_stringify_vector (f32 *elements, b8 element_count, b8 decimal_places, char *out) {
	char buf[64];
	u8 offset = 0;
	kzero_memory(buf, sizeof(buf));
	for (u8 i = 0; i < element_count; ++i) {
		f64_to_cstring_internal(elements[i], buf + offset, decimal_places);

		while (buf[offset]) {
			++offset;
		}

		if (i != element_count - 1) {
			buf[offset] = ' ';
			offset++;
		}
	}

	return out;
}

kstring kstring_append_vec2 (kstring string, vec2 v, u8 decimal_places) {
	char buf[64];
	char *vstr = kstring_stringify_vector(v.elements, 2, decimal_places, buf);
	return kstring_append_cstr(string, vstr);
}

kstring kstring_append_vec3 (kstring string, vec3 v, u8 decimal_places) {
	char buf[64];
	char *vstr = kstring_stringify_vector(v.elements, 3, decimal_places, buf);
	return kstring_append_cstr(string, vstr);
}

kstring kstring_append_vec4 (kstring string, vec4 v, u8 decimal_places) {
	char buf[64];
	char *vstr = kstring_stringify_vector(v.elements, 4, decimal_places, buf);
	return kstring_append_cstr(string, vstr);
}

kstring kstring_append_mat4 (kstring string, mat4 m, u8 decimal_places) {
	char buf[256];
	char *vstr = kstring_stringify_vector(m.data, 16, decimal_places, buf);
	return kstring_append_cstr(string, vstr);
}

kstring kstring_ltrim (kstring string) {
	kstring out_str;
	kzero_memory(&out_str, sizeof(kstring));

	for (u32 i = 0; i < string.length; ++i) {
		if (!char_is_whitespace(string.data[i])) {
			out_str.length = string.length - i;
			if (out_str.length > KSTRING_DEFAULT_BUF_SIZE) {
				out_str.data = kallocate(sizeof(char) * out_str.length, MEMORY_TAG_KSTRING);
				out_str.allocated = sizeof(char) * out_str.length;
			}
			kcopy_memory(out_str.data, string.data + i, out_str.length);
			break;
		}
	}

	// If at the end of the string, then the entire thing was whitespace.
	return kstring_create();
}

kstring kstring_rtrim (kstring string) {
	kstring out_str;
	kzero_memory(&out_str, sizeof(kstring));

	for (i32 i = string.length; i >= 0; --i) {
		if (!char_is_whitespace(string.data[i])) {
			out_str.length = i;
			if (out_str.length > KSTRING_DEFAULT_BUF_SIZE) {
				out_str.data = kallocate(sizeof(char) * out_str.length, MEMORY_TAG_KSTRING);
				out_str.allocated = sizeof(char) * out_str.length;
			}
			kcopy_memory(out_str.data, string.data, out_str.length);
			break;
		}
	}

	// If at the start of the string, then the entire thing was whitespace.
	return kstring_create();
}

kstring kstring_trim (kstring string) {
	kstring trimmed_l = kstring_ltrim(string);
	kstring trimmed_r = kstring_rtrim(trimmed_l);
	kstring_destroy(&trimmed_l);

	return trimmed_r;
}
kstring kstring_substr (kstring string, u32 start, u32 length) {
	kstring out_str = kstring_create();

	start = KMIN(start, string.length);
	u32 end = KMIN(start + length, string.length);

	// Actual length
	u32 len = end - start;
	if (len) {
		kstring_ensure_allocated(&out_str, len);
		kcopy_memory(out_str.data, string.data + start, len);
		out_str.length = len;
	}

	return out_str;
}

kstring kstring_format (const char *fmt, ...) {
	__builtin_va_list arg_ptr;
	va_start(arg_ptr, fmt);
	char *result = string_format_v(fmt, arg_ptr);
	va_end(arg_ptr);

	kstring out_str = kstring_from_cstring(result);
	kfree(result);
	return out_str;
}
