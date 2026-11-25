
/* Test-only HTTP module for allocation failure and size-overflow validation. */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_THREADS)
#include <ngx_thread_pool.h>
#endif


typedef struct {
    ngx_str_t  mode;
} ngx_http_safety_test_loc_conf_t;


static ngx_int_t ngx_http_safety_test_handler(ngx_http_request_t *r);
static void *ngx_http_safety_test_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_safety_test_directive(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);


static ngx_command_t  ngx_http_safety_test_commands[] = {

    { ngx_string("safety_test"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_safety_test_directive,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_safety_test_loc_conf_t, mode),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_safety_test_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_safety_test_create_loc_conf,  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_safety_test_module = {
    NGX_MODULE_V1,
    &ngx_http_safety_test_module_ctx,      /* module context */
    ngx_http_safety_test_commands,         /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_safety_test_text(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;

    static u_char  body[] = "safety-ok\n";

    r->headers_out.status = NGX_HTTP_OK;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_length_n = sizeof(body) - 1;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE || r->header_only) {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->pos = body;
    b->last = body + sizeof(body) - 1;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static ngx_int_t
ngx_http_safety_test_handler(ngx_http_request_t *r)
{
    ngx_pool_t                     *pool;
    ngx_array_t                    *array;
    ngx_list_t                     *list;
    ngx_table_elt_t                *h;
    ngx_http_safety_test_loc_conf_t  *conf;
#if (NGX_THREADS)
    ngx_thread_task_t              *task;
#endif

    conf = ngx_http_get_module_loc_conf(r, ngx_http_safety_test_module);

    if (conf->mode.len == 6
        && ngx_strncmp(conf->mode.data, "normal", 6) == 0)
    {
        return ngx_http_safety_test_text(r);
    }

    if (conf->mode.len == 10
        && ngx_strncmp(conf->mode.data, "direct-503", 10) == 0)
    {
        return ngx_http_allocation_failed(r);
    }

    if (conf->mode.len == 13
        && ngx_strncmp(conf->mode.data, "pool-overflow", 13) == 0)
    {
        pool = ngx_create_pool(NGX_MAX_SIZE_T_VALUE, r->connection->log);

        if (pool != NULL) {
            ngx_destroy_pool(pool);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return ngx_http_allocation_failed(r);
    }

    if (conf->mode.len == 14
        && ngx_strncmp(conf->mode.data, "array-overflow", 14) == 0)
    {
        array = ngx_array_create(r->pool, (ngx_uint_t) NGX_MAX_SIZE_T_VALUE,
                                 sizeof(ngx_str_t));

        if (array != NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return ngx_http_allocation_failed(r);
    }

    if (conf->mode.len == 13
        && ngx_strncmp(conf->mode.data, "list-overflow", 13) == 0)
    {
        list = ngx_list_create(r->pool, (ngx_uint_t) NGX_MAX_SIZE_T_VALUE,
                               sizeof(ngx_table_elt_t));

        if (list != NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return ngx_http_allocation_failed(r);
    }

    if (conf->mode.len == 15
        && ngx_strncmp(conf->mode.data, "thread-overflow", 15) == 0)
    {
#if (NGX_THREADS)
        task = ngx_thread_task_alloc(r->pool, NGX_MAX_SIZE_T_VALUE);

        if (task != NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return ngx_http_allocation_failed(r);
#else
        return NGX_HTTP_NOT_FOUND;
#endif
    }

    if (conf->mode.len == 15
        && ngx_strncmp(conf->mode.data, "header-overflow", 15) == 0)
    {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return ngx_http_allocation_failed(r);
        }

        h->hash = 1;
        ngx_str_set(&h->key, "X-Safety-Test");
        ngx_str_set(&h->value, "x");

        /* The encoder must reject this before dereferencing value.data. */
        h->value.len = NGX_MAX_SIZE_T_VALUE - 16;

        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = 0;

        return ngx_http_send_header(r);
    }

    return NGX_DECLINED;
}


static void *
ngx_http_safety_test_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_safety_test_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_safety_test_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static char *
ngx_http_safety_test_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                     *rv;
    ngx_http_core_loc_conf_t  *clcf;

    rv = ngx_conf_set_str_slot(cf, cmd, conf);
    if (rv != NGX_CONF_OK) {
        return rv;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_safety_test_handler;

    return NGX_CONF_OK;
}
