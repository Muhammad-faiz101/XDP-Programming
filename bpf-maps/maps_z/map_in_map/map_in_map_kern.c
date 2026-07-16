// map_in_map_kern.c

#include <linux/bpf.h>

#include <linux/if_ether.h>

#include <linux/ip.h>

#include <bpf/bpf_helpers.h>

#include <bpf/bpf_endian.h>

/*=========================================================
 * Inner Map Template
 * Source IP  ---> Packet Counter
 *========================================================*/

struct {

    __uint(type, BPF_MAP_TYPE_HASH);

    __uint(max_entries, 1024);

    __type(key, __u32);

    __type(value, __u64);

} inner_map SEC(".maps");

/*=========================================================

 * Outer Map

 *

 * Index ---> Inner Hash Map

 *========================================================*/

struct {

    __uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);

    __uint(max_entries, 4);



    __type(key, __u32);



    __array(values, typeof(inner_map));



} outer_map SEC(".maps");


/*=========================================================

 * XDP Program

 *========================================================*/

SEC("xdp")

int map_in_map_demo(struct xdp_md *ctx)

{

    void *data_end = (void *)(long)ctx->data_end;

    void *data = (void *)(long)ctx->data;



    struct ethhdr *eth = data;



    if ((void *)(eth + 1) > data_end)

        return XDP_PASS;



    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)

        return XDP_PASS;



    struct iphdr *iph = (void *)(eth + 1);



    if ((void *)(iph + 1) > data_end)

        return XDP_PASS;



    /* Source IP */

    __u32 src_ip = iph->saddr;
    /*

     * Select Inner Map

     *

     * For now we always use

     * index 0.

     */

    __u32 index = 0;
    /*

     * First Lookup

     *

     * Returns pointer to an inner map.

     */

    void *inner;

    inner = bpf_map_lookup_elem(&outer_map, &index);

    if (!inner)

        return XDP_PASS;

    /*

     * Second Lookup

     *

     * Lookup source IP inside

     * the inner hash map.

     */

    __u64 *count;

    count = bpf_map_lookup_elem(inner, &src_ip);

    if (count) {
        (*count)++;
    } else {

        __u64 value = 1;

        bpf_map_update_elem(inner,

                            &src_ip,

                            &value,

                            BPF_ANY);

    }
    return XDP_PASS;
}
char LICENSE[] SEC("license") = "GPL"; 