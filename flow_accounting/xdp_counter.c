#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

// 1. Expanded the key to hold both Source and Destination
struct flow_key {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, struct flow_key);
    __type(value, __u64);
} flow_counter SEC(".maps");

SEC("xdp")
int xdp_packet_parser(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* ================= Ethernet ================= */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    /* ================= IPv4 ================= */
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    struct flow_key key = {};
    // Grab both Source and Destination IPs
    key.saddr = ip->saddr;
    key.daddr = ip->daddr;
    key.sport = 0;
    key.dport = 0;

    /* ================= Transport Layer ================= */
    void *trans = (void *)ip + (ip->ihl * 4);

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = trans;
        if ((void *)(tcp + 1) > data_end)
            return XDP_PASS;
        
        // Grab both Source and Destination TCP Ports
        key.sport = tcp->source;
        key.dport = tcp->dest;
        
    } else if (ip->protocol == IPPROTO_UDP) {
        struct udphdr *udp = trans;
        if ((void *)(udp + 1) > data_end)
            return XDP_PASS;
            
        // Grab both Source and Destination UDP Ports
        key.sport = udp->source;
        key.dport = udp->dest;
    }

    /* ================= Update & Print ================= */
    // Only track if it has a valid port (TCP/UDP)
    if (key.sport != 0 && key.dport != 0) {
        __u64 *counter;
        __u64 current_count = 1;

        counter = bpf_map_lookup_elem(&flow_counter, &key);
        if (counter) {
            __sync_fetch_and_add(counter, 1);
            current_count = *counter + 1;
        } else {
            __u64 initial_value = 1;
            bpf_map_update_elem(&flow_counter, &key, &initial_value, BPF_ANY);
        }

        // Convert network byte order to host byte order for printing
        __u32 src = bpf_ntohl(key.saddr);
        __u32 dst = bpf_ntohl(key.daddr);
        __u16 sport = bpf_ntohs(key.sport);
        __u16 dport = bpf_ntohs(key.dport);

        // Print the full flow cleanly 
        // (Note: Modern kernels handle this many print arguments flawlessly)
        bpf_printk("[XDP FLOW] SRC: %d.%d.%d.%d:%d | DST: %d.%d.%d.%d:%d | CNT: %llu",
                    (src >> 24) & 0xff, (src >> 16) & 0xff, (src >> 8) & 0xff, src & 0xff, sport,
                    (dst >> 24) & 0xff, (dst >> 16) & 0xff, (dst >> 8) & 0xff, dst & 0xff, dport,
                    current_count);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";