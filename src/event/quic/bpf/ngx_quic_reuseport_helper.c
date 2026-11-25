#include <errno.h>
#include <linux/string.h>
#include <linux/udp.h>
#include <linux/bpf.h>
/*
 * the bpf_helpers.h is not included into linux-headers, only available
 * with kernel sources in "tools/lib/bpf/bpf_helpers.h" or in libbpf.
 */
#include <bpf/bpf_helpers.h>


#if !defined(SEC)
#define SEC(NAME)  __attribute__((section(NAME), used))
#endif


char _license[] SEC("license") = LICENSE;

/*****************************************************************************/

#define NGX_QUIC_PKT_LONG        0x80  /* header form */
#define NGX_QUIC_SERVER_CID_LEN  20


#define ngx_quic_parse_uint64(p)                                              \
    (((__u64)(p)[0] << 56) |                                                  \
     ((__u64)(p)[1] << 48) |                                                  \
     ((__u64)(p)[2] << 40) |                                                  \
     ((__u64)(p)[3] << 32) |                                                  \
     ((__u64)(p)[4] << 24) |                                                  \
     ((__u64)(p)[5] << 16) |                                                  \
     ((__u64)(p)[6] << 8)  |                                                  \
     ((__u64)(p)[7]))

#define ngx_quic_bpf_listen_key(worker)                                       \
    (((__u64) 0xFF << 56) | ((__u64) (worker) & 0xFFFFFFFFFFFFULL))


/*
 * actual map object is created by the "bpf" system call,
 * all pointers to this variable are replaced by the bpf loader
 */
struct {} ngx_quic_sockmap SEC(".maps");
struct {} ngx_quic_worker_counts SEC(".maps");


SEC(PROGNAME)
int ngx_quic_select_socket_by_dcid(struct sk_reuseport_md *ctx)
{
    int             rc, worker_idx;
    __u32           wc_key, *wc0, *wc1, n, m;
    __u64           key;
    size_t          offset;
    unsigned char  *start, *end, *dcid, byte, buf[NGX_QUIC_SERVER_CID_LEN];

    start = (unsigned char *) ctx->data;
    end = (unsigned char *) ctx->data_end;
    offset = sizeof(struct udphdr);

    if (start + offset >= end) {

        if (bpf_skb_load_bytes(ctx, offset, &byte, 1)) {
            goto failed;
        }

    } else {
        byte = start[offset];
    }

    if (byte & NGX_QUIC_PKT_LONG) {

        offset += 5;

        if (start + offset >= end) {

            if (bpf_skb_load_bytes(ctx, offset, &byte, 1)) {
                goto failed;
            }

        } else {
            byte = start[offset];
        }

        if (byte != NGX_QUIC_SERVER_CID_LEN) {
            goto new;
        }
    }

    offset++;

    if (start + offset + NGX_QUIC_SERVER_CID_LEN > end) {

        if (bpf_skb_load_bytes(ctx, offset, buf, NGX_QUIC_SERVER_CID_LEN)) {
            goto failed;
        }

        dcid = buf;

    } else {
        dcid = start + offset;
    }

    key = ngx_quic_parse_uint64(dcid);

    if ((key >> 56) == 0xFF) {
        goto new;
    }

    rc = bpf_sk_select_reuseport(ctx, &ngx_quic_sockmap, &key, 0);

    if (rc == 0) {
        return SK_PASS;
    }

    if (rc != -ENOENT) {
        return SK_DROP;
    }

new:

    wc_key = 0;
    wc0 = bpf_map_lookup_elem(&ngx_quic_worker_counts, &wc_key);
    if (wc0 == NULL) {
        return SK_DROP;
    }

    wc_key = 1;
    wc1 = bpf_map_lookup_elem(&ngx_quic_worker_counts, &wc_key);
    if (wc1 == NULL) {
        return SK_DROP;
    }

    n = *wc0 > *wc1 ? *wc0 : *wc1;

    if (n == 0) {
        return SK_DROP;
    }

    worker_idx = ctx->hash % n;
    key = ngx_quic_bpf_listen_key(worker_idx);

    rc = bpf_sk_select_reuseport(ctx, &ngx_quic_sockmap, &key, 0);

    if (rc == 0) {
        return SK_PASS;
    }

    if (rc != -ENOENT) {
        return SK_DROP;
    }

    m = *wc0 < *wc1 ? *wc0 : *wc1;

    if (m && m != n) {
        worker_idx = ctx->hash % m;
        key = ngx_quic_bpf_listen_key(worker_idx);

        rc = bpf_sk_select_reuseport(ctx, &ngx_quic_sockmap, &key, 0);

        if (rc == 0) {
            return SK_PASS;
        }

    }

    return SK_DROP;

failed:


    return SK_DROP;
}
