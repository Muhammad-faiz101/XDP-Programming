#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>


/*
 * Inner Map Template
 *
 * Source IP ---> packet counter
 */

struct {

    __uint(type, BPF_MAP_TYPE_HASH);

    __uint(max_entries, 1024);

    __type(key, __u32);

    __type(value, __u64);


} inner_map SEC(".maps");



/*
 * Outer Hash of Maps
 *
 * Key ---> Inner Map
 *
 * Example:
 *
 * 0          ---> inner_map_A
 * 1          ---> inner_map_B
 *
 */

struct {

    __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);

    __uint(max_entries, 16);
    /*
     * Outer key
     */
    __type(key, __u32);
    /*
     * Inner map type
     */
    __array(values, typeof(inner_map));


} outer_map SEC(".maps");

SEC("xdp")
int hash_map_in_map_demo(struct xdp_md *ctx)
{

    void *data = (void *)(long)ctx->data;

    void *data_end =
        (void *)(long)ctx->data_end;



    struct ethhdr *eth = data;


    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;



    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;



    struct iphdr *iph =
        (void *)(eth + 1);



    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;



    /*
     * Source IP
     */
    __u32 src_ip = iph->saddr;



    /*
     * Select outer hash key
     *
     * For now:
     * all packets use key 100
     */

    __u32 outer_key = 100;



    /*
     * First lookup:
     *
     * outer hash
     * 
     * key ---> inner map
     */

    void *inner;


    inner =
    bpf_map_lookup_elem(
        &outer_map,
        &outer_key);



    if (!inner)
        return XDP_PASS;



    /*
     * Second lookup:
     *
     * inside inner map
     *
     * IP ---> counter
     */

    __u64 *count;


    count =
    bpf_map_lookup_elem(
        inner,
        &src_ip);



    if (count)
    {

        (*count)++;

    }

    else
    {

        __u64 value = 1;


        bpf_map_update_elem(
            inner,
            &src_ip,
            &value,
            BPF_ANY);

    }


    return XDP_PASS;
}



char LICENSE[] SEC("license") = "GPL";