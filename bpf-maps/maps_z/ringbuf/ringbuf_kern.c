// ringbuf_kern.c

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "event.h"

/*=========================================================
 * Ring Buffer Map
 *
 * Userspace will read events from this buffer.
 *========================================================*/
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);

    /* 256 KB ring buffer */
    __uint(max_entries, 256 * 1024);

} events SEC(".maps");

/*=========================================================
 * XDP Program
 *========================================================*/
SEC("xdp")
int ringbuf_demo(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data     = (void *)(long)ctx->data;

    /*---------------- Ethernet ----------------*/
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    /*---------------- IPv4 ----------------*/
    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    /*---------------- Reserve Ring Buffer Event ----------------*/
    struct event *e;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);

    if (!e)
        return XDP_PASS;

    /*---------------- Fill Common Fields ----------------*/
    e->src_ip  = iph->saddr;
    e->dst_ip  = iph->daddr;
    e->protocol = iph->protocol;
    e->pkt_len  = bpf_ntohs(iph->tot_len);

    e->src_port = 0;
    e->dst_port = 0;

    /*---------------- TCP ----------------*/
    if (iph->protocol == IPPROTO_TCP) {

        struct tcphdr *tcph =
            (void *)iph + (iph->ihl * 4);

        if ((void *)(tcph + 1) > data_end) {
            bpf_ringbuf_discard(e, 0);
            return XDP_PASS;
        }

        e->src_port = bpf_ntohs(tcph->source);
        e->dst_port = bpf_ntohs(tcph->dest);
    }

    /*---------------- UDP ----------------*/
    else if (iph->protocol == IPPROTO_UDP) {

        struct udphdr *udph =
            (void *)iph + (iph->ihl * 4);

        if ((void *)(udph + 1) > data_end) {
            bpf_ringbuf_discard(e, 0);
            return XDP_PASS;
        }

        e->src_port = bpf_ntohs(udph->source);
        e->dst_port = bpf_ntohs(udph->dest);
    }

    /*---------------- Submit Event ----------------*/
    bpf_ringbuf_submit(e, 0);

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";