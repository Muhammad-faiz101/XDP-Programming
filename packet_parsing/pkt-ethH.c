#include<linux/bpf.h>
#include<bpf/bpf_helpers.h>

SEC("xdp")

int pkt_hcontents(struct xdp_md *context){
    __u8 *data = (__u8*)(long) context->data; //long:8bytes, __u8:1byte, need cpu to read 1byte at a time
    __u8 *data_end = (__u8*)(long) context->data_end;

    if (data + 16 > data_end) 
    {
        return XDP_PASS;
    }

    bpf_printk("DLL ETHERNET HEADER:\n");
    bpf_printk("Dest MAC : %02x:%02x:%02x:%02x:%02x:%02x\n", 
               data[0], data[1], data[2], data[3], data[4], data[5]); //6bytes
    bpf_printk("Src MAC : %02x:%02x:%02x:%02x:%02x:%02x\n", 
               data[6], data[7], data[8], data[9], data[10], data[11]); //6 bytes
    bpf_printk("Ethertype : 0x%02x%02x\n\n", data[12], data[13]); //2 bytes

    bpf_printk("IP HEADER:\n");
    bpf_printk("Version : %u\n", (data[14] >> 4) & 0x0F); //4 bits
    bpf_printk("IHL : %u\n", data[14] & 0x0F); //4 bits
    bpf_printk("DSCP : %u\n", (data[15] >> 2) & 0x3F); //6 bits
    bpf_printk("ECN : %u\n", data[15] & 0x03); //2


    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";