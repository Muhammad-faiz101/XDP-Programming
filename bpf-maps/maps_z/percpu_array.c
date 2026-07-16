
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY); //creates array map
    __uint(max_entries, 1); //
    __type(key, __u32);
    __type(value, __u64);
} counter_map SEC(".maps");

SEC("xdp")
int xdp_array_demo(struct xdp_md *ctx)
{
    __u32 key = 0;

    __u64 *counter = bpf_map_lookup_elem(&counter_map, &key);

    if (counter)
        (*counter)++;

    return XDP_PASS;
}