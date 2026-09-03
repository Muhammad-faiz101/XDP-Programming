#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define IP_HDR_LEN  20
#define TCP_HDR_LEN 20

/*
 * HTTP response.
 *
 * Content-Length refers only to the HTML body.
 */
#define HTTP_RESP \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 48\r\n" \
    "Connection: close\r\n" \
    "\r\n" \
    "<html><body>hi welcome to website </body></html>"

/*
 * Keep the response length automatically synchronized
 * with the actual string.
 */
#define RESP_LEN (sizeof(HTTP_RESP) - 1)


static __always_inline __u16 csum_fold_u32(__u32 sum)
{
    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);

    return (__u16)~sum;
}


SEC("xdp")
int xdp_http_injector(struct xdp_md *ctx)
{
    void *data;
    void *data_end;

    struct ethhdr *eth;
    struct iphdr *iph;
    struct tcphdr *tcph;

    /*
     * ------------------------------------------------------------
     * 1. Initial packet pointers
     * ------------------------------------------------------------
     */

    data = (void *)(long)ctx->data;
    data_end = (void *)(long)ctx->data_end;


    /*
     * ------------------------------------------------------------
     * 2. Ethernet
     * ------------------------------------------------------------
     */

    eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 3. IPv4
     * ------------------------------------------------------------
     */

    iph = (void *)(eth + 1);

    /*
     * Prove the fixed 20-byte IPv4 header exists.
     */
    if ((void *)((__u8 *)iph + IP_HDR_LEN) > data_end)
        return XDP_PASS;


    /*
     * Only IPv4 packets with no IP options.
     *
     * IHL = 5 -> 20 bytes.
     */
    if (iph->ihl != 5)
        return XDP_PASS;


    /*
     * Only TCP.
     */
    if (iph->protocol != IPPROTO_TCP)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 4. IP total length
     * ------------------------------------------------------------
     */

    __u32 ip_total_len;

    ip_total_len = bpf_ntohs(iph->tot_len);

    if (ip_total_len < IP_HDR_LEN + TCP_HDR_LEN)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 5. TCP
     * ------------------------------------------------------------
     *
     * TCP begins at:
     *
     *     Ethernet 14
     *     IPv4     20
     *     ----------------
     *              offset 34
     */

    tcph = (void *)((__u8 *)iph + IP_HDR_LEN);


    /*
     * IMPORTANT:
     *
     * Prove the fixed 20-byte TCP base header exists BEFORE
     * accessing tcph->doff.
     */
    if ((void *)((__u8 *)tcph + TCP_HDR_LEN) > data_end)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 6. Target TCP port 80
     * ------------------------------------------------------------
     */

    if (tcph->dest != bpf_htons(80))
        return XDP_PASS;


    /*
     * Only established/data packets.
     */
    if (!tcph->ack)
        return XDP_PASS;

    if (tcph->syn)
        return XDP_PASS;

    if (tcph->rst)
        return XDP_PASS;

    if (tcph->fin)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 7. Incoming TCP header length
     * ------------------------------------------------------------
     *
     * At this point the verifier already knows that the
     * first 20 bytes of TCP exist, so accessing doff is safe.
     */

    __u32 incoming_tcp_hdr_len;

    incoming_tcp_hdr_len = tcph->doff * 4;


    if (incoming_tcp_hdr_len < TCP_HDR_LEN)
        return XDP_PASS;


    /*
     * Maximum TCP header is 60 bytes.
     */
    if (incoming_tcp_hdr_len > 60)
        return XDP_PASS;


    /*
     * Make sure the complete TCP header exists.
     */
    if ((void *)((__u8 *)tcph + incoming_tcp_hdr_len) > data_end)
        return XDP_PASS;


    /*
     * Make sure TCP header fits inside the IP packet.
     */
    if (IP_HDR_LEN + incoming_tcp_hdr_len > ip_total_len)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 8. Calculate original payload length
     * ------------------------------------------------------------
     */

    __u32 payload_len;

    payload_len =
        ip_total_len -
        IP_HDR_LEN -
        incoming_tcp_hdr_len;


    /*
     * We only modify packets that contain payload.
     */
    if (payload_len == 0)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 9. Resize packet
     * ------------------------------------------------------------
     *
     * New packet:
     *
     * Ethernet
     * IPv4 20
     * TCP  20
     * HTTP response
     */

    int delta;

    delta =
        (int)(TCP_HDR_LEN + RESP_LEN) -
        (int)(incoming_tcp_hdr_len + payload_len);


    if (bpf_xdp_adjust_tail(ctx, delta) < 0)
        return XDP_PASS;


    /*
     * ------------------------------------------------------------
     * 10. RE-READ pointers after adjust_tail()
     * ------------------------------------------------------------
     */

    data = (void *)(long)ctx->data;
    data_end = (void *)(long)ctx->data_end;


    /*
     * Ethernet
     */
    eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;


    /*
     * IPv4
     */
    iph = (void *)(eth + 1);

    if ((void *)((__u8 *)iph + IP_HDR_LEN) > data_end)
        return XDP_DROP;


    /*
     * We still require no IPv4 options.
     */
    if (iph->ihl != 5)
        return XDP_PASS;


    /*
     * TCP
     */
    tcph = (void *)((__u8 *)iph + IP_HDR_LEN);

    /*
     * Fixed 20-byte TCP header.
     */
    if ((void *)((__u8 *)tcph + TCP_HDR_LEN) > data_end)
        return XDP_DROP;


    /*
     * Complete TCP header + response must exist.
     */
    if ((void *)((__u8 *)tcph +
                 TCP_HDR_LEN +
                 RESP_LEN) > data_end)
        return XDP_DROP;


    /*
     * ------------------------------------------------------------
     * 11. Swap Ethernet addresses
     * ------------------------------------------------------------
     */

    __u8 tmp_mac[ETH_ALEN];

    __builtin_memcpy(
        tmp_mac,
        eth->h_source,
        ETH_ALEN
    );

    __builtin_memcpy(
        eth->h_source,
        eth->h_dest,
        ETH_ALEN
    );

    __builtin_memcpy(
        eth->h_dest,
        tmp_mac,
        ETH_ALEN
    );


    /*
     * ------------------------------------------------------------
     * 12. Swap IPv4 addresses
     * ------------------------------------------------------------
     */

    __be32 tmp_ip;

    tmp_ip = iph->saddr;

    iph->saddr = iph->daddr;
    iph->daddr = tmp_ip;


    /*
     * ------------------------------------------------------------
     * 13. TCP SEQ/ACK
     * ------------------------------------------------------------
     */

    __be32 old_seq;
    __be32 old_ack;

    old_seq = tcph->seq;
    old_ack = tcph->ack_seq;


    /*
     * Response SEQ = request ACK
     */
    tcph->seq = old_ack;


    /*
     * Response ACK =
     * request SEQ + request payload length
     */
    tcph->ack_seq =
        bpf_htonl(
            bpf_ntohl(old_seq) + payload_len
        );


    /*
     * ------------------------------------------------------------
     * 14. Swap TCP ports
     * ------------------------------------------------------------
     */

    __be16 tmp_port;

    tmp_port = tcph->source;

    tcph->source = tcph->dest;
    tcph->dest = tmp_port;


    /*
     * ------------------------------------------------------------
     * 15. Force TCP header to 20 bytes
     * ------------------------------------------------------------
     */

    tcph->doff = 5;

    tcph->ack = 1;
    tcph->psh = 1;

    tcph->syn = 0;
    tcph->fin = 0;
    tcph->rst = 0;


    /*
     * ------------------------------------------------------------
     * 16. New IP total length
     * ------------------------------------------------------------
     */

    iph->tot_len =
        bpf_htons(
            IP_HDR_LEN +
            TCP_HDR_LEN +
            RESP_LEN
        );


    /*
     * ------------------------------------------------------------
     * 17. Write HTTP response
     * ------------------------------------------------------------
     */

    __u8 *payload_start;

    payload_start =
        (__u8 *)tcph + TCP_HDR_LEN;


    if ((void *)(payload_start + RESP_LEN) > data_end)
        return XDP_DROP;


    __builtin_memcpy(
        payload_start, // starting point of payload
        HTTP_RESP, // crafted response payload 
        RESP_LEN // no.of bytes - crafted response length
        
    );


    /*
     * ------------------------------------------------------------
     * 18. IPv4 checksum
     * ------------------------------------------------------------
     *
     * FIXED 20-BYTE ACCESS.
     *
     * This avoids the previous:
     *
     *     off=14 size=60
     *
     * verifier error.
     */

    iph->check = 0;

    __s64 ip_csum;

    ip_csum =
        bpf_csum_diff(
            NULL,
            0,
            (__be32 *)iph,
            IP_HDR_LEN,
            0
        );


    if (ip_csum < 0)
        return XDP_DROP;


    iph->check =
        csum_fold_u32(
            (__u32)ip_csum
        );


    /*
     * ------------------------------------------------------------
     * 19. TCP checksum
     * ------------------------------------------------------------
     */

    tcph->check = 0;


    /*
     * IPv4 pseudo-header:
     *
     *     source IP
     *     destination IP
     *     zero + TCP
     *     TCP length
     */

    __be32 pseudo_hdr[4];

    pseudo_hdr[0] = iph->saddr;
    pseudo_hdr[1] = iph->daddr;

    pseudo_hdr[2] =
        bpf_htonl(IPPROTO_TCP);

    pseudo_hdr[3] =
        bpf_htonl(
            TCP_HDR_LEN + RESP_LEN
        );


    /*
     * Pseudo-header checksum.
     */

    __s64 tcp_csum;

    tcp_csum =
        bpf_csum_diff(
            NULL,
            0,
            pseudo_hdr,
            sizeof(pseudo_hdr),
            0
        );


    if (tcp_csum < 0)
        return XDP_DROP;


    /*
     * TCP header + HTTP payload.
     */
    tcp_csum =
        bpf_csum_diff(
            NULL,
            0,
            (__be32 *)tcph,
            TCP_HDR_LEN + RESP_LEN,
            (__u32)tcp_csum
        );


    if (tcp_csum < 0)
        return XDP_DROP;


    tcph->check =
        csum_fold_u32(
            (__u32)tcp_csum
        );


    /*
     * ------------------------------------------------------------
     * 20. Send response back out the same interface
     * ------------------------------------------------------------
     */

    return XDP_TX;
}


char _license[] SEC("license") = "GPL";
