#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct packet_event {
    __u32 src_ip;
    __u16 dst_port;
    __u16 pad;      // Keep structure aligned
};

struct {
    __uint(type, BPF_MAP_TYPE_QUEUE);
    __uint(max_entries, 100);
    __type(value, struct packet_event);
} packet_queue SEC(".maps");

SEC("xdp")
int xdp_queue_demo(struct xdp_md *ctx)
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
    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    if (iph->protocol != IPPROTO_TCP)
        return XDP_PASS;

    /* TCP */
    struct tcphdr *tcph = (void *)iph + (iph->ihl * 4);

    if ((void *)(tcph + 1) > data_end)
        return XDP_PASS;

    /* Create event */
    struct packet_event event = {};

    event.src_ip = iph->saddr;
    event.dst_port = bpf_ntohs(tcph->dest);

    /* Push into queue */
    long ret = bpf_map_push_elem(&packet_queue, &event, 0);

    if (ret == 0)
        bpf_printk("Queued packet: dst_port=%d", event.dst_port);
    
    else
        bpf_printk("Queue is full!");

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";