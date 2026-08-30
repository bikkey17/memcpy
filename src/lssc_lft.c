/* Aligned (L)oad-(S)hift-(S)tore memory (C)opy
 * (L)ittle-endian
 * (F)oward
 * (T)hread-safe
 *
 * Copyright (c) 2021-26 Bikkey {github,gitlab}.com/bikkey17
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "lssc.h"

static void heel(void *dest, MWORD_t val, size_t n)
{
    #if 1 < MWORD_SIZE
    #if 2 < MWORD_SIZE
    #if 4 < MWORD_SIZE
    #if 8 < MWORD_SIZE
    if (n & 8) *(uint64_t*)dest= val, dest = (uint64_t*)dest + 1, val >>= 64;
    #endif
    if (n & 4) *(uint32_t*)dest= val, dest = (uint32_t*)dest + 1, val >>= 32;
    #endif
    if (n & 2) *(uint16_t*)dest= val, dest = (uint16_t*)dest + 1, val >>= 16;
    #endif
    if (n & 1) *(uint8_t*)dest= val;
    #endif
}

#define HEAD(t)                           \
    if (((uintptr_t)dest) & sizeof(t))    \
    {                                     \
        if (n < sizeof(t))                \
            return heel(dest, val, n);    \
        *(t*)dest= val, n-= sizeof(t);    \
        if (!n)                           \
            return;                       \
        val>>= CHAR_BIT * sizeof(t);      \
        dest= ((t*)dest) + 1;             \
    }

static void head(void *dest, MWORD_t val, size_t n)
{
    if ((uintptr_t)dest & (MMIN_ALIGN - 1))
    {
    #if 1 == MMIN_ALIGN
    }
    #endif
    #if 1 < MWORD_SIZE
        HEAD(uint8_t)
    #if 2 == MMIN_ALIGN
    }
    #endif
    #if 2 < MWORD_SIZE
        HEAD(uint16_t)
    #if 4 == MMIN_ALIGN
    }
    #endif
    #if 4 < MWORD_SIZE
        HEAD(uint32_t)
    #if 8 == MMIN_ALIGN
    }
    #endif
    #if 8 < MWORD_SIZE
        HEAD(uint64_t)
    #endif
    #endif
    #endif
    #endif
    #if 16 == MMIN_ALIGN
    }
    #endif
    return heel(dest, val, n);
}

#undef HEAD

/* lssc_lft copies src to dest using a load-shift-store sequence of aligned
 * read and write operations. It assumes that the data bytes are stored in
 * little-endian order and that the CPU is a little-endian machine.
 *
 *  Data is read and written forward (i.e. from start to end of the buffers).
 *
 * The call is thread-safe in that it does not write to byte locations not part
 * of the dest buffer.
 */
void lssc_lft(void *dest, const void *src, size_t n)
{    
    /* NOTE: this code is "mirroring" lssc_lrt.
     * All shift operations and all pointer updates are reversed.
     */

    if (dest == src || 0 == n)
        return;

    int h= sizeof(MWORD_t);
    int i= (uintptr_t)src  & (h-1);
    int j= (uintptr_t)dest & (h-1);
    int w= 0;

    MWORD_t *s= (MWORD_t*)((uintptr_t)src  & ~(uintptr_t)(h-1));
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));
    MWORD_t b= 0, m= 0; m= ~m;      /* no __uint128_t literals in gcc */

    /* If the src or dest alignment is not at a MWORD_t boundary, we need to */
    /* merge the destination words from multiple source words.               */

    if (i || j)
    {
        int t= h * CHAR_BIT, u= i * CHAR_BIT, v= j * CHAR_BIT;
        MWORD_t a= *s++ >> u;       /* Read (partial) first word and discard */
                                    /* low bits that are not part of src.    */
        b= *d & ~(m << v);          /* Preserve low margin.*/

        if (n < h - j)              /* This would break n -= h - j below and */
        {                           /* means all dest data fits into (*d).   */
            if (h - i < n)          /* If src data is split across two words,*/
                a |= *s << (t - u); /* fetch and append second slice.        */

            return head(dest, a, n);
        }

        a<<= u;                     /* Shift data back to their src postion. */
        if (n < h - i)              /* All src data were in (*s)! */
        {
            int k= u + n * CHAR_BIT;
            a&= ~(m << k);          /* Discard bits not part of src.*/
        }

        if (i != j)                 /* If src and dest alignment differ, we  */
        {                           /* need a load-shift-store loop.         */
            w= v - u;
            if (v < u)
            {                       /* Reduce this to u < v by pretending    */
                b|= a >> (u - v);   /* the bits in a were from dest (v+=t-u),*/
                w+= t;              /* so w' = (v+(t-u))-0 = t+(v-u) = w+t.  */
                a= *s++;            /* Restart at next word and pretend u==0.*/
            } 
            b|= (a << w);
            if (j)
                head(dest, b >> v, h - j);
            else
                *d= b;
            d++;
            b= a >> (t - w);        /* Shift excess bits into b.*/
            n-= h - j;

            while (h <= n)
            {                       /* While we can emit full words, repeat. */
                a= *s++;            /* Load */
                *d++= b | (a << w); /* Shift & Store */
                b= a >> (t - w);
                n-= h;
            }
        }
        else
        {                           /* Optimise when we don't need to shift. */
            b|= a;
            head(dest, b >> v, h - j);
            d++;
            b = 0;
            n-= h - j;

            /* Fall through to share code with !i && !j case.*/
        }
    }

    /* This may be unrolled into a large chunk of assembly, so better share.*/
    while (h <= n)
        *d++= *s++, n-= h;

    if (n)
    {                               /* Some data left to be written.*/
        int k = n * CHAR_BIT;
        if (w < k)                  /* If we haven't read all data yet,*/
            b |= (*s << w);         /* load them from the last src word.*/
        heel(d, b, n);
    }
}

void init_lft(void *dest, int ch, size_t n)
{    
    if (0 == n)
        return;

    int h= sizeof(MWORD_t);
    int j= (uintptr_t)dest & (h-1);
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));
    MWORD_t s = (unsigned char)ch;
    for (int t = CHAR_BIT; t < CHAR_BIT * MWORD_SIZE; t+= t)
        s|= s << t;

    if (j)
    {
        if (n < h - j)
            return head(dest, s >> (j * CHAR_BIT), n);
        head(dest, s >> (j * CHAR_BIT), h - j);
        d++, n-= h - j;
    }

    while (h <= n)
        *d++= s, n-= h;

    if (n)
        heel(d, s, n);
}


void zero_lft(void *dest, size_t n)
{    
    if (0 == n)
        return;

    int h= sizeof(MWORD_t);
    int j= (uintptr_t)dest & (h-1);
    MWORD_t *d= (MWORD_t*)((uintptr_t)dest & ~(uintptr_t)(h-1));

    if (j)
    {
        if (n < h - j)
            return head(dest, 0, n);
        head(dest, 0, h - j);
        d++, n-= h - j;
    }

    while (h <= n)
        *d++= 0, n-= h;

    if (n)
        heel(d, 0, n);
}
