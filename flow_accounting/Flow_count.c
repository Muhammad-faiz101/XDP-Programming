#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <bpf/bpf_endian.h>


/* Counter Map */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} counter_map SEC(".maps");

/* Flow we want to monitor */
#define SRC_IP   bpf_htonl(0x976500DF)      
#define DST_IP   bpf_htonl(0xA012F12)     

#define SRC_PORT 443
#define DST_PORT 39752

SEC("xdp")
int count_flow(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* Ethernet */
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    /* IPv4 */
    struct iphdr *ip = (void *)(eth + 1);

    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    if (ip->protocol != IPPROTO_TCP)
        return XDP_PASS;

    /* TCP */
    struct tcphdr *tcp = (void *)ip + ip->ihl * 4;

    if ((void *)(tcp + 1) > data_end)
        return XDP_PASS;

    /* Compare Flow */
    if (ip->saddr == SRC_IP &&
        ip->daddr == DST_IP &&
        bpf_ntohs(tcp->source) == SRC_PORT &&
        bpf_ntohs(tcp->dest) == DST_PORT)
    {
        __u32 key = 0;

        __u64 *count = bpf_map_lookup_elem(&counter_map, &key);

        if (count)
            (*count)++;
    }

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";