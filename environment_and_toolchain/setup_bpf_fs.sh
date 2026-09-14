#!/usr/bin/env bash
set -e

echo "=== 1. Mounting /sys/fs/bpf (BPF Virtual Filesystem) ==="
if ! mount | grep -q "/sys/fs/bpf"; then
    sudo mount -t bpf bpf /sys/fs/bpf
    echo "Mounted /sys/fs/bpf successfully."
else
    echo "/sys/fs/bpf is already mounted."
fi

echo "=== 2. Enabling In-Kernel BPF JIT Compilation ==="
sudo sysctl -w net.core.bpf_jit_enable=1

echo "=== 3. Setting Max Locked Memory Limit ==="
sudo ulimit -l unlimited || true

echo "=== System tuned for eBPF/XDP development! ==="