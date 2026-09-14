#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>

SEC("xdp")
int xdp_tx_prog(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // 1. Mandatory verifier bounds check for Ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // 2. Swap Source and Destination MAC addresses
    unsigned char tmp_mac[ETH_ALEN];
    __builtin_memcpy(tmp_mac, eth->h_source, ETH_ALEN);
    __builtin_memcpy(eth->h_source, eth->h_dest, ETH_ALEN);
    __builtin_memcpy(eth->h_dest, tmp_mac, ETH_ALEN);

    bpf_printk("XDP_TX: Swapped MACs and bouncing packet out ingress interface\n");
    return XDP_TX;
}

char _license[] SEC("license") = "GPL";