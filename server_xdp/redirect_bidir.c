#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>


#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9

SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;

    /* Bounds check */
    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    
    if (ctx->ingress_ifindex == ENS7F0_IFINDEX) {
    /* A -> B */

    /* Rewrite destination to Laptop B */
    __builtin_memcpy(eth->h_dest,
        (unsigned char[]){0x50,0xa1,0x32,0x76,0xe9,0xf9},
        ETH_ALEN);

    /* Rewrite source to server ens7f1 */
    __builtin_memcpy(eth->h_source,
        (unsigned char[]){0xb4,0x96,0x91,0x12,0x9d,0x46},
        ETH_ALEN);

    return bpf_redirect(ENS7F1_IFINDEX, 0);
    }


    if (ctx->ingress_ifindex == ENS7F1_IFINDEX) {
        /* B -> A */

        /* Rewrite destination to Laptop A MAC */
        __builtin_memcpy(eth->h_dest,
            (unsigned char[]){ 0x50,0xa1,0x32,0x76,0xde,0xbb},
            ETH_ALEN);

        /* Rewrite source to server ens7f0 MAC */
        __builtin_memcpy(eth->h_source,
            (unsigned char[]){0xb4,0x96,0x91,0x12,0x9d,0x44},
            ETH_ALEN);

        return bpf_redirect(ENS7F0_IFINDEX, 0);
    }

    return XDP_PASS;
  
}

char LICENSE[] SEC("license") = "GPL";


