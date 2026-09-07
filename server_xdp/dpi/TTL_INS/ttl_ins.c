 
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/in.h>
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9


static __always_inline void rewrite_ttl(
    struct iphdr *iph,
    __u8 new_ttl)
{
    __u16 old_word;
    __u16 new_word;
    __u32 sum;

    /*
     * TTL and Protocol occupy the same 16-bit word:
     *
     *     TTL        Protocol
     *  8 bits        8 bits
     *
     * Example:
     * TTL=64, TCP=6 -> 0x4006
     */
    old_word = ((__u16)iph->ttl << 8) | iph->protocol;

    new_word = ((__u16)new_ttl << 8) | iph->protocol;

    /*
     * Incremental Internet checksum update:
     *
     * HC' = ~(~HC + ~m + m')
     *
     * HC  = old checksum
     * m   = old 16-bit word
     * m'  = new 16-bit word
     */

    sum = (~bpf_ntohs(iph->check) & 0xffff);
    sum += (~old_word & 0xffff);
    sum += new_word;
    bpf_printk("Old checksum: %x", iph->check);
    /*
     * Fold carries back into the lower 16 bits.
     */
    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);

    /*
     * Write the new IPv4 checksum.
     */
    iph->check = bpf_htons(~sum);
    bpf_printk("New checksum: %x", iph->check);
    
    bpf_printk("Old TTL: %d", iph->ttl);
    /*
     * Finally overwrite TTL.
     */
    iph->ttl = new_ttl;

    bpf_printk("New TTL %d",iph->ttl);
    bpf_printk("---------------Packet End---------------");
}


static __always_inline int xdp_redirect_common(
    struct xdp_md *ctx,
    __u8 ttl)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    /*
     * Allow ARP packets to pass normally.
     */
    if (eth->h_proto == bpf_htons(ETH_P_ARP))
        return XDP_PASS;

    /*
     * Only process IPv4 packets.
     */
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_DROP;

    /*
     * IPv4 header starts immediately after Ethernet header.
     */
    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_DROP;

    /*
     * Validate IPv4 header length.
     */
    if (iph->ihl < 5)
        return XDP_DROP;

    if ((void *)iph + (iph->ihl * 4) > data_end)
        return XDP_DROP;

    /*
     * Only rewrite TTL for TCP/IPv4 packets.
     */
    if (iph->protocol == IPPROTO_TCP) {
        rewrite_ttl(iph, ttl);
    }


    /*
     * A -> B
     */
    if (ctx->ingress_ifindex == ENS7F0_IFINDEX) {

        /* Rewrite destination to Laptop B */
        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xe9, 0xf9
            },
            ETH_ALEN
        );

        /* Rewrite source to server ens7f1 */
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


    /*
     * B -> A
     */
    if (ctx->ingress_ifindex == ENS7F1_IFINDEX) {

        /* Rewrite destination to Laptop A MAC */
        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xde, 0xbb
            },
            ETH_ALEN
        );

        /* Rewrite source to server ens7f0 MAC */
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


SEC("xdp/ttl3")
int xdp_ttl3(struct xdp_md *ctx)
{
    return xdp_redirect_common(ctx, 3);
}

SEC("xdp/ttl10")
int xdp_ttl10(struct xdp_md *ctx)
{
    return xdp_redirect_common(ctx,10);
}


char LICENSE[] SEC("license") = "GPL";

