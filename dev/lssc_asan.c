/* Write-outside-dest check for the (T) variants, under AddressSanitizer.
 *
 * Only the (T) routines are exercised here, and that restriction is what
 * makes the target possible at all. Every variant in this library loads a
 * whole aligned MWORD_t at each end of the run, so all of them read past
 * both buffers by design; ASan cannot tell that from a real overrun. Turning
 * reads off (-mllvm -asan-instrument-reads=false, see the Makefile) leaves
 * only stores instrumented, and "never stores outside dest" is exactly what
 * lssc_lft, lssc_lrt, init_lft and zero_lft promise. Their plain siblings
 * write the whole edge word back and would abort here, correctly, which is
 * why they are not in the table.
 *
 * This is the deterministic counterpart to lssc_mt.c. That one can only
 * observe a stray write if a neighbouring thread happens to interleave with
 * it; here the shadow map catches the store itself, on every call.
 *
 * dest is placed at the very end of its allocation so the byte one past it
 * is redzone: ASan tracks the right edge of a heap block exactly, to the
 * byte, whatever the alignment. The margin below dest is poisoned by hand,
 * which is only accurate to ASan's 8-byte shadow granularity -- see the
 * comment on low_margin_is_watched() below for what that does and does not
 * cover.
 * 
 * Made by Opus 5
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "lssc.h"

#if !defined(__has_include)
#  error "need __has_include"
#endif
#if !__has_include(<sanitizer/asan_interface.h>)
#  error "this file is only meaningful under -fsanitize=address"
#endif
#include <sanitizer/asan_interface.h>

extern void lssc_lft(void *dest, const void *src, size_t n);
extern void lssc_lrt(void *dest, const void *src, size_t n);
extern void init_lft(void *dest, int ch, size_t n);
extern void zero_lft(void *dest, size_t n);

#define MAXN   96
#define FILLCH 0x5a

static uint8_t pattern[MAXN + 64];

enum kind { COPY, INIT, ZERO };

/* ASan's shadow map records one state per 8 bytes, with a partial-granule
   encoding only for the *end* of an addressable range. So poisoning the
   margin below dest is exact only down to the granule containing dest: up to
   7 bytes immediately below dest stay addressable and a store into just
   those would go unseen. Stores from this library are whole MWORD_t, so a
   low-edge violation spans the entire edge word and reaches the poisoned
   granules whenever the margin is at least 8 bytes wide. */
static int low_margin_is_watched(size_t off)
{
    return off >= 8;
}

static void check(const char *name, enum kind k, size_t off, size_t n)
{
    uint8_t *p = malloc(off + n);
    if (!p) { perror("malloc"); exit(2); }
    uint8_t *dest = p + off;                /* dest+n == end of allocation */

    memset(p, 0xc3, off + n);
    if (off)
        __asan_poison_memory_region(p, off);

    switch (k) {                            /* any stray store aborts here */
    case COPY: lssc_lft(dest, pattern, n); break;
    case INIT: init_lft(dest, FILLCH, n);  break;
    case ZERO: zero_lft(dest, n);          break;
    }
    if (k == COPY) {                        /* second copy routine, same shape */
        if (off) __asan_unpoison_memory_region(p, off);
        memset(p, 0xc3, off + n);
        if (off) __asan_poison_memory_region(p, off);
        lssc_lrt(dest, pattern, n);
    }

    if (off)
        __asan_unpoison_memory_region(p, off);

    /* Content, so a routine cannot pass by simply not writing anything. */
    for (size_t i = 0; i < n; i++) {
        uint8_t want = k == COPY ? pattern[i] : k == INIT ? FILLCH : 0;
        if (dest[i] != want) {
            printf("  %s CONTENT off=%zu n=%zu byte %zu = %02x want %02x\n",
                   name, off, n, i, dest[i], want);
            exit(1);
        }
    }
    free(p);
}

int main(void)
{
    for (unsigned i = 0; i < sizeof pattern; i++) pattern[i] = (uint8_t)(i * 7 + 1);

    struct { const char *name; enum kind k; } T[] = {
        {"lssc_lft/lrt", COPY}, {"init_lft", INIT}, {"zero_lft", ZERO},
    };

    size_t watched = 0, blind = 0;
    for (unsigned t = 0; t < sizeof T / sizeof *T; t++) {
        for (size_t off = 0; off < 2 * MWORD_SIZE; off++)
            for (size_t n = 1; n <= MAXN; n++) {
                check(T[t].name, T[t].k, off, n);
                if (off == 0 || low_margin_is_watched(off)) watched++; else blind++;
            }
        printf("  %-12s ok\n", T[t].name);
    }

    printf("\n  %zu cases with both edges watched, %zu with the low margin\n"
           "  under ASan's 8-byte granularity (high edge exact throughout)\n",
           watched, blind);
    printf("\nOK\n");
    return 0;
}
