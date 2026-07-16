#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 10);
    __type(key, __u32);      // Source IP
    __type(value, __u64);    // Packet Counter
} ip_counter SEC(".maps");

SEC("xdp")
int xdp_ip_counter(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    __u32 src_ip = iph->saddr;

    __u64 *count = bpf_map_lookup_elem(&ip_counter, &src_ip);

    if (count) {
        (*count)++;
    } else {
        __u64 value = 1;

        bpf_map_update_elem(&ip_counter,
                            &src_ip,
                            &value,
                            BPF_ANY);
    }

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";