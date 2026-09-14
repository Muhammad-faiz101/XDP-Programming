#!/usr/bin/env bash
set -e

echo "=== Installing eBPF & XDP Toolchain ==="
sudo apt-get update
sudo apt-get install -y \
    clang \
    llvm \
    libbpf-dev \
    libelf-dev \
    gcc-multilib \
    linux-headers-$(uname -r) \
    linux-tools-common \
    linux-tools-$(uname -r) \
    bpftool \
    iproute2 \
    tcpdump \
    ethtool \
    iperf3 \
    nmap-common

echo "=== Toolchain successfully installed! ==="