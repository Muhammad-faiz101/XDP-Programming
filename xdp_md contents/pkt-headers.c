#include<linux/bpf.h>
#include<linux/if_ether.h>
#include<linux/ip.h>
#include<linux/tcp.h>
#include<bpf/bpf_helpers.h>
#include<bpf/bpf_endian.h>
#include <linux/in.h>

SEC("xdp")

int pkt_Hcontents(struct xdp_md *context)
{
    void *data = (void *)(long) context->data; 
    void *data_end = (void*)(long) context->data_end;

    //ethernet header
    struct ethhdr *ether = data;
    if ((void*)(ether+1) > data_end) 
        return XDP_PASS;

    if (bpf_ntohs(ether->h_proto) != ETH_P_IP) //filter out non-IPV4 traff
        return XDP_PASS;

    //ip header
    struct iphdr *ip = (void *)(ether+1);
    if ((void*)(ip + 1) > data_end)
        return XDP_PASS;

    if (ip->protocol != IPPROTO_TCP) //filtering out non-tcp traff
        return XDP_PASS;
    
    //tcp header
    struct tcphdr *tcp = (void*)(ip+1);
    if ((void*)(tcp+1) > data_end)
        return XDP_PASS;
    

    //data payload starting point
    __u32 tcp_headlen = tcp->doff *4;
    __u8 *payload = (__u8*)tcp + tcp_headlen;

    if((void*)(payload +4) > data_end) //atleast 4bytes if msg to read
        return XDP_PASS;


    bpf_printk("MAC Src:%02x:%02x:%02x  MAC Dest:%02x:%02x:%02x",
        ether->h_source[0], ether->h_source[1], ether->h_source[2],
        ether->h_dest[0], ether->h_dest[1], ether->h_dest[2]);

    bpf_printk("Src IP:PORT=%pI4:%u -> Dest IP:PORT=%pI4:%u\n", 
        &ip->saddr, bpf_ntohs(tcp->source), 
        &ip->daddr, bpf_ntohs(tcp->dest));

    bpf_printk("DATA CHUNKS: %c%c%c%c\n", 
        payload[0], payload[1], payload[2], payload[3]);
    // bpf_printk()
    
    
    return XDP_PASS;
}
char _license[] SEC("license") = "GPL";