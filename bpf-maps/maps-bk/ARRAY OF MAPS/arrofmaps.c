// kernel c code with static initialization
#include<linux/bpf.h>
#include<linux/in.h>
#include<linux/if_ether.h>
#include<linux/ip.h>
#include<bpf/bpf_endian.h>
#include<bpf/bpf_helpers.h>

//inner map template
struct inner_map{
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32); //ipv4 add
    __type(value, __u32); //status (1: blocked)
    __uint(max_entries, 1000);
};

// declaration and static popluation of actual inn maps
struct inner_map blacklist_profile1 SEC(".maps") =
{}; //popluation via cli

struct inner_map blacklist_profile2 SEC(".maps") =
{};

//outer map
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
    __type(key, __u32); // profile: 1 or 2
    __uint(max_entries, 2);
    __array(values, struct inner_map); // values plural

} outer_map SEC(".maps") =
{
    //static linking (loader links these automatically)
    .values = {
        [0] = &blacklist_profile1,
        [1] = &blacklist_profile2
    },
};

SEC("xdp")
int xdp_firewall(struct xdp_md *cont)
{
    void *data = (void*)(long) cont -> data;
    void *data_end = (void*)(long) cont->data_end;

    //parsing ethernet hdr
    struct ethhdr *eth = data;
    if ((void*)(eth+1)>data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *iph = (void*) (eth+1);
    if ((void*)(iph+1) > data_end)
        return XDP_PASS;

    __u32 sip = iph -> saddr;
    //static target profile 1 for this check
    __u32 prof_indx =1;
    void * inner_map = bpf_map_lookup_elem(&outer_map , &prof_indx);
    if (!inner_map)
    {
        return XDP_PASS;
    }

    //lookup ip inside the linked inner map
    __u32 *blocked = bpf_map_lookup_elem(inner_map, &sip); //no &
    if (blocked)
        return XDP_DROP;

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

//# syntax: bpftool map update id <MAP_ID> key <BYTES> value <BYTES>
// sudo bpftool map update id 42 key 0x32 0x01 0xa8 0xc0 value 0x01 0x00 0x00 0x00