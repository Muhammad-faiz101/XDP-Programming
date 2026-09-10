#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

// 1. Define Key and Value structures
struct flow_key {
    __u32 src_ip;
    __u32 dst_ip;
    __u16 src_port;
    __u16 dst_port;
    __u8  protocol;
};

struct flow_value {
    __u64 packets;
};

// 2. Define the BPF Hash Map
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, struct flow_key);
    __type(value, struct flow_value);
} flow_counter SEC(".maps");

// 3. XDP Program Entry Point
SEC("xdp")
int xdp_flow_counter(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data     = (void *)(long)ctx->data;

    // A. Parse Ethernet Header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // Check if it's an IPv4 packet
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    // B. Parse IPv4 Header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    // Check if it's a TCP packet
    if (ip->protocol != IPPROTO_TCP)
        return XDP_PASS;

    bpf_printk("Tcp packet recieved !");
    // C. Parse TCP Header
    struct tcphdr *tcp = (void *)((__u32 *)ip + ip->ihl);
    if ((void *)(tcp + 1) > data_end)
        return XDP_PASS;

    // D. Build the Flow Key
    struct flow_key key = {};
    key.src_ip   = ip->saddr;
    key.dst_ip   = ip->daddr;
    key.src_port = tcp->source;
    key.dst_port = tcp->dest;
    key.protocol = ip->protocol;

    // E. Map Lookup and Update
    struct flow_value *value;
    value = bpf_map_lookup_elem(&flow_counter, &key);
    if (value) {
        // Increment existing count safely
        __sync_fetch_and_add(&value->packets, 1);
    } else {
        bpf_printk("new flow ----------");
        // Initialize new flow
        struct flow_value init_val = { .packets = 1 };

long ret = bpf_map_update_elem(&flow_counter, &key, &init_val, BPF_ANY);

if (ret == 0)
    bpf_printk("map update SUCCESS");
else
    bpf_printk("map update FAILED: %ld", ret);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";