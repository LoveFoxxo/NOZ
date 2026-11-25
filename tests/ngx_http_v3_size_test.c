
/* Direct QPACK size-oracle tests, linked against the real nginx objects. */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_v3_encode.h>

#include <stdio.h>


/* Test-only leaf dependency; the production ngx_string.o is not replaced. */
void
ngx_strlow(u_char *dst, u_char *src, size_t n)
{
    while (n) {
        *dst = ngx_tolower(*src);
        dst++;
        src++;
        n--;
    }
}


#define TEST(c)                                                              \
    do {                                                                     \
        if (!(c)) {                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);           \
            return 1;                                                        \
        }                                                                    \
    } while (0)


int
main(void)
{
    u_char    *end;
    u_char     buffer[64];
    uintptr_t  size;
    ngx_str_t  name, value;

    name.data = (u_char *) "x";
    value.data = (u_char *) "y";

    name.len = 1;
    value.len = 1;

    size = ngx_http_v3_encode_field_l(NULL, &name, &value);
    TEST(size == 4);

    end = (u_char *) ngx_http_v3_encode_field_l(buffer, &name, &value);
    TEST((size_t) (end - buffer) == size);

    name.len = NGX_MAX_SIZE_T_VALUE;
    value.len = 1;
    size = ngx_http_v3_encode_field_l(NULL, &name, &value);
    TEST(size == (uintptr_t) NGX_MAX_SIZE_T_VALUE);

    name.len = 1;
    value.len = NGX_MAX_SIZE_T_VALUE;
    size = ngx_http_v3_encode_field_l(NULL, &name, &value);
    TEST(size == (uintptr_t) NGX_MAX_SIZE_T_VALUE);

    size = ngx_http_v3_encode_field_lri(NULL, 0, 1, NULL,
                                        NGX_MAX_SIZE_T_VALUE);
    TEST(size == (uintptr_t) NGX_MAX_SIZE_T_VALUE);

    size = ngx_http_v3_encode_field_lri(NULL, 0, 1, NULL, 1);
    TEST(size == 3);

    size = ngx_http_v3_encode_field_section_prefix(NULL, 0, 0, 0);
    TEST(size == 2);

    printf("ngx_http_v3_size_test: all checks passed\n");
    return 0;
}
