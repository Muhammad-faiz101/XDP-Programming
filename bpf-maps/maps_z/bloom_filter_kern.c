// bloom_filter_kern.c

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/*------------------------------------------------------------
 * Bloom Filter Map
 *-----------------------------------------------------------*/
struct {
    __uint(type, BPF_MAP_TYPE_BLOOM_FILTER);
    __uint(max_entries, 1024);
    __uint(map_extra, 5);          // Number of hash functions
    __type(value, __u32);
} bloom_filter SEC(".maps");

/*------------------------------------------------------------
 * XDP Program
 *-----------------------------------------------------------*/
SEC("xdp")
int bloom_filter_demo(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    /*---------------- Ethernet ----------------*/
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    /* Only IPv4 */
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    /*---------------- IPv4 ----------------*/
    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    /* Source IP */
    __u32 src_ip = iph->saddr;

    /*--------------------------------------------------
     * Check if IP already exists in Bloom Filter
     *-------------------------------------------------*/
    if (bpf_map_peek_elem(&bloom_filter, &src_ip) == 0) {

        bpf_printk("--------------------------------------");
        bpf_printk("Bloom Filter Result : SEEN BEFORE");
        bpf_printk("Source IP : %x", bpf_ntohl(src_ip));
        bpf_printk("--------------------------------------");

    } else {

        bpf_printk("--------------------------------------");
        bpf_printk("Bloom Filter Result : NEW IP");
        bpf_printk("Source IP : %x", bpf_ntohl(src_ip));
        bpf_printk("Inserting into Bloom Filter");
        bpf_printk("--------------------------------------");

        bpf_map_push_elem(&bloom_filter, &src_ip, BPF_ANY);
    }

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";