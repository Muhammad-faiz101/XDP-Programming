#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

// 1. CHANGE MAP TYPE TO STACK
struct {
    __uint(type, BPF_MAP_TYPE_STACK); 
    __uint(max_entries, 10);
    __type(value, __u32);
} stackmap SEC(".maps");

/* ---------------- PRODUCER (XDP) ---------------- */
SEC("xdp")
int ipv4_stack(struct xdp_md *cont)
{
    void *data = (void*)(long) cont -> data;
    void *data_end = (void*)(long) cont-> data_end;

    struct ethhdr *eth = data;
    if ((void*)(eth + 1) > data_end)
        return XDP_PASS;

    if (bpf_ntohs(eth -> h_proto) != ETH_P_IP)
        return XDP_DROP;

    struct iphdr *iph = (void *)(eth + 1);
    if ((void*)(iph + 1) > data_end)
        return XDP_PASS;

    __u32 sip = iph -> saddr;

    // Pushing works exactly the same way
    long ret = bpf_map_push_elem(&stackmap, &sip, BPF_ANY);     
    if (ret == 0) {
        bpf_printk("STACK PRODUCER -> Pushed IP: %pI4\n", &sip);
    }
    return XDP_PASS;
}

// /* ---------------- CONSUMER (Kprobe) ---------------- */
// SEC("kprobe/sys_clone") 
// int dequeue_packet(void *ctx)
// {
//     __u32 popped_ip = 0;

//     // 2. USE THE SAME CHOSEN POP HELPER
//     // Even though it says "pop_elem", the kernel knows this is a Stack map 
//     // and will automatically pull the LAST (newest) item added instead of the oldest.
//     long ret = bpf_map_pop_elem(&stackmap, &popped_ip);

//     if (ret == 0) {
//         bpf_printk("STACK CONSUMER -> Popped Newest IP: %pI4\n", &popped_ip);
//     }

//     return 0;
// }