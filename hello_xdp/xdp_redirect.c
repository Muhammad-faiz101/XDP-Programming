#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>

// BPF Map storing target egress interface indices
struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} tx_dev_map SEC(".maps");

SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // Redirect packet to target egress interface stored at index 0 of the map
    __u32 target_key = 0;
    return bpf_redirect_map(&tx_dev_map, target_key, 0);
}

char _license[] SEC("license") = "GPL";