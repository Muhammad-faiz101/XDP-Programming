#!/usr/bin/env bash
set -euo pipefail

IF_CLIENT="ens8f0"
IF_F="ens7f0"
IF_Z="ens7f1"
PIN_DIR="/sys/fs/bpf/xdp_l7_router"

echo "[+] Detaching XDP..."
sudo bpftool net detach xdpgeneric dev "$IF_CLIENT" 2>/dev/null || true
sudo bpftool net detach xdpgeneric dev "$IF_F" 2>/dev/null || true
sudo bpftool net detach xdpgeneric dev "$IF_Z" 2>/dev/null || true
sudo ip link set dev "$IF_CLIENT" xdp off 2>/dev/null || true
sudo ip link set dev "$IF_F" xdp off 2>/dev/null || true
sudo ip link set dev "$IF_Z" xdp off 2>/dev/null || true

sudo rm -rf "$PIN_DIR" 2>/dev/null || true

echo "[+] XDP state removed. Promiscuous mode was left unchanged."
echo "    Disable it manually only if your lab requires it:"
echo "    sudo ip link set dev $IF_CLIENT promisc off"
echo "    sudo ip link set dev $IF_F promisc off"
echo "    sudo ip link set dev $IF_Z promisc off"
