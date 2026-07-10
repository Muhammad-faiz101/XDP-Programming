#include<linux/bpf.h>
#include<linux/if_ether.h>
#include<linux/ip.h>
#include <linux/ipv6.h> 
#include<linux/tcp.h>
#include <linux/udp.h>   

#include<bpf/bpf_helpers.h>
#include<bpf/bpf_endian.h>
#include <linux/in.h>

char LICENSE[] SEC("license") = "GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 200);
    __type(key, __u32);
    __type(value, __u64);
} hashmap SEC(".maps");

SEC("xdp")
int basic_firewall(struct xdp_md *context){
    //start and end of pkt pointers thru xdpmd
    void *data=(void*)(long) context-> data;
    void *data_end=(void*)(long) context -> data_end;

    //ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth+1)>data_end) //boundary check
        return XDP_PASS;
        
    //ipv4 traff
    if (bpf_ntoh(eth -> h_proto != ETH_P_IP)) 
        return XDP_PASS;

    //ip header
    struct iphdr *iph = (void *)(eth +1);
    if ((void*)(iph +1) > data_end)
        return XDP_PASS;

    __u32 srcip= iph -> saddr;
    __u64 *drop_count;

    //is src ip inside map
    drop_count= bpf_map_lookup_elem(&hashmap, &srcip);

    if (drop_count){
        __sync_fetch_and_add(drop_count, 1); //ip blocked, increment

        if (*drop_count> 100){
            bpf_map_delete_element(&hashmap, &srcip);
        }
        return XDP_DROP;
    }
    return XDP_PASS;

}

/*
    incoming ip -> hash func -> result in hash index -> place ip in 
    that slot -> check new ip if exist in map -> if true -> increment
    pkt count.
    if pkt drp count > 100 -> delete element
*/