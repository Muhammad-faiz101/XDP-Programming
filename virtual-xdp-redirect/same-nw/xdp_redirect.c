//subnets are not different
#include<linux/bpf.h>
#include<bpf/bpf_helpers.h>

struct{
    __uint(type, BPF_MAP_TYPE_DEVMAP); //devmap holds ifindices
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u32);
} tx_port SEC(".maps");

SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctxt)
{
    __u32 key =0; //key 0 will hold ifindex of other cable

    if(bpf_map_lookup_elem(&tx_port, &key)) //if map has a valid cable id at key 0
    {
        return bpf_redirect_map(&tx_port, key, 0); //redirect pkt out through the cable stored at key 0
    }


return XDP_PASS; // fallback

}
char _license[] SEC("license") = "GPL";