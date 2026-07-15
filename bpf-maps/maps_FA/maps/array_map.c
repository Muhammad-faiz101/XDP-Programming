#include<linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>

struct{
    __uint(type,BPF_MAP_TYPE_ARRAY);
    __uint(max_entries,3);
    __type(key,__u32);
    __type(value,__u64);

} map_array_counters SEC(".maps");

SEC("xdp")
int protocol_counter(struct xdp_md *ctx)
{
    void  *data_end=(void *)(long )ctx->data_end;
    void *data=(void *)(long )ctx->data;

    struct ethhdr *eth=data;
    
    //checking if the packet is at least greater than ehternet header
    if((void* )(eth+1)>data_end)
    {
        return XDP_PASS;
    }
    // checking if the protocol is ipv4
    if (eth->h_proto!=__constant_htons(ETH_P_IP))
    {
        return XDP_PASS;
    }
    // checkign if the packet is greater than ip header
    struct iphdr * ip=(void *)(eth+1);

    if ((void *)(ip+1)> data_end)
    {
        return XDP_PASS;
    }

    __u32 key;
    // setting key value on the basis of transport layer protocol
    switch (ip->protocol)
    {
        case IPPROTO_TCP:// in linux/in.h
            key=0;
            break;
        
        case IPPROTO_UDP:
            key=1;
            break;

        case IPPROTO_ICMP://in linux/in.h
            key=2;
            break;

        default:
            return XDP_PASS;
    }

    __u64 *count;
    // passsing map and key as parameter and getting value of key as a return
    count =bpf_map_lookup_elem(&map_array_counters,&key);

    if(count)
    {
        (*count)++;
        
    }
    return XDP_PASS;

}
char LICENSE[] SEC("license")="GPL";