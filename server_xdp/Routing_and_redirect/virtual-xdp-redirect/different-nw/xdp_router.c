#include<linux/bpf.h>
#include<linux/if_ether.h>
#include<bpf/bpf_helpers.h>
#include<bpf/bpf_endian.h>

// map for redirecting out the target interface
struct{
    __uint(type,    BPF_MAP_TYPE_DEVMAP); 
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u32);
} tx_port SEC(".maps");

//map to hold next hop dest mac addr
struct{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, unsigned char[6]);
} mac_map SEC(".maps");

SEC("xdp")
int xdp_router(struct xdp_md *ctxt)
{
    void *data = (void*)(long) ctxt->data;
    void *data_end = (void*)(long) ctxt->data_end;

    struct ethhdr *eth = data;

    if ((void*)(eth+1)>data_end)
        return XDP_DROP;

        /* 1. Pass ARP to the kernel stack so gateways can answer ARP requests */
        if (eth->h_proto == bpf_htons(ETH_P_ARP))
            return XDP_PASS;
    
        /* 2. Drop everything that isn't IPv4 */
        if (eth->h_proto != bpf_htons(ETH_P_IP))
            return XDP_DROP;

    __u32 key =0;
    unsigned char *next_hop_mac = bpf_map_lookup_elem(&mac_map, &key);

    if(next_hop_mac)
    {
        /* 1. Set Source MAC = This interface's MAC (where packet leaves from) */
        /* 2. Set Dest MAC   = Next-Hop Target MAC (Device A or Device B)      */

        //copy old dest mac to new src mac
        __builtin_memcpy(eth->h_source, eth->h_dest, 6);
        //set new dest mac from map lookup
        __builtin_memcpy(eth->h_dest, next_hop_mac, 6);

        //
        return bpf_redirect_map(&tx_port, key, 0);
    }

    return XDP_PASS;

}
char _license[] SEC("license") = "GPL";
