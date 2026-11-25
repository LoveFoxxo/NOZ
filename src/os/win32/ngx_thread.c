
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_err_t
ngx_create_thread(ngx_tid_t *tid,
    ngx_thread_value_t (__stdcall *func)(void *arg), void *arg, ngx_log_t *log)
{
    u_long     id;
    ngx_err_t  err;

    *tid = CreateThread(NULL, 0, func, arg, 0, &id);

    if (*tid != NULL) {
        return 0;
    }

    err = ngx_errno;
    return err;
}
