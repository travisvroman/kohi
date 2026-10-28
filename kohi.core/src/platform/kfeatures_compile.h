/**
 * @file kfeatures_compile.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#if defined(__x86_64__) || defined(_M_X64)
#	define KCPU_ARCH_X86_64 1
#elif defined(__aarch64__)
#	define KCPU_ARCH_XARM64 1
#endif

#if defined(__SSE__)
#	define KCOMPILETIME_SSE 1
#endif

#if defined(__SSE2__)
#	define KCOMPILETIME_SSE2 1
#endif

#if defined(__AVX__)
#	define KCOMPILETIME_AVX 1
#endif

#if defined(__AVX2__)
#	define KCOMPILETIME_AVX2 1
#endif

#if defined(__NEON__)
#	define KCOMPILETIME_NEON 1
#endif
