#!/usr/bin/env bash

echo "=== 1. Kernel & BPF JIT Status ==="
uname -r
sysctl net.core.bpf_jit_enable
sysctl net.core.bpf_jit_harden

echo -e "\n=== 2. Mounted BPF Filesystem ==="
mount | grep bpf || echo "Warning: /sys/fs/bpf not mounted. Run: sudo mount -t bpf bpf /sys/fs/bpf"

echo -e "\n=== 3. Available Network Interfaces ==="
ip -br link

echo -e "\n=== 4. Target Interface Driver & XDP Support ==="
IFACE=${1:-"eth0"}
if ip link show "$IFACE" > /dev/null 2>&1; then
    echo "Inspecting interface: $IFACE"
    ethtool -i "$IFACE" | grep driver
    ethtool -k "$IFACE" | grep -E "rx-vlan-filter|tx-checksum"
else
    echo "Interface $IFACE not found. Pass interface as: ./check_env.sh <iface_name>"
fi