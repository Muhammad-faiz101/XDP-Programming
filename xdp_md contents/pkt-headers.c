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

    //mac addresses
    bpf_printk("MAC Src:%02x:%02x:%02x  MAC Dest:%02x:%02x:%02x\n",
        ether->h_source[0], ether->h_source[1], ether->h_source[2],
        ether->h_dest[0], ether->h_dest[1], ether->h_dest[2]);



    //ip addresses and ports
    bpf_printk("Src IP:PORT=%pI4:%u -> Dest IP:PORT=%pI4:%u\n", 
        &ip->saddr, bpf_ntohs(tcp->source), 
        &ip->daddr, bpf_ntohs(tcp->dest));
        // Convert sequence and total length to host byte order
    __u32 seq_num = bpf_ntohl(tcp->seq);
    __u16 total_packet_bytes = bpf_ntohs(ip->tot_len);
    //ip pkt size, ttl, seq#
    bpf_printk("TCP Seq Num: %u", seq_num);
    bpf_printk("Packet Size: %u bytes | TTL: %u\n", total_packet_bytes, ip->ttl);
    
    __u16 ip_packet_id = bpf_ntohs(ip->id);//16-bit IP ID from network byte order to host byte order
    __u8 ip_version = ip->version; //extract the ip ver 

    bpf_printk("IP Version: IPv%u", ip_version);
    bpf_printk("IP Packet ID (Fragment Tracker): %u\n", ip_packet_id);



    //connection flags
    if (tcp->syn) {
        bpf_printk("Connection Status: [SYN]\n");
    } else if (tcp->fin) {
        bpf_printk("Connection Status: [FIN]\n");
    } else {
        bpf_printk("Connection Status: [DATA/ACK]\n");
    }

    //payload snips(many of which are encrypted tho)
    bpf_printk("DATA CHUNKS: %c%c%c%c\n", 
        payload[0], payload[1], payload[2], payload[3]);
    bpf_printk("---------\n");

    
    return XDP_PASS;
}
char _license[] SEC("license") = "GPL";