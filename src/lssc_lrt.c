/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * (L)ittle-endian
 * (R)everse
* (T)hread-safe
 *
 * Copyright (c) 2021-26 Bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "lssc.h"

static void head(void *end, MWORD_t val, size_t n)
{
    #if 1 < MWORD_SIZE
    #if 2 < MWORD_SIZE
    #if 4 < MWORD_SIZE
    #if 8 < MWORD_SIZE
    if (n & 8) end = (uint64_t*)end - 1, *(uint64_t*)end= val >> (MWORD_BITS - 64), val <<= 64;
    #endif
    if (n & 4) end = (uint32_t*)end - 1, *(uint32_t*)end= val >> (MWORD_BITS - 32), val <<= 32;
    #endif
    if (n & 2) end = (uint16_t*)end - 1, *(uint16_t*)end= val >> (MWORD_BITS - 16), val <<= 16;
    #endif
    if (n & 1) end = (uint8_t*)end - 1,  *(uint8_t*)end = val >> (MWORD_BITS -  8);
    #endif
}

#define HEEL(t)                                        \
    if (((uintptr_t)end) & sizeof(t))                  \
    {                                                  \
        if (n < sizeof(t))                             \
            return head(end, val, n);                  \
        end= ((t*)end) - 1;                            \
        *(t*)end= val >> (MWORD_BITS - CHAR_BIT * sizeof(t)); \
        n-= sizeof(t);                                 \
        if (!n)                                        \
            return;                                    \
        val<<= CHAR_BIT * sizeof(t);                   \
    }

static void heel(void *dest, MWORD_t val, size_t n)
{
    void *end= (uint8_t*)dest + n;

    if ((uintptr_t)end & (MMIN_ALIGN - 1))
    {
    #if 1 == MMIN_ALIGN
    }
    #endif
    #if 1 < MWORD_SIZE
        HEEL(uint8_t)
    #if 2 == MMIN_ALIGN
    }
    #endif
    #if 2 < MWORD_SIZE
        HEEL(uint16_t)
    #if 4 == MMIN_ALIGN
    }
    #endif
    #if 4 < MWORD_SIZE
        HEEL(uint32_t)
    #if 8 == MMIN_ALIGN
    }
    #endif
    #if 8 < MWORD_SIZE
        HEEL(uint64_t)
    #endif
    #endif
    #endif
    #endif
    #if 16 == MMIN_ALIGN
    }
    #endif
    return head(end, val, n);
}

#undef HEEL

/* lssc_lrt copies src to dest using a load-shift-store sequence. of aligned
 * read and write operations. It assumes that the data bytes are stored in
 * little-endian order and that the CPU is a little-endian machine.
 *
 * Data is read and written in reverse (i.e. from end to start of the buffers).
 *
 * The call is thread-safe in that it does not write to byte locations not part
 * of the dest buffer.
 */
void lssc_lrt(void *dest, const void *src, size_t n)
{
    /* NOTE: this code is "mirroring" lssc_lft.
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

            return heel(dest, a, n);
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
            b |= (a >> w);
            if (j)                      /* Reverse mirror of lssc_lft.       */
                heel(d, b << v, h - j); /* Data is the low h-j bytes of *d   */
            else
                *d= b;                 
            d--;
            b= a << (t - w);        /* Shift excess bits into b.*/
            n-= h - j;

            while (h <= n)
            {                       /* While we can emit full words, repeat. */
                a= *s--;            /* Load */
                *d--= b | (a >> w); /* Shift & Store */
                b= a << (t - w);
                n-= h;
            }
        }
        else
        {                           /* Optimise when we don't need to shift. */
            b|= a;
            heel(d, b << v, h - j);
            d--;
            b = 0;
            n-= h - j;

            /* Fall through to share code with !i && !j case.*/
        }
    }

    /* This may be unrolled into a large chunk of assembly, so better share.*/
    while (h <= n)
        *d--= *s--, n-= h;

    if (n)
    {                               /* Some data left to be written.*/
        int k = n * CHAR_BIT;
        if (w < k)                  /* If we haven't read all data yet, */
            b |= (*s >> w);         /* load them from the last src word.*/
        head(d + 1, b, n);
    }
}
