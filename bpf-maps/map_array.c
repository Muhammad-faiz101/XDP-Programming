#include<linux/bpf.h>
#include<bpf/bpf_helpers.h>

struct{
        __uint(type,BPF_MAP_TYPE_ARRAY);
        __uint(max_entries,1);
        __type(key,__u32);
        __type(value,__u64);

}counter SEC(".maps");


SEC("xdp")
int drop_packets(struct xdp_md*ctx)
        {
	__u32 key =0;
	__u64 *value;
	value=bpf_map_lookup_elem(&counter,&key); 
	if (!value)
	{
		return  XDP_ABORTED;
	}	

	(*value)++;
	
	
	
         if ((*value)%2==0)
        {
                return XDP_DROP;
        }
        else
        {
                return XDP_PASS;
	}      

}

char  _license[] SEC("license")="GPL";

