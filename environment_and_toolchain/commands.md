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


# Remove the XDP program
sudo ip link set dev wlp1s0 xdpgeneric off



# Inspect network interfaces and attached XDP programs
ip -details link show


