
/* Microbenchmark for the checked-allocation arithmetic hot paths. */

#define _POSIX_C_SOURCE  200809L

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NGX_OK                       0
#define NGX_ERROR                   -1
#define NGX_MAX_SIZE_T_VALUE        ((size_t) (SIZE_MAX >> 1))

typedef intptr_t   ngx_int_t;
typedef uintptr_t  ngx_uint_t;

#define ngx_inline  inline

#include "../src/core/private/include/ngx_math.h"


static volatile uint64_t  benchmark_sink;


static double
now_seconds(void)
{
    struct timespec  ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }

    return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
}


static ngx_inline ngx_int_t
portable_mul_size(size_t a, size_t b, size_t *result)
{
    if (a == 0 || b == 0) {
        *result = 0;
        return NGX_OK;
    }

    if (a > NGX_PRIV_MATH_SIZE_MAX
        || b > NGX_PRIV_MATH_SIZE_MAX
        || b > NGX_PRIV_MATH_SIZE_MAX / a)
    {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = a * b;
    return NGX_OK;
}


static ngx_inline ngx_int_t
portable_add_size(size_t a, size_t b, size_t *result)
{
    if (a > NGX_PRIV_MATH_SIZE_MAX
        || b > NGX_PRIV_MATH_SIZE_MAX - a)
    {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = a + b;
    return NGX_OK;
}


static double
bench_mul(uint64_t iterations, ngx_uint_t portable, size_t *pairs)
{
    size_t   a, b, out;
    double   start, elapsed;
    uint64_t i, acc;

    acc = 0;
    out = 0;
    start = now_seconds();

    for (i = 0; i < iterations; i++) {
        a = pairs[(i & 1023) * 2];
        b = pairs[(i & 1023) * 2 + 1];

        if (portable) {
            if (portable_mul_size(a, b, &out) != NGX_OK) {
                return -1;
            }

        } else {
            if (ngx_priv_math_mul_size(a, b, &out) != NGX_OK) {
                return -1;
            }
        }

        acc += out;
    }

    elapsed = now_seconds() - start;
    benchmark_sink = benchmark_sink + acc;
    return elapsed;
}


static double
bench_add(uint64_t iterations, ngx_uint_t portable, size_t *pairs)
{
    size_t   a, b, out;
    double   start, elapsed;
    uint64_t i, acc;

    acc = 0;
    out = 0;
    start = now_seconds();

    for (i = 0; i < iterations; i++) {
        a = pairs[(i & 1023) * 2];
        b = (pairs[(i & 1023) * 2 + 1] & 0xffff) + 1;

        if (portable) {
            if (portable_add_size(a, b, &out) != NGX_OK) {
                return -1;
            }

        } else {
            if (ngx_priv_math_add_size(a, b, &out) != NGX_OK) {
                return -1;
            }
        }

        acc += out;
    }

    elapsed = now_seconds() - start;
    benchmark_sink = benchmark_sink + acc;
    return elapsed;
}


int
main(int argc, char **argv)
{
    size_t   *pairs;
    uint64_t  iterations, state;
    double    elapsed;
    int       round;
    unsigned  i;

    iterations = (argc > 1) ? strtoull(argv[1], NULL, 10) : 10000000;

    pairs = malloc(2048 * sizeof(size_t));
    if (pairs == NULL) {
        perror("malloc");
        return 2;
    }

    state = 0x9e3779b97f4a7c15ULL;

    for (i = 0; i < 1024; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        pairs[i * 2] = (size_t) ((state >> 16) & 0x7fffffff) + 1;
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        pairs[i * 2 + 1] = (size_t) ((state >> 16) & 0x7fffffff) + 1;
    }

    printf("operation,round,seconds,ns_per_op\n");

    for (round = 1; round <= 3; round++) {
        elapsed = bench_mul(iterations, 1, pairs);
        printf("portable_mul,%d,%.9f,%.3f\n", round, elapsed,
               elapsed * 1000000000.0 / (double) iterations);
        elapsed = bench_mul(iterations, 0, pairs);
        printf("builtin_mul,%d,%.9f,%.3f\n", round, elapsed,
               elapsed * 1000000000.0 / (double) iterations);
        elapsed = bench_add(iterations, 1, pairs);
        printf("portable_add,%d,%.9f,%.3f\n", round, elapsed,
               elapsed * 1000000000.0 / (double) iterations);
        elapsed = bench_add(iterations, 0, pairs);
        printf("builtin_add,%d,%.9f,%.3f\n", round, elapsed,
               elapsed * 1000000000.0 / (double) iterations);
    }

    free(pairs);
    fprintf(stderr, "sink=%llu\n", (unsigned long long) benchmark_sink);
    return 0;
}
