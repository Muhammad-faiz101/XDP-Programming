#include<linux/bpf.h>
#include<bpf/bpf_helpers.h>
#include<linux/if_ether.h>
#include<linux/ip.h>
#include<linux/in.h>
#include<bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

struct {
    __uint(type, BPF_MAP_TYPE_QUEUE);
    __uint(max_entries, 10);
    // __type(key, 0);
    __type(value, __u32);
} queuemap SEC(".maps");

SEC("xdp")
int ipv4_queue(struct xdp_md *cont)
{
    void *data = (void*)(long) cont -> data;
    void *data_end = (void*)(long) cont-> data_end;

    struct ethhdr *eth = data;
    if ((void*)(eth + 1)>data_end)
        return XDP_PASS;

    if (bpf_ntohs(eth -> h_proto) != ETH_P_IP)
        return XDP_DROP;

    struct iphdr *iph = (void *)(eth +1);
    if ((void*)(iph +1) > data_end)
        return XDP_PASS;

    __u32 sip=iph -> saddr; //s ip to be pushed into the stack

// Capture the return value of the push
long ret = bpf_map_push_elem(&queuemap, &sip, BPF_ANY);     
// long bpf_map_push_elem(struct bpf_map *map, const void *value, u64 flags)

// ONLY print if it successfully entered the queue (ret == 0)
if (ret == 0) {
    bpf_printk("Successfully queued IP: %pI4\n", &sip);
} else if (ret == -7) { // -E2BIG
    // bpf_printk("Queue is full, dropping log!\n");
}
    return XDP_PASS;
        
}
// to pop out elements on terminal:
//  sudo bpftool map pop id ...
