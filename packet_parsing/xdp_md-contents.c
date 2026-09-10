#include<linux/bpf.h>
#include<bpf/bpf_helpers.h>

SEC("xdp")
int observe_xdp_md(struct xdp_md *context){
    __u32 pkt_data = context->data; //this is equiv to skb's data pointer
    __u32 pkt_data_end = context->data_end; //equiv to skb's tail pointer
    __u32 infindex = context->ingress_ifindex; //n/w interface index the pkt arrived on

//   __u32 pkt_data_meta = context->data_meta; //this should be empty by default

    __u32 pkt_len = pkt_data_end - pkt_data; //length of the packet

    bpf_printk("Ingress Interface Index: %u\n", infindex);
    bpf_printk("context->data offset       : 0x%X\n", pkt_data);
    bpf_printk("context->data_end offset   : 0x%X\n", pkt_data_end);
    bpf_printk("Calculated Packet Size : %u bytes\n", pkt_len);

//    bpf_printk("context->data_meta offset  : 0x%X\n", pkt_data_meta);


    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

