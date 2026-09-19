/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * (L)ittle-endian
 * (F)oward
 *
 * Copyright (c) 2021-26 bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "lssc.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("no-strict-aliasing")
#endif

/* lssc_lf copies src to dest using a load-shift-store sequence of aligned
 * read and write operations. It assumes that the data bytes are stored in
 * little-endian order and that the CPU is a little-endian machine.
 * 
 * Data is read and written forward (i.e. from start to end of the buffers).
 * 
 * This call is not thread-safe because it may write back unmodified data to
 * byte locations not part of the dest buffer. It also assumes that MMU pages
 * are MWORD_t aligned (or else it may produce unexpected page faults).
 */
void lssc_lf(void *dest, const void *src, size_t n)
{
    /* NOTE: this code is "mirroring" lssc_lr.
     * All shift operations and all pointer updates are reversed.
     */

    if (dest == src || 0 == n)
        return;

    size_t h= sizeof(MWORD_t);
    size_t i= (uintptr_t)src  & (h-1);
    size_t j= (uintptr_t)dest & (h-1);
    size_t w= 0;

    MWORD_t *s= (MWORD_t*)((uintptr_t)src  & ~(uintptr_t)(h-1));
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));
    MWORD_t b= 0, m= (MWORD_t)~b;            /* no __uint128_t literals in gcc */

    /* If either src or dest is not MWORD_t aligned, we need to */
    /* merge each destination word from multiple source words.  */

    if (i || j)
    {
        size_t t= h * CHAR_BIT, u= i * CHAR_BIT, v= j * CHAR_BIT;
        MWORD_t a= *s++ >> u;       /* Read (partial) first word and discard */
                                    /* low bits that are not part of src.    */
        b= *d & (MWORD_t)~(m << v); /* Preserve low margin of dest. */

        if (n < h - j)              /* This would break n -= h - j below and */
        {                           /* means all dest data fits into (*d).   */
            if (h - i < n)          /* If src data is split across two words,*/
                a |= *s << (t - u); /* fetch and append second slice.        */

            size_t k= v + n * CHAR_BIT;
            a<<= v;                 /* Shift data into their dest place.*/
            a&= (MWORD_t)~(m << k); /* Discard bits that are not part of src.*/
            b|= *d & (m << k);      /* Restore high margin.*/
            *d= b | a;              /* Write back. */
            return;
        }

        a<<= u;                     /* Shift data back to their src postion. */
        if (n < h - i)              /* All src data were in (*s)! */
        {
            size_t k= u + n * CHAR_BIT;
            a&= (MWORD_t)~(m << k); /* Discard bits not part of src.*/
        }

        if (i != j)                 /* If src and dest alignment differ, we  */
        {                           /* need a load-shift-store loop.         */
            w= v - u;               /* safe: C99 6.2.5p9 and w+=t below      */
            if (v < u)              /* If there are more leading bits in dest*/
            {                       /* reduce this to u < v by pretending    */
                b|= a >> (u - v);   /* the bits in a were from dest (v+=t-u),*/
                w+= t;              /* so w' = (v+(t-u))-0 = t+(v-u) = w+t.  */
                a= *s++;            /* Restart at next word and pretend u==0.*/
            } 
            *d++= b | (MWORD_t)(a << w); /* Write back combined word.*/
            b= a >> (t - w);        /* Shift excess bits into b.*/
            n-= h - j;              /* j < h by definition */

            while (h <= n)
            {                       /* While we can emit full words, repeat. */
                a= *s++;            /* Load */
                *d++= b | (MWORD_t)(a << w); /* Shift & Store */
                b= a >> (t - w);
                n-= h;
            }
        }
        else
        {                           /* Optimise when we don't need to shift. */
            *d++= b | a;            /* b has no high margin; just write back.*/
            b= 0;
            n-= h - j;              /* j < h by definition */

            /* Fall through to share code with !i && !j case.*/
        }
    }

    /* This may be unrolled into a large chunk of assembly, so better share.*/
    while (h <= n)
        *d++= *s++, n-= h;

    if (n)
    {                               /* Some data left to be written.*/
        size_t k= n * CHAR_BIT;
        if (w < k)                  /* If we haven't read all data yet, */
            b |= (*s << w);         /* load them from the last src word.*/
        MWORD_t a= *d;              /* Load last dest word.*/
        b&= (MWORD_t)~(m << k);     /* Clear extra src bits.*/
        a&=  (m << k);              /* Clear dest bits to be replaced.*/
        *d= b | a;                  /* Merge and store.*/
    }
}

void init_lf(void *dest, int ch, size_t n)
{
    if (0 == n)
        return;

    size_t h= sizeof(MWORD_t);
    size_t j= (uintptr_t)dest & (h-1);
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));
    MWORD_t s= (unsigned char)ch;
    for (int t= CHAR_BIT; t < CHAR_BIT * MWORD_SIZE; t+= t)
        s|= s << t;

    MWORD_t b= 0, m= (MWORD_t)~b;            /* no __uint128_t literals in gcc */

    /* If either src or dest is not MWORD_t aligned, we need to */
    /* merge each destination word from multiple source words.  */

    if (j)
    {
        size_t uv= j * CHAR_BIT;
        MWORD_t a= s >> uv;
        b= *d & (MWORD_t)~(m << uv);

        if (n < h - j)
        {
            size_t k= uv + n * CHAR_BIT;
            a<<= uv;
            *d= b | (*d & (m << k)) | (a & ~(m << k));
            return;
        }

        *d++= b | (MWORD_t)(a << uv), n-= h - j;
    }

    while (h <= n)
        *d++= s, n-= h;

    if (n)
    {
        size_t k= n * CHAR_BIT;
        *d= (s & (MWORD_t)~(m << k)) | (*d & (MWORD_t)(m << k));
    }
}

void zero_lf(void *dest, size_t n)
{
    if (0 == n)
        return;

    size_t h= sizeof(MWORD_t);
    size_t j= (uintptr_t)dest & (h-1);
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));
    MWORD_t b= 0, m= (MWORD_t)~b;            /* no __uint128_t literals in gcc */

    /* If either src or dest is not MWORD_t aligned, we need to */
    /* merge each destination word from multiple source words.  */

    if (j)
    {
        size_t uv= j * CHAR_BIT;
        b= *d & (MWORD_t)~(m << uv);

        if (n < h - j)
        {
            *d= b | (*d & (m << (uv + n * CHAR_BIT)));
            return;
        }

        *d++= b, n-= h - j;
    }

    while (h <= n)
        *d++= 0, n-= h;

    if (n)
        *d &= m << (n * CHAR_BIT);
}
