#pragma once
/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * Copyright (c) 2021-26 bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */

// This header is an implementation header shared between the implementations
// and the final memcpy/memmove/memset wrappers. Don't use this as the
// interface.

#ifndef MWORD_SIZE

#ifdef __aarch64__
// MWORD_SIZE 16 isn't improving much for unaligned access
// and performs worse when data is 8-byte aligned
#define MWORD_SIZE 8
#define MMIN_ALIGN 8
#endif

#ifdef __i386__
#define MWORD_SIZE 4
#define MMIN_ALIGN 4
#endif

#ifdef __x86_64__
// MWORD_SIZE 16 isn't improving much for unaligned access
// and performs worse when data is 8-byte aligned
#define MWORD_SIZE 8
#define MMIN_ALIGN 8
#endif

#endif /* MWORD_SIZE */

#if !defined(MWORD_SIZE) || !defined(MMIN_ALIGN)
#  error "Unknown CPU: define MWORD_SIZE and MMIN_ALIGN for it"
#endif

/* Ensure that MWORD_SIZE and MMIN_ALIGN are compatible.
   This assumption is at the heart of the code.
 */
#if MWORD_SIZE % MMIN_ALIGN != 0
#  error "MWORD_SIZE must be a multiple of MMIN_ALIGN"
#endif

#if defined(__has_attribute)
#  if __has_attribute(may_alias)
#    define LSSC_MAY_ALIAS __attribute__((may_alias))
#  endif
#  if __has_attribute(unused)
#    define LSSC_UNUSED __attribute__((unused))
#  endif
#elif defined(__GNUC__) // pre-gcc 5 / clang 3
#  define LSSC_MAY_ALIAS __attribute__((may_alias))
#  define LSSC_UNUSED __attribute__((unused))
#endif
#ifndef LSSC_MAY_ALIAS
#  define LSSC_MAY_ALIAS
#endif
#ifndef LSSC_UNUSED
#  define LSSC_UNUSED
#endif

#if MWORD_SIZE == 1
typedef uint8_t     LSSC_MAY_ALIAS MWORD_t;
#elif MWORD_SIZE == 2
typedef uint16_t    LSSC_MAY_ALIAS MWORD_t;
#elif MWORD_SIZE == 4
typedef uint32_t    LSSC_MAY_ALIAS MWORD_t;
#elif MWORD_SIZE == 8
typedef uint64_t    LSSC_MAY_ALIAS MWORD_t;
#elif MWORD_SIZE == 16
typedef __uint128_t LSSC_MAY_ALIAS MWORD_t;
#else
#  error "Unsupported MWORD_SIZE"
#endif

#define MWORD_BITS (MWORD_SIZE * CHAR_BIT)

/* Ensure that the mapping from size to type works as expected. If it doesn't,
   you may have to add a new conditional for your platform.
 */
static char assert_correct_MWORD_t_size[1 - 2 * (sizeof(MWORD_t) != MWORD_SIZE)] LSSC_UNUSED;
