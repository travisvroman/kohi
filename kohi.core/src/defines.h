/**
 * @file defines.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains global type definitions which are used
 * throughout the entire engine and applications referencing it.
 * Numeric types are asserted statically to gurantee expected size.
 * @version 2.0
 * @date 2024-04-03
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#define KNULL 0

// Unsigned int types.

/** @brief Unsigned 8-bit integer */
typedef unsigned char u8;

/** @brief Unsigned 16-bit integer */
typedef unsigned short u16;

/** @brief Unsigned 32-bit integer */
typedef unsigned int u32;

/** @brief Unsigned 64-bit integer */
typedef unsigned long long u64;

// Signed int types.

/** @brief Signed 8-bit integer */
typedef signed char i8;

/** @brief Signed 16-bit integer */
typedef signed short i16;

/** @brief Signed 32-bit integer */
typedef signed int i32;

/** @brief Signed 64-bit integer */
typedef signed long long i64;

// Floating point types

/** @brief 32-bit floating point number */
typedef float f32;

/** @brief 64-bit floating point number */
typedef double f64;

// Boolean types

/** @brief 32-bit boolean type, used for APIs which require it */
typedef int b32;

/** @brief 8-bit boolean type */
typedef _Bool b8;

/** @brief A range, typically of memory */
typedef struct krange {
    /** @brief The offset in bytes. */
    u64 offset;
    /** @brief The size in bytes. */
    u64 size;
} krange;

/** @brief A range, typically of memory */
typedef struct range32 {
    /** @brief The offset in bytes. */
    i32 offset;
    /** @brief The size in bytes. */
    i32 size;
} range32;
// Properly define static assertions.
#if defined(__clang__) || defined(__GNUC__)
/** @brief Static assertion */
#    define STATIC_ASSERT _Static_assert
#else

/** @brief Static assertion */
#    define STATIC_ASSERT static_assert
#endif

// Ensure all types are of the correct size.

/** @brief Assert u8 to be 1 byte.*/
STATIC_ASSERT(sizeof(u8) == 1, "Expected u8 to be 1 byte.");

/** @brief Assert u16 to be 2 bytes.*/
STATIC_ASSERT(sizeof(u16) == 2, "Expected u16 to be 2 bytes.");

/** @brief Assert u32 to be 4 bytes.*/
STATIC_ASSERT(sizeof(u32) == 4, "Expected u32 to be 4 bytes.");

/** @brief Assert u64 to be 8 bytes.*/
STATIC_ASSERT(sizeof(u64) == 8, "Expected u64 to be 8 bytes.");

/** @brief Assert i8 to be 1 byte.*/
STATIC_ASSERT(sizeof(i8) == 1, "Expected i8 to be 1 byte.");

/** @brief Assert i16 to be 2 bytes.*/
STATIC_ASSERT(sizeof(i16) == 2, "Expected i16 to be 2 bytes.");

/** @brief Assert i32 to be 4 bytes.*/
STATIC_ASSERT(sizeof(i32) == 4, "Expected i32 to be 4 bytes.");

/** @brief Assert i64 to be 8 bytes.*/
STATIC_ASSERT(sizeof(i64) == 8, "Expected i64 to be 8 bytes.");

/** @brief Assert f32 to be 4 bytes.*/
STATIC_ASSERT(sizeof(f32) == 4, "Expected f32 to be 4 bytes.");

/** @brief Assert f64 to be 8 bytes.*/
STATIC_ASSERT(sizeof(f64) == 8, "Expected f64 to be 8 bytes.");

/** @brief True.*/
#define true 1

/** @brief False. */
#define false 0

#define U64_MAX 18446744073709551615UL
#define U32_MAX 4294967295U
#define U16_MAX 65535U
#define U8_MAX 255U
#define U64_MIN 0UL
#define U32_MIN 0U
#define U16_MIN 0U
#define U8_MIN 0U

#define I8_MAX 127
#define I16_MAX 32767
#define I32_MAX 2147483647
#define I64_MAX 9223372036854775807L
#define I8_MIN (-I8_MAX - 1)
#define I16_MIN (-I16_MAX - 1)
#define I32_MIN (-I32_MAX - 1)
#define I64_MIN (-I64_MAX - 1)

/**
 * @brief Any id set to this should be considered invalid,
 * and not actually pointing to a real object.
 */
#define INVALID_ID_U64 U64_MAX
#define INVALID_ID_U32 U32_MAX
#define INVALID_ID_U16 U16_MAX
#define INVALID_ID_U8 U8_MAX
#define INVALID_ID INVALID_ID_U32

// Platform detection
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#    define KPLATFORM_WINDOWS 1
#    ifndef _WIN64
#        error "64-bit is required on Windows!"
#    endif
#elif defined(__linux__) || defined(__gnu_linux__)
// Linux OS
#    define KPLATFORM_LINUX 1
#    if defined(__ANDROID__)
#        define KPLATFORM_ANDROID 1
#    endif
#elif defined(__unix__)
// Catch anything not caught by the above.
#    define KPLATFORM_UNIX 1
#elif defined(_POSIX_VERSION)
// Posix
#    define KPLATFORM_POSIX 1
#elif __APPLE__
// Apple platforms
#    define KPLATFORM_APPLE 1
#    include <TargetConditionals.h>
#    if TARGET_IPHONE_SIMULATOR
// iOS Simulator
#        define KPLATFORM_IOS 1
#        define KPLATFORM_IOS_SIMULATOR 1
#    elif TARGET_OS_IPHONE
#        define KPLATFORM_IOS 1
// iOS device
#    elif TARGET_OS_MAC
// HACK: Should probably be in the Vulkan Renderer lib, not here.
#        define VK_USE_PLATFORM_MACOS_MVK
// Other kinds of Mac OS
#    else
#        error "Unknown Apple platform"
#    endif
#else
#    error "Unknown platform!"
#endif

#ifdef KEXPORT
// Exports
#    ifdef _MSC_VER
#        define KAPI __declspec(dllexport)
#    else
#        define KAPI __attribute__((visibility("default")))
#    endif
#else
// Imports
#    ifdef _MSC_VER
/** @brief Import/export qualifier */
#        define KAPI __declspec(dllimport)
#    else
/** @brief Import/export qualifier */
#        define KAPI
#    endif
#endif

#if _DEBUG
#    define KOHI_DEBUG 1
#    define KOHI_RELEASE 0
#else
#    define KOHI_RELEASE 1
#    define KOHI_DEBUG 0
#endif

// Feature build flags.

#if KOHI_DEBUG
#    define KOHI_HOT_RELOAD 1
#else
#    define KOHI_HOT_RELOAD 0
#endif

/**
 * @brief Clamps value to a range of min and max (inclusive).
 * @param value The value to be clamped.
 * @param min The minimum value of the range.
 * @param max The maximum value of the range.
 * @returns The clamped value.
 */
#define KCLAMP(value, min, max) ((value <= min) ? min : (value >= max) ? max \
                                                                       : value)

// Inlining
#if defined(__clang__) || defined(__gcc__)
/** @brief Inline qualifier */
#    define KINLINE __attribute__((always_inline)) inline

/** @brief No-inline qualifier */
#    define KNOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)

/** @brief Inline qualifier */
#    define KINLINE __forceinline

/** @brief No-inline qualifier */
#    define KNOINLINE __declspec(noinline)
#else

/** @brief Inline qualifier */
#    define KINLINE static inline

/** @brief No-inline qualifier */
#    define KNOINLINE
#endif

// Deprecation
#if defined(__clang__) || defined(__gcc__)
/** @brief Mark something (i.e. a function) as deprecated. */
#    define KDEPRECATED(message) __attribute__((deprecated(message)))
#elif defined(_MSC_VER)
/** @brief Mark something (i.e. a function) as deprecated. */
#    define KDEPRECATED(message) __declspec(deprecated(message))
#else
#    error "Unsupported compiler - don't know how to define deprecations!"
#endif

/** @brief Gets the number of bytes from amount of gibibytes (GiB) (1024*1024*1024) */
#define GIBIBYTES(amount) ((amount) * 1024ULL * 1024ULL * 1024ULL)
/** @brief Gets the number of bytes from amount of mebibytes (MiB) (1024*1024) */
#define MEBIBYTES(amount) ((amount) * 1024ULL * 1024ULL)
/** @brief Gets the number of bytes from amount of kibibytes (KiB) (1024) */
#define KIBIBYTES(amount) ((amount) * 1024ULL)

/** @brief Gets the number of bytes from amount of gigabytes (GB) (1000*1000*1000) */
#define GIGABYTES(amount) ((amount) * 1000ULL * 1000ULL * 1000ULL)
/** @brief Gets the number of bytes from amount of megabytes (MB) (1000*1000) */
#define MEGABYTES(amount) ((amount) * 1000ULL * 1000ULL)
/** @brief Gets the number of bytes from amount of kilobytes (KB) (1000) */
#define KILOBYTES(amount) ((amount) * 1000ULL)

KINLINE u64 get_aligned(u64 operand, u64 granularity) {
    return ((operand + (granularity - 1)) & ~(granularity - 1));
}

KINLINE krange get_aligned_range(u64 offset, u64 size, u64 granularity) {
    return (krange){get_aligned(offset, granularity), get_aligned(size, granularity)};
}

#define KSWAP(type, a, b) \
    {                     \
        type temp = a;    \
        a = b;            \
        b = temp;         \
    }

#define KMIN(x, y) (x < y ? x : y)
#define KMAX(x, y) (x > y ? x : y)

/**
 * @brief Indicates if the provided flag is set in the given flags int.
 */
#define FLAG_GET(flags, flag) ((flags & flag) == flag)

/**
 * @brief Sets a flag within the flags int to enabled/disabled.
 *
 * @param flags The flags int to write to.
 * @param flag The flag to set.
 * @param enabled Indicates if the flag is enabled or not.
 */
#define FLAG_SET(flags, flag, enabled) (flags = (enabled ? (flags | flag) : (flags & ~flag)))

/**
 * Unpacks 4 u16s from a single u64.
 * @param packed The packed u64.
 * @param a A pointer to a u16 to hold the first value.
 * @param b A pointer to a u16 to hold the second value.
 * @param c A pointer to a u16 to hold the third value.
 * @param d A pointer to a u16 to hold the fourth value.
 */
#define UNPACK_U64_U16S(packed, a, b, c, d) \
    {                                       \
        a = (u16)(packed >> 48);            \
        b = (u16)(packed >> 32);            \
        c = (u16)(packed >> 16);            \
        d = (u16)(packed >> 0);             \
    }

/**
 * Unpacks a single u16 at the given index (0-3).
 */
#define UNPACK_U64_U16_AT(packed, index) ((u16)(packed >> ((3 - index) * 16)))

/**
 * @brief Packs 4 u16s into a single u64
 */
#define PACK_U64_U16S(a, b, c, d) (((u64)a << 48) | ((u64)b << 32) | ((u64)c << 16) | ((u64)d))

/**
 * @brief Packs n into target. Target can be an already-packed u64.
 * @param target The u64 to be packed into.
 * @param n The u16 to be packed.
 * @param index The index to pack at (0-3).
 */
#define PACK_U64_U16_AT(target, n, index) (target = (target | ((u64)n << ((3 - index) * 16))))

/**
 * Unpacks 2 u32s from a single u64.
 * @param packed The packed u64.
 * @param a A pointer to a u32 to hold the first value.
 * @param b A pointer to a u32 to hold the second value.
 */
#define UNPACK_U64_U32S(packed, a, b) \
    {                                 \
        a = (u32)(packed >> 32);      \
        b = (u32)(packed >> 0);       \
    }

/**
 * Unpacks a single u32 at the given index (0-1).
 */
#define UNPACK_U64_U32_AT(packed, index) ((u32)(packed >> ((1 - index) * 32)))

/**
 * @brief Packs 2 u32s into a single u64
 */
#define PACK_U64_U32S(a, b) (((u64)a << 32) | ((u64)b))

/**
 * @brief Packs n into target. Target can be an already-packed u64.
 * @param target The u64 to be packed into.
 * @param n The u32 to be packed.
 * @param index The index to pack at (0-1).
 */
#define PACK_U64_U32_AT(target, n, index) (target = (target | ((u64)n << ((1 - index) * 32))))
