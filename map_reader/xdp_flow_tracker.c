#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct flow_key {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240); // Increased slightly to handle all network traffic safely
    __type(key, struct flow_key);
    __type(value, __u64);
} flow_counter SEC(".maps");

SEC("xdp")
int xdp_packet_parser(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return XDP_PASS;
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP) return XDP_PASS;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) return XDP_PASS;

    struct flow_key key = {};
    key.saddr = ip->saddr;
    key.daddr = ip->daddr;
    key.sport = 0;
    key.dport = 0;

    void *trans = (void *)ip + (ip->ihl * 4);

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = trans;
        if ((void *)(tcp + 1) > data_end) return XDP_PASS;
        key.sport = tcp->source;
        key.dport = tcp->dest;
    } else if (ip->protocol == IPPROTO_UDP) {
        struct udphdr *udp = trans;
        if ((void *)(udp + 1) > data_end) return XDP_PASS;
        key.sport = udp->source;
        key.dport = udp->dest;
    }

    if (key.sport != 0 && key.dport != 0) {
        __u64 *counter = bpf_map_lookup_elem(&flow_counter, &key);
        if (counter) {
            __sync_fetch_and_add(counter, 1);
        } else {
            __u64 initial_value = 1;
            bpf_map_update_elem(&flow_counter, &key, &initial_value, BPF_ANY);
        }
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";