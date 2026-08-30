/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * (L)ittle-endian
 * (R)everse
 *
 * Copyright (c) 2021-26 Bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include "lssc.h"

/* lssc_lr copies src to dest using a load-shift-store sequence of aligned
 * read and write operations. It assumes that the data bytes are stored in
 * little-endian order and that the CPU is a little-endian machine.
 * 
 * Data is read and written in reverse (i.e. from end to start of the buffers).
 * 
 * This call is not thread-safe because it may write back unmodified data to
 * byte locations not part of the dest buffer. It also assumes that MMU pages
 * are MWORD_t aligned (or else it may produce unexpected page faults).
 */
void lssc_lr(void *dest, const void *src, size_t n)
{
    /* NOTE: this code is "mirroring" lssc_lf.
     * All shift operations and all pointer updates are reversed.
     */

    if (dest == src || 0 == n)
        return;

    int h= sizeof(MWORD_t);
    int i= ~((uintptr_t)src  + n - 1) & (h-1);
    int j= ~((uintptr_t)dest + n - 1) & (h-1);
    int w= 0;

    MWORD_t *s= (MWORD_t*)(((uintptr_t)src  + n - 1) & ~(uintptr_t)(h-1));
    MWORD_t *d= (MWORD_t*)(((uintptr_t)dest + n - 1) & ~(uintptr_t)(h-1));
    MWORD_t b= 0, m= ~b;            /* no __uint128_t literals in gcc */

    /* If either src or dest is not MWORD_t aligned, we need to */
    /* merge each destination word from multiple source words.  */

    if (i || j)
    {
        int t= h * CHAR_BIT, u= i * CHAR_BIT, v= j * CHAR_BIT;
        MWORD_t a= *s-- << u;       /* Read (partial) last word and discard */
                                    /* high bits that are not part of src.  */
        b= *d & ~(m >> v);          /* Preserve high margin of dest.*/

        if (n < h - j)              /* This would break n -= h - j below and */
        {                           /* means all dest data fits into (*d).   */
            if (h - i < n)          /* If src data is split across two words,*/
                a |= *s >> (t - u); /* fetch and append second slice.        */

            int k= v + n * CHAR_BIT;
            a>>= v;                 /* Shift data into their dest place.*/
            a&=     ~(m >> k);      /* Discard bits that are not part of src.*/
            b|= *d & (m >> k);      /* Restore low margin.*/
            *d= b | a;              /* Write back. */
            return;
        }

        a>>= u;                     /* Shift data back to their src postion. */
        if (n < h - i)              /* All src data were in (*s)! */
        {
            int k= u + n * CHAR_BIT;
            a&= ~(m >> k);          /* Discard bits not part of src.*/
        }

        if (i != j)                 /* If src and dest alignment differ, we  */
        {                           /* need a load-shift-store loop.         */
            w= v - u;
            if (v < u)              /* If there's more trailing bits in dest,*/
            {                       /* reduce this to u < v by pretending    */
                b|= a << (u - v);   /* the bits in a were from dest (v+=t-u),*/
                w+= t;              /* so w' = (v+(t-u))-0 = t+(v-u) = w+t.  */
                a= *s--;            /* Restart at next word and pretend u==0.*/
            } 
            *d--= b | (a >> w);     /* Write back combined word.*/
            b= a << (t - w);        /* Shift excess bits into b.*/
            n-= h - j;

            while (h <= n)
            {                       /* While we can emit full words, repeat. */
                a= *s--;    /* Load */
                *d--= b | (a >> w); /* Shift & Store */
                b= a << (t - w);
                n-= h;
            }
        }
        else
        {                           /* Optimise when we don't need to shift.*/
            *d--= b | a;            /* b has no low margin; just write back.*/
            b = 0;
            n-= h - j;

            /* Fall through to share code with !i && !j case.*/
        }
    }

    /* this may be unrolled into a large chunk of assembly, so better share.*/
    while (h <= n)
        *d--= *s--, n-= h;

    if (n)
    {                               /* Some data left to be written.*/
        int k = n * CHAR_BIT;
        if (w < k)                  /* If we haven't read all data yet, */
            b |= (*s >> w);         /* load them from the last src word.*/
        MWORD_t a= *d;              /* Load first dest word.*/
        b&= ~(m >> k);              /* Clear extra src bits.*/
        a&=  (m >> k);              /* Clear dest bits to be replaced.*/
        *d= b | a;                  /* Merge and store.*/
    }
}
