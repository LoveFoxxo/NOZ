
/* Standalone boundary tests for src/core/private/include/ngx_math.h. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define NGX_OK                       0
#define NGX_ERROR                   -1
#define NGX_MAX_SIZE_T_VALUE        ((size_t) (SIZE_MAX >> 1))

typedef intptr_t   ngx_int_t;
typedef uintptr_t  ngx_uint_t;

#define ngx_inline  inline

#include "../src/core/private/include/ngx_math.h"


static unsigned long  checks;


#define TEST(c)                                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(c)) {                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);           \
            return 1;                                                        \
        }                                                                    \
    } while (0)


int
main(void)
{
    size_t     out, values[16];
    ngx_uint_t uout;
    size_t     i, j;

    out = 0;
    uout = 0;

    TEST(ngx_priv_math_valid_size(0) == NGX_OK);
    TEST(ngx_priv_math_valid_size(NGX_PRIV_MATH_SIZE_MAX) == NGX_OK);
    TEST(ngx_priv_math_valid_size(NGX_MAX_SIZE_T_VALUE) == NGX_ERROR);
    TEST(ngx_priv_math_valid_size(SIZE_MAX) == NGX_ERROR);

    TEST(ngx_priv_math_add_size(10, 20, &out) == NGX_OK && out == 30);
    TEST(ngx_priv_math_add_size(NGX_PRIV_MATH_SIZE_MAX, 1, &out) == NGX_ERROR);
    TEST(out == NGX_MAX_SIZE_T_VALUE);
    TEST(ngx_priv_math_add_size(SIZE_MAX, 1, &out) == NGX_ERROR);
    TEST(out == NGX_MAX_SIZE_T_VALUE);

    TEST(ngx_priv_math_mul_size(16, 32, &out) == NGX_OK && out == 512);
    TEST(ngx_priv_math_mul_size(0, SIZE_MAX, &out) == NGX_OK && out == 0);
    TEST(ngx_priv_math_mul_size(SIZE_MAX, 0, &out) == NGX_OK && out == 0);
    TEST(ngx_priv_math_mul_size(NGX_MAX_SIZE_T_VALUE, 2, &out) == NGX_ERROR);
    TEST(out == NGX_MAX_SIZE_T_VALUE);

    TEST(ngx_priv_math_sat_add_size(100, 23) == 123);
    TEST(ngx_priv_math_sat_add_size(SIZE_MAX, 1) == NGX_MAX_SIZE_T_VALUE);
    TEST(ngx_priv_math_sat_mul_size(31, 33) == 1023);
    TEST(ngx_priv_math_sat_mul_size(SIZE_MAX, 2) == NGX_MAX_SIZE_T_VALUE);
    TEST(ngx_priv_math_acc2_size(1, 2, 3) == 6);
    TEST(ngx_priv_math_acc3_size(1, 2, 3, 4) == 10);
    TEST(ngx_priv_math_acc4_size(1, 2, 3, 4, 5) == 15);
    TEST(ngx_priv_math_acc5_size(1, 2, 3, 4, 5, 6) == 21);
    TEST(ngx_priv_math_acc5_size(NGX_PRIV_MATH_SIZE_MAX, 1, 2, 3, 4, 5)
         == NGX_MAX_SIZE_T_VALUE);

    TEST(ngx_priv_math_add_uint(100, 200, &uout) == NGX_OK && uout == 300);
    TEST(ngx_priv_math_add_uint((ngx_uint_t) NGX_PRIV_MATH_SIZE_MAX, 1, &uout)
         == NGX_ERROR);

    TEST(ngx_priv_math_align_up_size(0, 64, &out) == NGX_OK && out == 0);
    TEST(ngx_priv_math_align_up_size(1, 64, &out) == NGX_OK && out == 64);
    TEST(ngx_priv_math_align_up_size(129, 64, &out) == NGX_OK && out == 192);
    TEST(ngx_priv_math_align_up_size(129, 0, &out) == NGX_ERROR);
    TEST(ngx_priv_math_align_up_size(129, 3, &out) == NGX_ERROR);
    TEST(ngx_priv_math_align_up_size(NGX_PRIV_MATH_SIZE_MAX, 64, &out)
         == NGX_ERROR);
    TEST(out == NGX_MAX_SIZE_T_VALUE);

#if defined(__SIZEOF_INT128__)
    values[0] = 0;
    values[1] = 1;
    values[2] = 2;
    values[3] = 63;
    values[4] = 64;
    values[5] = 127;
    values[6] = 128;
    values[7] = 255;
    values[8] = 256;
    values[9] = 65535;
    values[10] = 65536;
    values[11] = NGX_PRIV_MATH_SIZE_MAX - 1;
    values[12] = NGX_PRIV_MATH_SIZE_MAX;
    values[13] = NGX_MAX_SIZE_T_VALUE;
    values[14] = SIZE_MAX - 1;
    values[15] = SIZE_MAX;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        for (j = 0; j < sizeof(values) / sizeof(values[0]); j++) {
            unsigned __int128  reference;
            ngx_int_t          rc;
            ngx_uint_t         expect;

            reference = (unsigned __int128) values[i]
                        + (unsigned __int128) values[j];
            expect = reference <= (unsigned __int128) NGX_PRIV_MATH_SIZE_MAX;
            rc = ngx_priv_math_add_size(values[i], values[j], &out);
            TEST(rc == (expect ? NGX_OK : NGX_ERROR));
            TEST(out == (expect ? (size_t) reference : NGX_MAX_SIZE_T_VALUE));

            reference = (unsigned __int128) values[i]
                        * (unsigned __int128) values[j];
            expect = reference <= (unsigned __int128) NGX_PRIV_MATH_SIZE_MAX;
            rc = ngx_priv_math_mul_size(values[i], values[j], &out);
            TEST(rc == (expect ? NGX_OK : NGX_ERROR));
            TEST(out == (expect ? (size_t) reference : NGX_MAX_SIZE_T_VALUE));
        }
    }
#endif

#if (NGX_PRIV_MATH_HAVE_OVERFLOW)
    printf("ngx_private_math_test: builtin path, %lu checks passed\n", checks);
#else
    printf("ngx_private_math_test: portable path, %lu checks passed\n", checks);
#endif

    return 0;
}
