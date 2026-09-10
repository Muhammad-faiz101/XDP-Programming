#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# XDP L7 Router / HTTP->Z / HTTPS->F
#
# Server topology:
#   ens8f0 = Laptop A / client
#   ens7f0 = Laptop F / HTTPS path + return path
#   ens7f1 = Laptop Z / HTTP passive monitor
#
# The script discovers server ifindexes and server-interface MACs
# automatically from /sys/class/net. Peer laptop MACs are configured
# below because they live on other machines.
# ============================================================

IF_CLIENT="ens8f0"
IF_F="ens7f0"
IF_Z="ens7f1"

# Peer MAC addresses supplied for this topology.-of devices connected to server
MAC_A="fc:45:96:aa:a6:54"
MAC_F="50:a1:32:76:de:bb"
MAC_Z="50:a1:32:76:e9:f9"

PIN_DIR="/sys/fs/bpf/xdp_l7_router"
OBJ="./xdp_l7_router.o"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: '$1' not found." >&2
        exit 1
    }
}

for c in ip bpftool clang; do
    need_cmd "$c"
done

for ifc in "$IF_CLIENT" "$IF_F" "$IF_Z"; do
    [[ -d "/sys/class/net/$ifc" ]] || {
        echo "ERROR: interface '$ifc' does not exist." >&2
        exit 1
    }
done

[[ -f "$OBJ" ]] || {
    echo "ERROR: $OBJ not found. Compile xdp_l7_router.c first." >&2
    exit 1
}
d
# Discover Linux ifindexes dynamically.
CLIENT_IFINDEX=$(cat "/sys/class/net/$IF_CLIENT/ifindex")
F_IFINDEX=$(cat "/sys/class/net/$IF_F/ifindex")
Z_IFINDEX=$(cat "/sys/class/net/$IF_Z/ifindex")

# Discover server-interface MACs dynamically.-of server interfaces 
MAC_CLIENT_IF=$(cat "/sys/class/net/$IF_CLIENT/address")
MAC_F_IF=$(cat "/sys/class/net/$IF_F/address")
MAC_Z_IF=$(cat "/sys/class/net/$IF_Z/address")

mac_bytes() {
    local mac="$1"
    IFS=: read -r -a octets <<< "$mac"
    printf '0x%s ' "${octets[@]}"
}

# Prints the 6 bytes as bpftool expects them.
mac_bytes_clean() {
    local mac="$1"
    IFS=: read -r -a octets <<< "$mac"
    printf '0x%s ' "${octets[0]}"
    printf '0x%s ' "${octets[1]}"
    printf '0x%s ' "${octets[2]}"
    printf '0x%s ' "${octets[3]}"
    printf '0x%s ' "${octets[4]}"
    printf '0x%s' "${octets[5]}"
}

uint32_bytes() {
    local n="$1"
    printf '0x%02x 0x%02x 0x%02x 0x%02x' \
        $(( n        & 255 )) \
        $(( (n >> 8)  & 255 )) \
        $(( (n >> 16) & 255 )) \
        $(( (n >> 24) & 255 ))
}

cleanup() {
    echo "[+] Detaching old XDP programs..."
    sudo bpftool net detach xdpgeneric dev "$IF_CLIENT" 2>/dev/null || true
    sudo bpftool net detach xdpgeneric dev "$IF_F" 2>/dev/null || true
    sudo bpftool net detach xdpgeneric dev "$IF_Z" 2>/dev/null || true
    sudo ip link set dev "$IF_CLIENT" xdp off 2>/dev/null || true
    sudo ip link set dev "$IF_F" xdp off 2>/dev/null || true
    sudo ip link set dev "$IF_Z" xdp off 2>/dev/null || true
    sudo rm -rf "$PIN_DIR" 2>/dev/null || true
}

echo "============================================================"
echo " XDP HTTP/HTTPS selector"
echo "============================================================"
echo "[+] Client : $IF_CLIENT  ifindex=$CLIENT_IFINDEX  MAC=$MAC_CLIENT_IF"
echo "[+] F      : $IF_F       ifindex=$F_IFINDEX  MAC=$MAC_F_IF"
echo "[+] Z      : $IF_Z       ifindex=$Z_IFINDEX  MAC=$MAC_Z_IF"
echo ""
echo "[+] Peer A MAC: $MAC_A"
echo "[+] Peer F MAC: $MAC_F"
echo "[+] Peer Z MAC: $MAC_Z"
echo ""
echo "[+] Forwarding policy:"
echo "    TCP/80  : $IF_CLIENT -> $IF_Z (HTTP; connection may fail)"
echo "    TCP/443 : $IF_CLIENT -> $IF_F (HTTPS -> NAT)"
echo "    IPv4    : $IF_F -> $IF_CLIENT (HTTPS return path)"
echo "    ARP     : $IF_CLIENT <-> $IF_F"
echo ""

echo "[+] Cleaning old state..."
cleanup

sudo mount -t bpf bpf /sys/fs/bpf 2>/dev/null || true
sudo mkdir -p "$PIN_DIR"

# Transparent L2 forwarding requires the NICs to accept frames whose
# destination MAC is the peer's MAC, so enable promiscuous mode.
echo "[+] Enabling promiscuous mode on server ports..."
sudo ip link set dev "$IF_CLIENT" promisc on
sudo ip link set dev "$IF_F" promisc on
sudo ip link set dev "$IF_Z" promisc on

echo "[+] Loading one shared XDP program..."
sudo bpftool prog load "$OBJ" "$PIN_DIR/prog" type xdp \
    pinmaps "$PIN_DIR"

echo "[+] Attaching the same program to all three interfaces..."
sudo bpftool net attach xdpgeneric pinned "$PIN_DIR/prog" dev "$IF_CLIENT"
sudo bpftool net attach xdpgeneric pinned "$PIN_DIR/prog" dev "$IF_F"
sudo bpftool net attach xdpgeneric pinned "$PIN_DIR/prog" dev "$IF_Z"

# ------------------------------------------------------------
# ifindex_map logical roles:
#   0 = client / ens8f0
#   1 = F      / ens7f0
#   2 = Z      / ens7f1
# ------------------------------------------------------------
echo "[+] Populating ifindex_map..."
sudo bpftool map update pinned "$PIN_DIR/ifindex_map" \
    key 0 0 0 0 value $(uint32_bytes "$CLIENT_IFINDEX")
sudo bpftool map update pinned "$PIN_DIR/ifindex_map" \
    key 1 0 0 0 value $(uint32_bytes "$F_IFINDEX")
sudo bpftool map update pinned "$PIN_DIR/ifindex_map" \
    key 2 0 0 0 value $(uint32_bytes "$Z_IFINDEX")

# ------------------------------------------------------------
# tx_port logical slots:
#   0 = F
#   1 = Z
#   2 = client
# ------------------------------------------------------------
echo "[+] Populating tx_port DEVMAP..."
sudo bpftool map update pinned "$PIN_DIR/tx_port" \
    key 0 0 0 0 value $(uint32_bytes "$F_IFINDEX")
sudo bpftool map update pinned "$PIN_DIR/tx_port" \
    key 1 0 0 0 value $(uint32_bytes "$Z_IFINDEX")
sudo bpftool map update pinned "$PIN_DIR/tx_port" \
    key 2 0 0 0 value $(uint32_bytes "$CLIENT_IFINDEX")

# ------------------------------------------------------------
# mac_map slots:
#   0: toward F      src=server ens7f0, dst=Laptop F
#   1: toward Z      src=server ens7f1, dst=Laptop Z
#   2: toward client  src=server ens8f0, dst=Laptop A
# ------------------------------------------------------------
echo "[+] Populating mac_map..."
sudo bpftool map update pinned "$PIN_DIR/mac_map" \
    key 0 0 0 0 \
    value $(mac_bytes_clean "$MAC_F") $(mac_bytes_clean "$MAC_F_IF")

sudo bpftool map update pinned "$PIN_DIR/mac_map" \
    key 1 0 0 0 \
    value $(mac_bytes_clean "$MAC_Z") $(mac_bytes_clean "$MAC_Z_IF")

sudo bpftool map update pinned "$PIN_DIR/mac_map" \
    key 2 0 0 0 \
    value $(mac_bytes_clean "$MAC_A") $(mac_bytes_clean "$MAC_CLIENT_IF")

echo ""
echo "[+] SUCCESS"
echo "    Pinned objects: $PIN_DIR"
echo ""
echo "    HTTP : $IF_CLIENT -> $IF_Z"
echo "    HTTPS: $IF_CLIENT -> $IF_F"
echo "    Reply: $IF_F -> $IF_CLIENT"
echo ""
echo "[+] Useful checks:"
echo "    sudo bpftool net show"
echo "    sudo bpftool map dump pinned $PIN_DIR/ifindex_map"
echo "    sudo bpftool map dump pinned $PIN_DIR/tx_port"
echo "    sudo bpftool map dump pinned $PIN_DIR/mac_map"
echo "    sudo tcpdump -ni $IF_CLIENT tcp"
echo "    sudo tcpdump -ni $IF_F tcp port 443"
echo "    sudo tcpdump -ni $IF_Z tcp port 80"
