#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

// Ensure SEC macro is defined explicitly if bpf_helpers header missed it
#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif

struct mac_pair {
    unsigned char dst_mac[6];
    unsigned char src_mac[6];
};

struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u32);
} tx_port SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct mac_pair);
} mac_map SEC(".maps");

SEC("xdp")
int xdp_router(struct xdp_md *ctxt)
{
    void *data = (void *)(long)ctxt->data;
    void *data_end = (void *)(long)ctxt->data_end;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    if (eth->h_proto == bpf_htons(ETH_P_ARP))
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_DROP;

    __u32 key = 0;
    struct mac_pair *macs = bpf_map_lookup_elem(&mac_map, &key);

    if (macs) {
        __u32 *ifindex = bpf_map_lookup_elem(&tx_port, &key);

        if (ifindex) {
            bpf_printk("tx_port[%u] = %u", key, *ifindex);
        }

        bpf_printk("SRC %02x:%02x:%02x:%02x:%02x:%02x",
                macs->src_mac[0], macs->src_mac[1],
                macs->src_mac[2], macs->src_mac[3],
                macs->src_mac[4], macs->src_mac[5]);

        bpf_printk("DST %02x:%02x:%02x:%02x:%02x:%02x",
                macs->dst_mac[0], macs->dst_mac[1],
                macs->dst_mac[2], macs->dst_mac[3],
                macs->dst_mac[4], macs->dst_mac[5]);

        __builtin_memcpy(eth->h_source, macs->src_mac, 6);
        __builtin_memcpy(eth->h_dest, macs->dst_mac, 6);

        return bpf_redirect_map(&tx_port, key, 0);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";