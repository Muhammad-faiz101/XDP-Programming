#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_drop_prog(struct xdp_md *ctx) {
    bpf_printk("XDP_DROP: Packet dropped at driver layer!\n");
    return XDP_DROP;
}

char _license[] SEC("license") = "GPL";