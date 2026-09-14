#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_abort_prog(struct xdp_md *ctx) {
    bpf_printk("XDP_ABORTED: Unhandled error condition encountered!\n");
    return XDP_ABORTED;
}

char _license[] SEC("license") = "GPL";