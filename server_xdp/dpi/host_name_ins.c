#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>


#define ENS7F0_IFINDEX 11
#define ENS7F1_IFINDEX 12

#define HTTP_PORT 80
#define MAX_HTTP_SCAN 256
#define MAX_HOST_LEN 63


SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* =====================================================
     * Ethernet
     * ===================================================== */

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    /*
     * Let ARP reach the normal network stack.
     */
    if (eth->h_proto == bpf_htons(ETH_P_ARP))
        return XDP_PASS;

    /*
     * This experiment handles IPv4 only.
     */
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_DROP;


    /* =====================================================
     * IPv4
     * ===================================================== */

    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_DROP;

    /*
     * Only TCP is relevant for HTTP.
     */
    if (iph->protocol != IPPROTO_TCP)
        goto forward;


    /*
     * IPv4 header length is variable.
     */
    __u32 ip_header_len = iph->ihl * 4;

    if (ip_header_len < sizeof(struct iphdr))
        return XDP_DROP;

    if ((void *)iph + ip_header_len > data_end)
        return XDP_DROP;


    /* =====================================================
     * TCP
     * ===================================================== */

    struct tcphdr *tcph =
        (void *)iph + ip_header_len;

    if ((void *)(tcph + 1) > data_end)
        return XDP_DROP;

    /*
     * TCP header length is variable.
     */
    __u32 tcp_header_len = tcph->doff * 4;

    if (tcp_header_len < sizeof(struct tcphdr))
        return XDP_DROP;

    if ((void *)tcph + tcp_header_len > data_end)
        return XDP_DROP;


    __u16 src_port = bpf_ntohs(tcph->source);
    __u16 dst_port = bpf_ntohs(tcph->dest);


/* =====================================================
 * HTTP DPI
 *
 * Only inspect A -> B traffic going to TCP port 80.
 * ===================================================== */

if (ctx->ingress_ifindex == ENS7F0_IFINDEX &&
    dst_port == HTTP_PORT) {

    unsigned char *payload =
        (void *)tcph + tcp_header_len;

    /*
     * We need at least one byte of TCP payload.
     */
    if ((void *)payload >= data_end)
        goto forward;

    /*
     * -------------------------------------------------
     * Check for HTTP request method
     * -------------------------------------------------
     *
     * We explicitly check that enough bytes exist
     * before accessing payload[0..4].
     */

    int is_http = 0;

    /*
     * GET 
     */
    if (payload + 4 <= (unsigned char *)data_end &&
        payload[0] == 'G' &&
        payload[1] == 'E' &&
        payload[2] == 'T' &&
        payload[3] == ' ') {

        is_http = 1;
    }

    /*
     * POST
     */
    if (payload + 5 <= (unsigned char *)data_end &&
        payload[0] == 'P' &&
        payload[1] == 'O' &&
        payload[2] == 'S' &&
        payload[3] == 'T' &&
        payload[4] == ' ') {

        is_http = 1;
    }

    /*
     * HEAD
     */
    if (payload + 5 <= (unsigned char *)data_end &&
        payload[0] == 'H' &&
        payload[1] == 'E' &&
        payload[2] == 'A' &&
        payload[3] == 'D' &&
        payload[4] == ' ') {

        is_http = 1;
    }


    /*
     * -------------------------------------------------
     * Search for "Host:"
     * -------------------------------------------------
     */

    if (is_http) {

        int host_position = -1;

#pragma unroll
        for (int i = 0; i < MAX_HTTP_SCAN - 5; i++) {

            /*
             * IMPORTANT:
             *
             * Prove to the verifier that all five
             * bytes payload[i] ... payload[i+4]
             * are inside the packet.
             */
            if (payload + i + 5 > (unsigned char *)data_end)
                break;

            if (payload[i]     == 'H' &&
                payload[i + 1] == 'o' &&
                payload[i + 2] == 's' &&
                payload[i + 3] == 't' &&
                payload[i + 4] == ':') {

                host_position = i;
                break;
            }
        }


        /*
         * -------------------------------------------------
         * Extract Host value
         * -------------------------------------------------
         */

        if (host_position >= 0) {

            int host_start = host_position + 5;

            /*
             * Skip spaces after "Host:"
             */
#pragma unroll
            for (int i = 0; i < 8; i++) {

                if (payload + host_start + 1 >
                    (unsigned char *)data_end)
                    break;

                if (payload[host_start] == ' ')
                    host_start++;
                else
                    break;
            }


            char host[MAX_HOST_LEN + 1] = {};

            int host_len = 0;


#pragma unroll
            for (int i = 0; i < MAX_HOST_LEN; i++) {

                /*
                 * Make sure the byte we want to read
                 * is inside the packet.
                 */
                if (payload + host_start + i + 1 >
                    (unsigned char *)data_end)
                    break;

                char c = payload[host_start + i];

                /*
                 * Host header ends at CRLF.
                 */
                if (c == '\r' || c == '\n')
                    break;

                host[i] = c;
                host_len++;
            }

            /*
             * Null terminate the string.
             */
            host[host_len] = '\0';

            bpf_printk(
                "HTTP Host: %s",
                host
            );
        }
    }
}

forward:

    /* =====================================================
     * A -> B / Internet
     * ===================================================== */

    if (ctx->ingress_ifindex == ENS7F0_IFINDEX) {

        /*
         * Destination = Laptop B
         */
        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xe9, 0xf9
            },
            ETH_ALEN
        );

        /*
         * Source = ens7f1 MAC
         */
        __builtin_memcpy(
            eth->h_source,
            (unsigned char[]){
                0xb4, 0x96, 0x91,
                0x12, 0x9d, 0x46
            },
            ETH_ALEN
        );

        return bpf_redirect(ENS7F1_IFINDEX, 0);
    }


    /* =====================================================
     * B / Internet -> A
     * ===================================================== */

    if (ctx->ingress_ifindex == ENS7F1_IFINDEX) {

        /*
         * Destination = Laptop A
         */
        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xde, 0xbb
            },
            ETH_ALEN
        );

        /*
         * Source = ens7f0 MAC
         */
        __builtin_memcpy(
            eth->h_source,
            (unsigned char[]){
                0xb4, 0x96, 0x91,
                0x12, 0x9d, 0x44
            },
            ETH_ALEN
        );

        return bpf_redirect(ENS7F0_IFINDEX, 0);
    }


    return XDP_PASS;
}


char LICENSE[] SEC("license") = "GPL";
