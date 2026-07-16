
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>


struct {
    __uint(type , BPF_MAP_TYPE_HASH);
    __uint(max_entries , 30);
    __type(key , __u32);
    __type(value, __u64);
} packet_count SEC(".maps");

SEC("xdp")
int xdp_hash_counter(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    /* Only process IPv4 packets */
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    /* Parse IPv4 Header */
    struct iphdr *ip = (void *)(eth + 1);

    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    //key is source ip
    __u32 key = ip->saddr;

    //value is the counter value 
    __u64 *counter = bpf_map_lookup_elem(&packet_count , &key);

    if(counter) {
        //ip already exist
        (*counter)++;
    }
    else {
        __u64 value = 1;

        bpf_map_update_elem(&packet_count, &key, &value, BPF_ANY);
    }
    return XDP_PASS;
}
char LICENSE[] SEC("license") = "GPL";