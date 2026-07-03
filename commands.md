# Compile the XDP program
clang -O2 -g -target bpf -c xdp_md_analysis.c -o xdp_md_analysis.o

# Attach the XDP program in generic mode
sudo ip link set dev wlp1s0 xdpgeneric obj xdp_md_analysis.o sec xdp

# Verify that the program is attached
ip -details link show wlp1s0

# View trace output from bpf_printk()
sudo cat /sys/kernel/tracing/trace_pipe

# Generate traffic
ping 8.8.8.8
curl http://example.com
wget http://example.com

# Send TCP data to a listening server
echo "Hello XDP!" | nc <SERVER_IP> 8080

# If testing locally, first start a listener
nc -l 8080

# Then send data from another terminal
echo "Hello XDP!" | nc 127.0.0.1 8080

# Remove the XDP program
sudo ip link set dev wlp1s0 xdpgeneric off

# List available XDP sections in an object file
llvm-objdump -h xdp_md_analysis.o

# Inspect loaded BPF programs
sudo bpftool prog show

# Inspect network interfaces and attached XDP programs
ip -details link show

# Show interface information
ip addr show

# Show routing table
ip route

# Capture packets for comparison with your XDP output
sudo tcpdump -i wlp1s0

# Display verbose information about a kernel function
sudo bpftrace -lv 'kprobe:netif_receive_generic_xdp'

# Trace execution of the generic XDP receive path
sudo bpftrace -e '
kprobe:netif_receive_generic_xdp
{
    printf("XDP generic packet received\n");
}
'

# Trace skb information after XDP processing
sudo bpftrace -e '
kprobe:netif_receive_generic_xdp
{
    $skb = *(struct sk_buff **)arg0;
    printf("len=%d protocol=0x%x\n", $skb->len, $skb->protocol);
}
'
