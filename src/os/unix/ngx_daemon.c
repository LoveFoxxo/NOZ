
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_int_t
ngx_daemon(ngx_log_t *log)
{
    int  fd;

    switch (fork()) {
    case -1:
        return NGX_ERROR;

    case 0:
        break;

    default:
        exit(0);
    }

    ngx_parent = ngx_pid;
    ngx_pid = ngx_getpid();

    if (setsid() == -1) {
        return NGX_ERROR;
    }

    umask(0);

    fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        return NGX_ERROR;
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        return NGX_ERROR;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        return NGX_ERROR;
    }


    if (fd > STDERR_FILENO) {
        if (close(fd) == -1) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
