#include<linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>

struct{
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries,1000);
    __type(key,__u32);
    __type(value,__u64);

} map_hash_counters SEC(".maps");

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

    __u32 src_addr=ip->saddr;
   
    __u64 *count;
    // passsing map and key as parameter and getting value of key as a return
    count =bpf_map_lookup_elem(&map_hash_counters,&src_addr);

    if(count)
    {
        (*count)++;
        
    }
    else
    {
        __u64 initial_value=1;

        bpf_map_update_elem(&map_hash_counters,&src_addr,&initial_value,BPF_ANY);
    }

    return XDP_PASS
}
char LICENSE[] SEC("license")="GPL";