
/*
 * Copyright (C) Nginx, Inc.
 *
 * Private, dependency-free integer size arithmetic for nginx.
 *
 * Techniques adapted from:
 * - Armadillo include/armadillo_bits/memory.hpp (Apache-2.0):
 *   validate an element-count/element-size product before allocation.
 * - OpenBLAS common.h (BSD-3-Clause):
 *   require power-of-two alignment and round down only after checked addition.
 * - LAPACK INSTALL/sroundup_lwork.f and INSTALL/droundup_lwork.f
 *   (BSD-3-Clause):
 *   use a safe saturated result rather than allowing a size to wrap.
 *
 * This header implements the techniques independently in C.  It contains no
 * floating-point, cryptographic, C++, Fortran, or external library dependency.
 */

#ifndef _NGX_PRIVATE_MATH_H_INCLUDED_
#define _NGX_PRIVATE_MATH_H_INCLUDED_


/* Reserve NGX_MAX_SIZE_T_VALUE as an unallocatable overflow sentinel. */
#define NGX_PRIV_MATH_SIZE_MAX  ((size_t) NGX_MAX_SIZE_T_VALUE - 1)

#if !defined(NGX_PRIV_MATH_NO_BUILTIN)                                      \
    && ((defined(__GNUC__) && __GNUC__ >= 5) || defined(__clang__))
#define NGX_PRIV_MATH_HAVE_OVERFLOW  1
#define NGX_PRIV_MATH_UNLIKELY(c)    __builtin_expect(!!(c), 0)
#else
#define NGX_PRIV_MATH_HAVE_OVERFLOW  0
#define NGX_PRIV_MATH_UNLIKELY(c)    (c)
#endif


static ngx_inline ngx_int_t
ngx_priv_math_valid_size(size_t size)
{
    return size <= NGX_PRIV_MATH_SIZE_MAX ? NGX_OK : NGX_ERROR;
}


static ngx_inline ngx_int_t
ngx_priv_math_add_size(size_t a, size_t b, size_t *result)
{
    if (NGX_PRIV_MATH_UNLIKELY(a > NGX_PRIV_MATH_SIZE_MAX
                               || b > NGX_PRIV_MATH_SIZE_MAX - a))
    {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = a + b;
    return NGX_OK;
}


static ngx_inline ngx_int_t
ngx_priv_math_mul_size(size_t a, size_t b, size_t *result)
{
#if (NGX_PRIV_MATH_HAVE_OVERFLOW)
    size_t  value;

    if (NGX_PRIV_MATH_UNLIKELY(__builtin_mul_overflow(a, b, &value)
                               || value > NGX_PRIV_MATH_SIZE_MAX))
    {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = value;
#else
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
#endif

    return NGX_OK;
}


static ngx_inline ngx_int_t
ngx_priv_math_add_uint(ngx_uint_t a, ngx_uint_t b, ngx_uint_t *result)
{
    ngx_uint_t  max;

    max = (ngx_uint_t) NGX_PRIV_MATH_SIZE_MAX;

    if (NGX_PRIV_MATH_UNLIKELY(a > max || b > max - a)) {
        *result = (ngx_uint_t) NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = a + b;
    return NGX_OK;
}


static ngx_inline size_t
ngx_priv_math_sat_add_size(size_t a, size_t b)
{
    size_t  result;

    if (ngx_priv_math_add_size(a, b, &result) != NGX_OK) {
        return NGX_MAX_SIZE_T_VALUE;
    }

    return result;
}


static ngx_inline size_t
ngx_priv_math_sat_mul_size(size_t a, size_t b)
{
    size_t  result;

    if (ngx_priv_math_mul_size(a, b, &result) != NGX_OK) {
        return NGX_MAX_SIZE_T_VALUE;
    }

    return result;
}


static ngx_inline size_t
ngx_priv_math_acc2_size(size_t total, size_t a, size_t b)
{
    return ngx_priv_math_sat_add_size(ngx_priv_math_sat_add_size(total, a), b);
}


static ngx_inline size_t
ngx_priv_math_acc3_size(size_t total, size_t a, size_t b, size_t c)
{
    return ngx_priv_math_sat_add_size(ngx_priv_math_acc2_size(total, a, b), c);
}


static ngx_inline size_t
ngx_priv_math_acc4_size(size_t total, size_t a, size_t b, size_t c, size_t d)
{
    return ngx_priv_math_sat_add_size(ngx_priv_math_acc3_size(total, a, b, c),
                                      d);
}


static ngx_inline size_t
ngx_priv_math_acc5_size(size_t total, size_t a, size_t b, size_t c, size_t d,
    size_t e)
{
    return ngx_priv_math_sat_add_size(
               ngx_priv_math_acc4_size(total, a, b, c, d), e);
}


static ngx_inline ngx_int_t
ngx_priv_math_align_up_size(size_t size, size_t alignment, size_t *result)
{
    size_t  adjusted;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    if (ngx_priv_math_add_size(size, alignment - 1, &adjusted) != NGX_OK) {
        *result = NGX_MAX_SIZE_T_VALUE;
        return NGX_ERROR;
    }

    *result = adjusted & ~(alignment - 1);
    return NGX_OK;
}


#endif /* _NGX_PRIVATE_MATH_H_INCLUDED_ */
