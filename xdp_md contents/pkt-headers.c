#include<linux/bpf.h>
#include<linux/if_ether.h>
#include<linux/ip.h>
#include <linux/ipv6.h> 
#include<linux/tcp.h>
#include <linux/udp.h>   
#include <linux/icmp.h>
#include<bpf/bpf_helpers.h>
#include<bpf/bpf_endian.h>
#include <linux/in.h>

SEC("xdp")

int pkt_Hcontents(struct xdp_md *context)
{
    void *data = (void *)(long) context->data; 
    void *data_end = (void*)(long) context->data_end;

    //ethernet header LAYER 2
    struct ethhdr *ether = data;
    if ((void*)(ether+1) > data_end) 
        return XDP_PASS;

    //ip header LAYER 3
    __u16 proto = bpf_ntohs(ether->h_proto);
    __u8 next_proto = 0;
    void *l4_header_start=NULL;

    if (proto == ETH_P_IP) //IPV4 traff
    {
        struct iphdr *ip = (void *)(ether + 1);
        if ((void*)(ip + 1) > data_end) return XDP_PASS;

        next_proto = ip->protocol;
        l4_header_start = (void *)(ip + 1);

        __u16 ip_packet_id = bpf_ntohs(ip->id);
        __u16 total_packet_bytes = bpf_ntohs(ip->tot_len);

        bpf_printk("IP Version: IPv%u | Packet ID: %u\n", ip->version, ip_packet_id);
        bpf_printk("Src IP: %pI4 -> Dest IP: %pI4 | Size: %u bytes | TTL: %u\n", 
            &ip->saddr, &ip->daddr, total_packet_bytes, ip->ttl);
    }
    else if (proto == ETH_P_IPV6) //ipv6 traff
    {
        struct ipv6hdr *ip6 = (void *)(ether + 1);
        if ((void*)(ip6 + 1) > data_end) 
            return XDP_PASS;

        next_proto = ip6->nexthdr;
        l4_header_start = (void *)(ip6 + 1);

        // Extract Traffic Class: high 4 bits of first byte + low 4 bits of second byte
        __u8 traffic_class = ((ip6->priority) << 4) | ((ip6->flow_lbl[0]) >> 4);
        __u16 payload_len = bpf_ntohs(ip6->payload_len);

        bpf_printk("IP Version: IPv%u | Next Header: %u\n", ip6->version, next_proto);
        bpf_printk("Traffic Class: 0x%02X | Payload Len: %u | Hop Limit: %u\n", 
            traffic_class, payload_len, ip6->hop_limit);
        bpf_printk("Src IPv6: %pI6 -> Dest IPv6: %pI6\n", &ip6->saddr, &ip6->daddr);

    } else {
        // Pass background traffic like ARP
        return XDP_PASS;
    }

    //tcp header LAYER 4
    if (next_proto == IPPROTO_TCP){ //for tcp traffic
        //tcp header
        struct tcphdr *tcp = l4_header_start;
        if ((void*)(tcp+1) > data_end)
            return XDP_PASS;

        //tcp ports
        bpf_printk("TCP Src PORT:%u -> Dest PORT:%u\n",  bpf_ntohs(tcp->source), bpf_ntohs(tcp->dest));
            // Convert sequence and total length to host byte order
        __u32 seq_num = bpf_ntohl(tcp->seq);
        //ip pkt size, ttl, seq#
        bpf_printk("TCP Seq Num: %u", seq_num);



        //connection flags
        if (tcp->syn) {
            bpf_printk("Connection Status: [SYN]\n");
        } else if (tcp->fin) {
            bpf_printk("Connection Status: [FIN]\n");
        } else {
            bpf_printk("Connection Status: [DATA/ACK]\n");
        }

        //payload snips(many of which are encrypted tho)
        __u32 tcp_headlen = tcp->doff *4;
        __u8 *payload = (__u8*)tcp + tcp_headlen;
        if ((void*)(payload+4) <= data_end)
        {
        bpf_printk("DATA CHUNKS: %c%c%c%c\n", 
            payload[0], payload[1], payload[2], payload[3]);
        // bpf_printk("---------\n");
        }
    }
    else if (next_proto == IPPROTO_UDP) {
        // --- UDP Parsing ---
        struct udphdr *udp = l4_header_start;
        if ((void*)(udp + 1) > data_end) return XDP_PASS;

        __u16 src_port = bpf_ntohs(udp->source);
        __u16 dst_port = bpf_ntohs(udp->dest);
        __u16 udp_len = bpf_ntohs(udp->len);

        bpf_printk("UDP PORT: %u -> %u | Length: %u bytes\n", src_port, dst_port, udp_len);

        // DNS Detection (DNS runs on Port 53)
        if (src_port == 53 || dst_port == 53) {
            bpf_printk("Application Detected: [DNS Query/Response]\n");
            
            // Raw DNS Payload Peek (Skips past 8-byte UDP header)
            __u8 *dns_payload = (__u8*)udp + 8;
            if ((void*)(dns_payload + 4) <= data_end) {
                // First 2 bytes of DNS payload are the Transaction ID
                __u16 dns_tx_id = (dns_payload[0] << 8) | dns_payload[1];
                bpf_printk("DNS Transaction ID: 0x%04X\n", dns_tx_id);
            }
        }

    } else if (next_proto == IPPROTO_ICMP || next_proto == IPPROTO_ICMPV6) {
        // --- ICMP Parsing (Ping requests/replies) ---
        struct icmphdr *icmp = l4_header_start;
        if ((void*)(icmp + 1) > data_end) return XDP_PASS;

        bpf_printk("ICMP Packet Detected | Type: %u | Code: %u\n", icmp->type, icmp->code);
        if (icmp->type == 8 || icmp->type == 128) {
            bpf_printk("ICMP Event: [PING REQUEST]\n");
        } else if (icmp->type == 0 || icmp->type == 129) {
            bpf_printk("ICMP Event: [PING REPLY]\n");
        }
    }

    bpf_printk("---------\n");
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";