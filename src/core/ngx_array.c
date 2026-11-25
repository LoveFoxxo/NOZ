
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }

    if (ngx_array_init(a, p, n, size) != NGX_OK) {
        return NULL;
    }

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    size_t       bytes;
    ngx_pool_t  *p;

    p = a->pool;

    if (ngx_priv_math_mul_size((size_t) a->nalloc, a->size, &bytes) == NGX_OK
        && (u_char *) a->elts + bytes == p->d.last)
    {
        p->d.last -= bytes;
    }

    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }
}


void *
ngx_array_push(ngx_array_t *a)
{
    return ngx_array_push_n(a, 1);
}


void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size, bytes, offset;
    ngx_uint_t   nalloc, required;
    ngx_pool_t  *p;

    if (a->size == 0 || a->nelts > a->nalloc) {
        return NULL;
    }

    if (ngx_priv_math_add_uint(a->nelts, n, &required) != NGX_OK
        || ngx_priv_math_mul_size((size_t) n, a->size, &size) != NGX_OK
        || ngx_priv_math_mul_size((size_t) a->nelts, a->size, &offset)
           != NGX_OK)
    {
        return NULL;
    }

    if (required > a->nalloc) {

        /* the array is full */

        p = a->pool;

        if (ngx_priv_math_mul_size((size_t) a->nalloc, a->size, &bytes)
            != NGX_OK)
        {
            return NULL;
        }

        if ((u_char *) a->elts + bytes == p->d.last
            && size <= (size_t) (p->d.end - p->d.last))
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += size;
            a->nalloc = required;

        } else {
            /* allocate a new array */

            nalloc = (n >= a->nalloc) ? n : a->nalloc;

            if (ngx_priv_math_add_uint(nalloc, nalloc, &nalloc) != NGX_OK
                || ngx_priv_math_mul_size((size_t) nalloc, a->size, &bytes)
                   != NGX_OK)
            {
                return NULL;
            }

            new = ngx_palloc(p, bytes);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, offset);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + offset;
    a->nelts = required;

    return elt;
}
