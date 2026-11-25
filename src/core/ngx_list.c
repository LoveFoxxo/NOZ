
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_list_t *
ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    ngx_list_t  *list;

    list = ngx_palloc(pool, sizeof(ngx_list_t));
    if (list == NULL) {
        return NULL;
    }

    if (ngx_list_init(list, pool, n, size) != NGX_OK) {
        return NULL;
    }

    return list;
}


void *
ngx_list_push(ngx_list_t *l)
{
    void             *elt;
    size_t            bytes, offset;
    ngx_uint_t        nelts;
    ngx_list_part_t  *last;

    if (l->size == 0 || l->nalloc == 0) {
        return NULL;
    }

    last = l->last;

    if (last->nelts > l->nalloc) {
        return NULL;
    }

    if (last->nelts == l->nalloc) {

        /* the last part is full, allocate a new list part */

        if (ngx_priv_math_mul_size((size_t) l->nalloc, l->size, &bytes)
            != NGX_OK)
        {
            return NULL;
        }

        last = ngx_palloc(l->pool, sizeof(ngx_list_part_t));
        if (last == NULL) {
            return NULL;
        }

        last->elts = ngx_palloc(l->pool, bytes);
        if (last->elts == NULL) {
            return NULL;
        }

        last->nelts = 0;
        last->next = NULL;

        l->last->next = last;
        l->last = last;
    }

    if (ngx_priv_math_mul_size((size_t) last->nelts, l->size, &offset)
           != NGX_OK
        || ngx_priv_math_add_uint(last->nelts, 1, &nelts) != NGX_OK)
    {
        return NULL;
    }

    elt = (char *) last->elts + offset;
    last->nelts = nelts;

    return elt;
}
