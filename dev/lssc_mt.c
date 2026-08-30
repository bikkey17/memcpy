/* Concurrent-neighbour check for the (T) variants.
 *
 * lssc_lft, lssc_lrt, init_lft and zero_lft never touch a byte outside dest.
 * A single-threaded test cannot see a violation of that: the plain variants
 * read the edge word, replace the middle and write the whole word back, so the
 * bytes around dest end up holding what they held before. The difference
 * becomes observable only if something else writes those bytes between that
 * load and that store, because the write-back then restores the old value and
 * the other writer's update is lost.
 *
 * So a neighbour thread owns a counter in the margin on each side of dest and
 * checks that what it reads back is what it just wrote. Nothing but the
 * routine under test could have changed it, so a mismatch is a direct
 * observation of a write outside dest and does not depend on timing. A clean
 * run is weaker evidence than a dirty one, so only the (T) variants gate the
 * build; for the others a quiet run means the window never opened, and is
 * reported as inconclusive rather than as a pass.
 *
 * The counters must be volatile. Otherwise the compiler may assume nothing
 * else writes them, a data race being undefined in any case, and fold the
 * read-back into the value just written, leaving the check a tautology.
 *
 * Made by Opus 5
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <pthread.h>
#include "lssc.h"

extern void lssc_lf (void *dest, const void *src, size_t n);
extern void lssc_lft(void *dest, const void *src, size_t n);
extern void lssc_lr (void *dest, const void *src, size_t n);
extern void lssc_lrt(void *dest, const void *src, size_t n);
extern void init_lf (void *dest, int ch, size_t n);
extern void init_lft(void *dest, int ch, size_t n);
extern void zero_lf (void *dest, size_t n);
extern void zero_lft(void *dest, size_t n);

#define W    MWORD_SIZE

/* dest starts half a word into a word and spans a whole number of words, so
   there are W/2 margin bytes below it and W/2 above, each run starting on a
   W/2 boundary -- room for a counter on both sides whatever W is. */
#define OFF  (W + W / 2)            /* dest offset within buf   */
#define LEN  (2 * W)                /* dest length              */
#define LO   (W)                    /* margin below dest        */
#define HI   (3 * W + W / 2)        /* margin above dest        */

#define ITERATIONS 3000000

static uint8_t buf[8 * W] __attribute__((aligned(64)));
static uint8_t src[8 * W];
static volatile int running;
static volatile long clobbers;

/* The routines differ in signature, so each gets a thunk down to "write LEN
   bytes at dest" -- what the neighbour is watching is the same either way. */
typedef void (*victim_t)(void *dest, size_t n);

static void v_lssc_lf (void *d, size_t n) { lssc_lf (d, src, n); }
static void v_lssc_lft(void *d, size_t n) { lssc_lft(d, src, n); }
static void v_lssc_lr (void *d, size_t n) { lssc_lr (d, src, n); }
static void v_lssc_lrt(void *d, size_t n) { lssc_lrt(d, src, n); }
static void v_init_lf (void *d, size_t n) { init_lf (d, 0x5a, n); }
static void v_init_lft(void *d, size_t n) { init_lft(d, 0x5a, n); }
static void v_zero_lf (void *d, size_t n) { zero_lf (d, n); }
static void v_zero_lft(void *d, size_t n) { zero_lft(d, n); }

static void *neighbour(void *unused)
{
    (void)unused;
    volatile uint16_t *lo = (volatile uint16_t*)&buf[LO];
    volatile uint16_t *hi = (volatile uint16_t*)&buf[HI];
    for (uint16_t v = 1; running; v++) {
        *lo = v;
        *hi = v;
        if (*lo != v || *hi != v)   /* only the routine could have written */
            clobbers++;
    }
    return NULL;
}

static long run(victim_t fn)
{
    pthread_t th;
    clobbers = 0; running = 1;
    pthread_create(&th, NULL, neighbour, NULL);
    for (long i = 0; i < ITERATIONS; i++)
        fn(buf + OFF, LEN);
    running = 0;
    pthread_join(th, NULL);
    return clobbers;
}

int main(void)
{
    for (unsigned i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 11 + 3);

    struct { const char *name; victim_t fn; int safe; } V[] = {
        {"lssc_lf ", v_lssc_lf , 0}, {"lssc_lft", v_lssc_lft, 1},
        {"lssc_lr ", v_lssc_lr , 0}, {"lssc_lrt", v_lssc_lrt, 1},
        {"init_lf ", v_init_lf , 0}, {"init_lft", v_init_lft, 1},
        {"zero_lf ", v_zero_lf , 0}, {"zero_lft", v_zero_lft, 1},
    };

    /* A counter needs 2 bytes of margin on each side. */
    if (W / 2 < (int)sizeof(uint16_t)) {
        printf("  MWORD_SIZE=%d leaves no room for a margin counter, skipped\n", W);
        return 0;
    }

    int bad = 0, mute = 0;
    for (unsigned i = 0; i < sizeof V / sizeof *V; i++) {
        long c = run(V[i].fn);
        const char *verdict;
        if (V[i].safe) verdict = c ? "FAIL: wrote outside dest" : "ok, neighbour intact";
        else           verdict = c ? "wrote outside dest (as documented)"
                                   : "inconclusive, no write seen this run";
        printf("  %s  %9ld clobbers  %s\n", V[i].name, c, verdict);
        if (V[i].safe && c)             /* only the (T) variants are a gate; */
            bad = 1;                    /* the others cannot fail this test  */
        if (!V[i].safe && !c)
            mute++;
    }
    if (mute)
        printf("\n  note: %d plain variant(s) went unobserved -- the contrast\n"
               "        is weaker this run, but no (T) variant was implicated\n", mute);
    printf("\n%s\n", bad ? "FAIL" : "OK");
    return bad;
}
