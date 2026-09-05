/* Compares the copies against the alternatives available on a target with no
 * MMU.
 *
 * Four baselines, in increasing order of what the hardware has to allow:
 *
 *   byte_copy     `*d++ = *s++` over bytes. Legal at every offset on every
 *                 target, since a byte access is aligned by definition. On a
 *                 no-MMU target it is the only alternative to lssc, so it is
 *                 the comparison the second table turns on.
 *   word_copy     the same loop over MWORD_t. Available when something else
 *                 guarantees alignment. First table only; see "Why word_copy
 *                 is aligned-only".
 *   word_copy_ua  the same loop with the hardware absorbing the mismatch:
 *                 what an unaligned-capable configuration can do and lssc
 *                 cannot.
 *   libc memcpy   included for scale. It may use vectors, unaligned loads and
 *                 non-temporal stores, none of which a freestanding
 *                 aligned-access-only copy can reach for.
 *
 * The tables answer, in order: what lssc costs when everything is aligned (it
 * degenerates to the same word loop); what an alignment mismatch costs, and
 * how that compares to copying bytes; and what the same mismatch costs when
 * the hardware absorbs it.
 *
 * Measured here, against byte_copy: lssc runs about an order of magnitude
 * faster when the two alignments match, and keeps a smaller lead of roughly
 * 1.5-3x when they do not. It was not slower in any configuration measured,
 * which matters because a general copy cannot know its alignments in advance.
 *
 *
 * Availability of unaligned access
 *
 * This runs on a host, so the second and third tables describe a permissive
 * configuration rather than the target. Whether an unaligned access is allowed
 * at all depends on the architecture and on how memory is configured:
 *
 *   x86-64                       allowed for ordinary loads and stores;
 *                                misalignment costs time, never faults.
 *   AArch64, MMU on, Normal
 *     memory, SCTLR_ELx.A == 0   allowed.
 *   AArch64, MMU off             not allowed. With the MMU disabled every data
 *                                access is Device-nGnRnE, and unaligned access
 *                                to Device memory faults, including one
 *                                sitting entirely inside a page.
 *   AArch64, SCTLR_ELx.A == 1    not allowed, even on Normal memory.
 *   Any target, Device/MMIO
 *     region                     not allowed.
 *
 * On AArch64 with no MMU, then, word_copy_ua is unavailable rather than merely
 * slower, and memcpy is the function this library exists to supply. That
 * leaves byte_copy as the alternative to read the second table against.
 *
 * The third table measures an x86-64 microarchitectural property and does not
 * generalise. It separates two costs that are easily conflated, but on a
 * target where unaligned access faults unconditionally the distinction does
 * not arise.
 *
 *
 * Why word_copy is aligned-only
 *
 * It cannot run misaligned. The compiler selects the instruction at compile
 * time from the type and cannot know the runtime address. MWORD_t is
 * __uint128_t, whose _Alignof is 16, so dereferencing MWORD_t* asserts the
 * pointer is 16-byte aligned and the compiler may emit an instruction that
 * requires it. It will not add a runtime alignment check, which would cost
 * every iteration to cover a case the type excludes.
 *
 * Measured with this file's word_copy at src%16 = 1:
 *
 *     -O0        scalar moves, no vector    completes
 *     -O1..-O3   movaps                     SIGSEGV
 *
 * Passing a misaligned pointer is undefined behaviour. The -O0 row shows it
 * can also complete without symptom, which is how it survives a debug build.
 * word_copy_ua sidesteps this: memcpy of a constant MWORD_SIZE through
 * uint8_t* states that the alignment is 1, so the compiler emits movups.
 * Defined behaviour, still a single load and store; checked in the
 * disassembly, no libc call.
 *
 *
 * Measurement pitfalls
 *
 * All three of these produced plausible but wrong numbers here. Check for them
 * after changing anything in this file.
 *
 * 1. Inlining. The baselines are static and reached through a function
 *    pointer, which the compiler sees through: it inlined them into the timing
 *    loop while lssc_* -- a separate translation unit, no LTO -- kept paying a
 *    call, so the two sides were not comparable. Hence noinline on each
 *    baseline. To confirm they survive:
 *        clang -O3 -Isrc -S -o - dev/lssc_bench.c | grep '^word_copy'
 *
 * 2. Dead-code elimination. If nothing observes dest, the copy is deleted and
 *    the timing loop measures an empty loop. The memory clobber in gbps()
 *    prevents this; without it every number in the file is invalid.
 *
 * 3. Auto-vectorisation changing what a baseline is. Written plainly,
 *    byte_copy vectorises into unaligned 16-byte moves, which makes it a
 *    second copy of word_copy_ua and describes a copy the target cannot
 *    perform. Hence the volatile there. Check with:
 *        clang -O3 -Isrc -S -o - dev/lssc_bench.c |
 *            awk '/^byte_copy:/,/.size.byte_copy/' | grep -c movups
 *
 * In general: confirm that what is being timed is what it claims to be.
 *
 *
 * Reading the numbers
 *
 * GB/s, best of several trials. Best-of reports the machine's capability but
 * hides variance, so the header line re-measures one fixed configuration
 * repeatedly and prints the spread between identical runs. Gaps smaller than
 * that spread carry no information. On a shared or frequency-scaling machine
 * the spread can exceed 30%, which covers most of the first table; for numbers
 * worth quoting, use a quiet machine with a pinned CPU and fixed frequency.
 *
 * Not part of `make check`, since a spread that size makes an unstable gate.
 *
 * Made by Opus 5
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "lssc.h"

extern void lssc_lf (void *dest, const void *src, size_t n);
extern void lssc_lft(void *dest, const void *src, size_t n);
extern void lssc_lr (void *dest, const void *src, size_t n);
extern void lssc_lrt(void *dest, const void *src, size_t n);

typedef void (*copy_t)(void *dest, const void *src, size_t n);

/* The baseline an MMU, or a cooperative allocator, provides and which lssc
   does without: valid only when both ends are MWORD_t aligned and n is a whole
   number of words. Faults otherwise; see the header.

   noinline on every baseline: without it the compiler inlines them into the
   timing loop while lssc_* still pays a call, leaving the two sides
   incomparable. */
__attribute__((noinline))
static void word_copy(void *dest, const void *src, size_t n)
{
    MWORD_t *d = dest;
    const MWORD_t *s = src;
    for (size_t i = n / sizeof(MWORD_t); i; i--)
        *d++ = *s++;
}

/* The same loop with the hardware absorbing the misalignment: what alignment
   mismatch costs when the CPU handles it rather than the software -- on a
   configuration that permits unaligned access at all, which the no-MMU AArch64
   target this library targets does not. See the header.

   memcpy of a constant MWORD_SIZE is the defined way to ask for it. Because
   uint8_t* gives the compiler an alignment of 1, this lowers to a single
   unaligned load and store (movups on x86-64, no libc call). Casting to a
   misaligned MWORD_t* would be undefined behaviour, and a segfault at -O1 and
   above. */
__attribute__((noinline))
static void word_copy_ua(void *dest, const void *src, size_t n)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (size_t i = n / sizeof(MWORD_t); i; i--) {
        memcpy(d, s, sizeof(MWORD_t));
        d += sizeof(MWORD_t);
        s += sizeof(MWORD_t);
    }
}

/* The fallback that always works: byte accesses are aligned by definition, so
   this is legal at every offset on every target, MMU or not. On a no-MMU
   machine with mismatched alignments it is what lssc is measured against.

   The volatile is what keeps it a byte copy. Written plainly, the host
   compiler vectorises the loop into unaligned 16-byte moves (movups in the
   disassembly), which turns the baseline into word_copy_ua and so describes a
   copy a strict-align target cannot perform. volatile leaves the scalar
   byte-at-a-time loop such a target would run.

   It blocks vectorisation and little else: clang still unrolls this to roughly
   eight scalar movzbl/movb pairs per iteration, close to what a strict-align
   target would emit. */
__attribute__((noinline))
static void byte_copy(void *dest, const void *src, size_t n)
{
    volatile uint8_t *d = dest;
    const volatile uint8_t *s = src;
    for (size_t i = n; i; i--)
        *d++ = *s++;
}

__attribute__((noinline))
static void libc_copy(void *dest, const void *src, size_t n)
{
    memcpy(dest, src, n);
}

#define TRIALS   5
#define MAXSIZE  (1u << 20)
#define PAGE     4096
/* Page-aligned with a spare page, so a stated offset really does straddle the
   boundary the placement table below claims it does. */
#define SLACK    (2 * PAGE)

static uint8_t *bufsrc, *bufdst;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* The memory clobber stops the compiler hoisting the call out of the loop or
   keeping dest in registers across iterations. Without it nothing observes
   dest, so the copy is dead code: the loop still runs and still reports a
   plausible number, having measured nothing. */
static double gbps(copy_t fn, size_t soff, size_t doff, size_t n)
{
    uint8_t *s = bufsrc + soff, *d = bufdst + doff;
    long reps = 1;
    double dt;

    for (;;) {                          /* calibrate to ~30ms per trial */
        double t0 = now();
        for (long i = 0; i < reps; i++) {
            fn(d, s, n);
            __asm__ volatile("" ::: "memory");
        }
        dt = now() - t0;
        if (dt > 0.03 || reps > (1L << 30)) break;
        reps *= 2;
    }

    double best = dt;
    for (int t = 1; t < TRIALS; t++) {
        double t0 = now();
        for (long i = 0; i < reps; i++) {
            fn(d, s, n);
            __asm__ volatile("" ::: "memory");
        }
        dt = now() - t0;
        if (dt < best) best = dt;
    }
    return (double)n * (double)reps / best / 1e9;
}

static const struct { const char *name; copy_t fn; } V[] = {
    {"lssc_lf",  lssc_lf },
    {"lssc_lft", lssc_lft},
    {"lssc_lr",  lssc_lr },
    {"lssc_lrt", lssc_lrt},
};
#define NV (sizeof V / sizeof *V)

int main(void)
{
    bufsrc = aligned_alloc(PAGE, MAXSIZE + SLACK);
    bufdst = aligned_alloc(PAGE, MAXSIZE + SLACK);
    if (!bufsrc || !bufdst) { perror("aligned_alloc"); return 2; }
    for (size_t i = 0; i < MAXSIZE + SLACK; i++) bufsrc[i] = (uint8_t)(i * 7 + 1);
    memset(bufdst, 0, MAXSIZE + SLACK);

    /* Re-measure one fixed configuration repeatedly. Everything here is a
       best-of, which hides variance rather than showing it, so this reports
       up front how far apart identical measurements land on this machine.
       Treat differences smaller than that spread as nothing at all. */
    double lo = 1e30, hi = 0;
    for (int i = 0; i < 9; i++) {
        double g = gbps(lssc_lft, 0, 0, 4096);
        if (g < lo) lo = g;
        if (g > hi) hi = g;
    }
    printf("MWORD_SIZE=%d  GB/s, best of %d\n", MWORD_SIZE, TRIALS);
    printf("timing noise here: identical runs spanned %.1f-%.1f GB/s (%.0f%%)"
           " -- ignore smaller gaps\n\n", lo, hi, 100.0 * (hi - lo) / hi);

    static const size_t sizes[] = {64, 256, 1024, 4096, 65536, 262144, MAXSIZE};

    printf("both ends MWORD_t aligned -- the case word_copy can do\n");
    printf("  %9s %10s %10s", "size", "word_copy", "byte_copy");
    for (unsigned v = 0; v < NV; v++) printf(" %9s", V[v].name);
    printf(" %9s\n", "memcpy");
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        size_t n = sizes[i];
        printf("  %9zu %10.1f %10.1f", n, gbps(word_copy, 0, 0, n),
               gbps(byte_copy, 0, 0, n));
        for (unsigned v = 0; v < NV; v++) printf(" %9.1f", gbps(V[v].fn, 0, 0, n));
        printf(" %9.1f\n", gbps(libc_copy, 0, 0, n));
    }

    printf("\nmisaligned -- word_copy is not legal here at all. word_copy_ua is\n"
           "the same loop with the hardware absorbing the mismatch, which needs a\n"
           "config that permits unaligned access -- not no-MMU AArch64 (n = 4096)\n");
    printf("  %6s %6s %12s %10s", "src", "dst", "word_copy_ua", "byte_copy");
    for (unsigned v = 0; v < NV; v++) printf(" %9s", V[v].name);
    printf(" %9s\n", "memcpy");
    static const size_t offs[][2] = {
        {0,0}, {0,1}, {1,0}, {1,1}, {3,3}, {3,7}, {8,0}, {0,8}, {7,9},
    };
    for (unsigned i = 0; i < sizeof offs / sizeof *offs; i++) {
        size_t so = offs[i][0], dof = offs[i][1];
        printf("  %6zu %6zu %12.1f %10.1f", so, dof,
               gbps(word_copy_ua, so, dof, 4096), gbps(byte_copy, so, dof, 4096));
        for (unsigned v = 0; v < NV; v++) printf(" %9.1f", gbps(V[v].fn, so, dof, 4096));
        printf(" %9.1f\n", gbps(libc_copy, so, dof, 4096));
    }

    /* Separates two costs, on a host where unaligned access is permitted at
       all: the load/store unit absorbs the misalignment itself, and the MMU is
       involved only once an access straddles a page and needs a second
       translation. Same instruction and size throughout; only what the access
       crosses changes. Buffers are page aligned, so the offsets land where
       they claim to.

       The page row is the only result here that clears the noise floor:
       around 10x slower than the others, run after run, against perhaps 2x for
       crossing a cache line.

       An x86-64 result. On a no-MMU AArch64 target the question does not
       arise, no unaligned access being permitted at any placement; see the
       header. */
    printf("\nwhere the unaligned access lands -- host x86-64 property, says nothing\n"
           "about a no-MMU target (word_copy_ua, one %d-byte word from a fixed spot)\n",
           MWORD_SIZE);
    printf("  %-22s %12s\n", "access straddles", "GB/s");
    static const struct { const char *what; size_t off; } place[] = {
        {"nothing (aligned)",    0},
        {"nothing (in a line)",  8},
        {"a 64-byte cache line", 56},
        {"a 4096-byte page",     4088},
    };
    for (unsigned i = 0; i < sizeof place / sizeof *place; i++)
        printf("  %-22s %12.1f\n", place[i].what,
               gbps(word_copy_ua, place[i].off, place[i].off, MWORD_SIZE));

    free(bufsrc); free(bufdst);
    return 0;
}
