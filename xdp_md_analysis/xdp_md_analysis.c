#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_parser(struct xdp_md *ctx)
{
    /* Packet boundaries */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* ---------------- Ethernet ---------------- */

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    __u16 eth_type = __builtin_bswap16(eth->h_proto);

    bpf_printk("======================================");
    bpf_printk("New Packet");
    bpf_printk("Packet Length : %llu bytes",
                (__u64)((char *)data_end - (char *)data));

    bpf_printk("Destination MAC : %02x:%02x:%02x:%02x:%02x:%02x",
                eth->h_dest[0], eth->h_dest[1], eth->h_dest[2],
                eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);

    bpf_printk("Source MAC      : %02x:%02x:%02x:%02x:%02x:%02x",
                eth->h_source[0], eth->h_source[1], eth->h_source[2],
                eth->h_source[3], eth->h_source[4], eth->h_source[5]);

    bpf_printk("EtherType       : 0x%04x", eth_type);

    /* Continue only for IPv4 */
    if (eth_type != ETH_P_IP)
        return XDP_PASS;

    /* ---------------- IPv4 ---------------- */

    struct iphdr *ip = (void *)(eth + 1);

    /* Minimum IPv4 header */
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    /* Complete IPv4 header (including options) */
    if ((void *)ip + ip->ihl * 4 > data_end)
        return XDP_PASS;

    bpf_printk("------------- IPv4 Header -------------");

    bpf_printk("Version         : %u", ip->version);
    bpf_printk("Header Length   : %u bytes", ip->ihl * 4);
    bpf_printk("Total Length    : %u",
                __builtin_bswap16(ip->tot_len));
    bpf_printk("TTL             : %u", ip->ttl);

    bpf_printk("Protocol Number : %u", ip->protocol);

    switch (ip->protocol) {
    case 1:
        bpf_printk("Protocol        : ICMP");
        break;
    case 6:
        bpf_printk("Protocol        : TCP");
        break;
    case 17:
        bpf_printk("Protocol        : UDP");
        break;
    default:
        bpf_printk("Protocol        : Other");
        break;
    }

    __u8 *src = (__u8 *)&ip->saddr;
    __u8 *dst = (__u8 *)&ip->daddr;

    bpf_printk("Source IP       : %u.%u.%u.%u",
                src[0], src[1], src[2], src[3]);

    bpf_printk("Destination IP  : %u.%u.%u.%u",
                dst[0], dst[1], dst[2], dst[3]);

    /* ---------------- TCP ---------------- */

    if (ip->protocol == IPPROTO_TCP) {

        struct tcphdr *tcp = (void *)ip + ip->ihl * 4;

        /* Minimum TCP header */
        if ((void *)(tcp + 1) > data_end)
            return XDP_PASS;

        /* Complete TCP header (including options) */
        if ((void *)tcp + tcp->doff * 4 > data_end)
            return XDP_PASS;

        bpf_printk("------------- TCP Header -------------");

        bpf_printk("Source Port      : %u",
                    __builtin_bswap16(tcp->source));

        bpf_printk("Destination Port : %u",
                    __builtin_bswap16(tcp->dest));

        bpf_printk("Sequence Number  : %u",
                    __builtin_bswap32(tcp->seq));

        bpf_printk("Ack Number       : %u",
                    __builtin_bswap32(tcp->ack_seq));

        /* ---------------- Payload ---------------- */

      /* -------- Payload -------- */

unsigned char *payload = (unsigned char *)tcp + tcp->doff * 4;

/* Verify that the first 16 payload bytes exist */
if (payload + 16 > (unsigned char *)data_end) {
    bpf_printk("Payload too small");
    return XDP_PASS;
}

int payload_len = (char *)data_end - (char *)payload;

bpf_printk("Payload Length   : %d bytes", payload_len);

char c0  = (payload[0]  >= 32 && payload[0]  <= 126) ? payload[0]  : '.';
char c1  = (payload[1]  >= 32 && payload[1]  <= 126) ? payload[1]  : '.';
char c2  = (payload[2]  >= 32 && payload[2]  <= 126) ? payload[2]  : '.';
char c3  = (payload[3]  >= 32 && payload[3]  <= 126) ? payload[3]  : '.';
char c4  = (payload[4]  >= 32 && payload[4]  <= 126) ? payload[4]  : '.';
char c5  = (payload[5]  >= 32 && payload[5]  <= 126) ? payload[5]  : '.';
char c6  = (payload[6]  >= 32 && payload[6]  <= 126) ? payload[6]  : '.';
char c7  = (payload[7]  >= 32 && payload[7]  <= 126) ? payload[7]  : '.';
char c8  = (payload[8]  >= 32 && payload[8]  <= 126) ? payload[8]  : '.';
char c9  = (payload[9]  >= 32 && payload[9]  <= 126) ? payload[9]  : '.';
char c10 = (payload[10] >= 32 && payload[10] <= 126) ? payload[10] : '.';
char c11 = (payload[11] >= 32 && payload[11] <= 126) ? payload[11] : '.';
char c12 = (payload[12] >= 32 && payload[12] <= 126) ? payload[12] : '.';
char c13 = (payload[13] >= 32 && payload[13] <= 126) ? payload[13] : '.';
char c14 = (payload[14] >= 32 && payload[14] <= 126) ? payload[14] : '.';
char c15 = (payload[15] >= 32 && payload[15] <= 126) ? payload[15] : '.';

bpf_printk("Payload ASCII:");
bpf_printk("%c%c%c%c%c%c%c%c",
            c0, c1, c2, c3, c4, c5, c6, c7);

bpf_printk("%c%c%c%c%c%c%c%c",
            c8, c9, c10, c11, c12, c13, c14, c15);

bpf_printk("--------------------------------------");
    }



    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";