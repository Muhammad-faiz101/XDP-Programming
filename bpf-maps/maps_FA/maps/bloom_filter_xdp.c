// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

/* Bloom Filter Map */
struct {
    __uint(type, BPF_MAP_TYPE_BLOOM_FILTER);
    __uint(max_entries, 1024);
    __type(value, __u32);
} bloom_filter SEC(".maps");

SEC("xdp")
int xdp_bloom_demo(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* Ethernet header */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    /* Only IPv4 */
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    /* IPv4 header */
    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    __u32 src_ip = iph->saddr;

    /*
     * Bloom filter lookup.
     *
     * Return value:
     *   0  -> value may exist
     *  <0  -> value definitely does not exist
     */
    long ret = bpf_map_peek_elem(&bloom_filter, &src_ip);

    if (ret == 0) {
        bpf_printk("Bloom: %x possibly present\n",
                   bpf_ntohl(src_ip));
        return XDP_DROP;
    } else {
        bpf_printk("Bloom: %x definitely absent\n",
                   bpf_ntohl(src_ip));
    }

    return XDP_PASS;
}