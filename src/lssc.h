#pragma once
/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * Copyright (c) 2021-26 bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */

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

#if MWORD_SIZE == 1
#  define MWORD_t uint8_t
#elif MWORD_SIZE == 2
#  define MWORD_t uint16_t
#elif MWORD_SIZE == 4
#  define MWORD_t uint32_t
#elif MWORD_SIZE == 8
#  define MWORD_t uint64_t
#elif MWORD_SIZE == 16
#  define MWORD_t __uint128_t
#else
#  error "Unsupported MWORD_SIZE"
#endif

#define MWORD_BITS (MWORD_SIZE * CHAR_BIT)

/* Ensure that the mapping from size to type works as expected. If it doesn't,
   you may have to add a new conditional for your platform.
 */
static char __MWORD_t_check[1 - 2 * (sizeof(MWORD_t) != MWORD_SIZE)] __attribute__((unused));
