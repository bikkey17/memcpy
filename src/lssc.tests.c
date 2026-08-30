#include <unity.h>
#include <stdlib.h>
#include "lssc.h"

static void fill(uint8_t *dest, uint8_t val)
{
    MWORD_t *start = (MWORD_t*)&dest[0];
    MWORD_t *end = (MWORD_t*)&dest[512];
    MWORD_t pp = val;
    for (int n= 8; n < 8 * sizeof(MWORD_t); n+= n)
        pp|= pp << n;

    for (MWORD_t *p = start; p < end; p++)
        *p= pp;

}

extern void lssc_lf (void *dest, const void *src, size_t n);
extern void lssc_lft(void *dest, const void *src, size_t n);
extern void lssc_lr (void *dest, const void *src, size_t n);
extern void lssc_lrt(void *dest, const void *src, size_t n);

typedef void (*memcpy_t)(void *dest, const void *src, size_t n);

static void copy_ShouldCopyWithoutFail(memcpy_t copy) {

    _Alignas(MWORD_SIZE) static uint8_t src[512];
    _Alignas(MWORD_SIZE) static uint8_t dst[512];

    for(int i = 0; i < 16; i++) // i is the start offset in src
    {
        for(int j = 0; j < 16; j++) // j is the start offset in dst
        {
            for (uint8_t l = 0; l < 254; l++) // l is the length
            {
                fill(src, 254);
                fill(dst, 255);

                // initialize the are to be copied with unique values
                for (uint8_t k= 0; k < l; k++)
                    src[i + k] = k;

                copy(dst + j, src + i, l);

                // check for no underrun

                for (int k = 0; k < i; k++)
                    TEST_ASSERT_EQUAL_INT(254, src[k]);

                for (int k = 0; k < j; k++)
                    TEST_ASSERT_EQUAL_INT(255, dst[k]);

                // check all accounted for

                for (uint8_t k = 0; k < l; k++)
                {
                    TEST_ASSERT_EQUAL_INT(k, src[i + k]);
                    TEST_ASSERT_EQUAL_INT(k, dst[j + k]);
                }

                // check no overrun

                for (int k = i+l; k < sizeof(src) / sizeof(*src); k++)
                    TEST_ASSERT_EQUAL_INT(254, src[k]);

                for (int k = j+l; k < sizeof(dst) / sizeof(*dst); k++)
                    TEST_ASSERT_EQUAL_INT(255, dst[k]);
            }
        }
    }
}

extern void init_lf (void *dest, int ch, size_t n);
extern void init_lft(void *dest, int ch, size_t n);

typedef void (*memset_t)(void *dest, int ch, size_t n);

static void init_ShouldInitWithoutFail(memset_t init) {

    _Alignas(MWORD_SIZE) static uint8_t dst[512];

    for(int ch = 0; ch <= CHAR_MAX; ch++) // better safe than sorry
    {
        for(int j = 0; j < 16; j++) // j is the start offset in dst
        {
            for (uint8_t l = 0; l < 254; l++) // l is the length
            {
                fill(dst, 255);
                init(dst + j, ch, l);

                // check for no underrun

                for (int k = 0; k < j; k++)
                    TEST_ASSERT_EQUAL_INT(255, dst[k]);

                // check all accounted for

                for (uint8_t k = 0; k < l; k++)
                {
                    TEST_ASSERT_EQUAL_INT(ch, dst[j + k]);
                }

                // check no overrun

                for (int k = j+l; k < sizeof(dst) / sizeof(*dst); k++)
                    TEST_ASSERT_EQUAL_INT(255, dst[k]);
            }
        }
    }
}

extern void zero_lf (void *dest, size_t n);
extern void zero_lft(void *dest, size_t n);

typedef void (*bzero_t)(void *dest, size_t n);

static void zero_ShouldZeroWithoutFail(bzero_t zero) {

    _Alignas(MWORD_SIZE) static uint8_t dst[512];

    for(int j = 0; j < 16; j++) // j is the start offset in dst
    {
        for (uint8_t l = 0; l < 254; l++) // l is the length
        {
            fill(dst, 255);

            zero(dst + j, l);

            // check for no underrun

            for (int k = 0; k < j; k++)
                TEST_ASSERT_EQUAL_INT(255, dst[k]);

            // check all accounted for

            for (uint8_t k = 0; k < l; k++)
                TEST_ASSERT_EQUAL_INT(0, dst[j + k]);

            // check no overrun

            for (int k = j+l; k < sizeof(dst) / sizeof(*dst); k++)
                TEST_ASSERT_EQUAL_INT(255, dst[k]);
        }
    }
}

static void lssc_lf_ShouldCopyWithoutFail (void) { copy_ShouldCopyWithoutFail( lssc_lf );  }
static void lssc_lft_ShouldCopyWithoutFail(void) { copy_ShouldCopyWithoutFail( lssc_lft ); }
static void lssc_lr_ShouldCopyWithoutFail (void) { copy_ShouldCopyWithoutFail( lssc_lr );  }
static void lssc_lrt_ShouldCopyWithoutFail(void) { copy_ShouldCopyWithoutFail( lssc_lrt ); }
static void init_lf_ShouldInitWithoutFail (void) { init_ShouldInitWithoutFail( init_lf );  }
static void init_lft_ShouldInitWithoutFail(void) { init_ShouldInitWithoutFail( init_lft ); }
static void zero_lf_ShouldZeroWithoutFail (void) { zero_ShouldZeroWithoutFail( zero_lf );  }
static void zero_lft_ShouldZeroWithoutFail(void) { zero_ShouldZeroWithoutFail( zero_lft ); }


void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(lssc_lf_ShouldCopyWithoutFail);
    RUN_TEST(lssc_lft_ShouldCopyWithoutFail);
    RUN_TEST(lssc_lr_ShouldCopyWithoutFail);
    RUN_TEST(lssc_lrt_ShouldCopyWithoutFail);
    RUN_TEST(init_lf_ShouldInitWithoutFail);
    RUN_TEST(init_lft_ShouldInitWithoutFail);
    RUN_TEST(zero_lf_ShouldZeroWithoutFail);
    RUN_TEST(zero_lft_ShouldZeroWithoutFail);

    return UNITY_END();
}