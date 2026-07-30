# XDP Virtual Router: Cross-Subnet Packet Redirection with MAC Rewriting

## Overview

This guide extends a basic XDP redirect setup into a **Layer-3-aware XDP router**. Instead of transparently bridging two devices on the *same* subnet, this program routes traffic **between two different subnets**, rewriting Ethernet source/destination MAC addresses in-flight: entirely inside the XDP hook, with no involvement from the kernel's normal IP routing stack for the redirected traffic itself.

ARP is deliberately excluded from the redirect logic and passed up to the kernel, so **standard ARP resolution still works for discovering gateway MAC addresses**.

**Note**: TTL decrement is not implemented here.

---

## Part 1: Architecture & Topology

```
      Subnet 1: 192.168.1.0/24                    Subnet 2: 192.168.2.0/24
+------------------+       +----------------------------------------+       +------------------+
|     Device A     |       |               XDP Server                |       |     Device B     |
|  192.168.1.10/24 |       |     (acts as gateway on both subnets)   |       |  192.168.2.10/24 |
|  GW: 192.168.1.1 |       |                                          |       |  GW: 192.168.2.1 |
|                  |       |  cable1_xdp                cable2_xdp   |       |                  |
|   [cable1_a] <---|-------| 192.168.1.1                192.168.2.1  |-------|---> [cable2_b]    |
|                  |       |  [XDP Hook]                [XDP Hook]   |       |                  |
+------------------+       +----------------------------------------+       +------------------+
```

### Key Differences From a Same-Subnet Bridge

| | Same-subnet bridge | This lab (cross-subnet router) |
|---|---|---|
| XDP server has IPs? | No (transparent switch) | Yes — one IP per subnet, acting as gateway |
| Devices' default route | Not needed | Required (`ip route add default via ...`) |
| MAC rewriting | None | Source & destination MAC rewritten per hop |
| Maps used | `tx_port` (DEVMAP) only | `tx_port` (DEVMAP) **and** `mac_map` (next-hop MAC) |
| ARP handling | N/A (same L2 segment) | Explicitly passed to kernel via `XDP_PASS` |

### Key Components

1. **Network Namespaces (`netns`)** — `device_a`, `xdp_server`, `device_b`, simulating three separate machines.
2. **Virtual Ethernet Pairs (`veth`)** — `cable1` connects `device_a` ↔ `xdp_server`; `cable2` connects `device_b` ↔ `xdp_server`.
3. **eBPF Program (`xdp_router.c`)** — parses Ethernet headers, passes ARP to the kernel, drops non-IPv4 traffic, rewrites MACs, and redirects IPv4 frames to the opposite interface.
4. **`tx_port` (DEVMAP)** — maps key `0` → target interface index (`ifindex`) to redirect out of.
5. **`mac_map` (ARRAY)** — maps key `0` → the next-hop destination MAC address to stamp onto outgoing frames.
6. **Two independent attachments** — the same object file is loaded once per interface, so each attachment gets its **own** copy of `tx_port` and `mac_map`, configured to point "the other way."

---

## Part 2: How the Flow Works (Including ARP)

### Forward direction: Device A → Device B

1. Device A wants to reach `192.168.2.10`. Its own subnet is `192.168.1.0/24`, so the kernel routing logic determines this is a remote destination and consults the default route: `via 192.168.1.1`.
2. Before it can send anything, Device A needs the **MAC address of the gateway** (`192.168.1.1`), so it broadcasts an ARP request.
3. That ARP frame arrives at `cable1_xdp`. The XDP program's very first real check is:
   ```c
   if (eth->h_proto == bpf_htons(ETH_P_ARP))
       return XDP_PASS;
   ```
   `XDP_PASS` sends the frame up into the normal Linux stack of the `xdp_server` namespace. Since `192.168.1.1` is a real address configured on `cable1_xdp`, the kernel's built-in ARP responder answers normally.
4. Device A caches the gateway's MAC and sends the real IPv4 packet, addressed at Layer 2 to the gateway but at Layer 3 to `192.168.2.10`.
5. This IPv4 frame hits the XDP hook on `cable1_xdp` again. It isn't ARP and it is IPv4, so the program proceeds:
   - Looks up `mac_map` (this interface's own instance) → gets Device B's MAC (pre-configured by the setup script).
   - Copies the current destination MAC (`cable1_xdp`'s own address) into the source MAC field.
   - Overwrites the destination MAC with Device B's MAC.
   - Calls `bpf_redirect_map(&tx_port, 0, 0)`, which looks up `cable2_xdp`'s ifindex in this instance's `tx_port` map and transmits the frame directly out of it.
6. The frame arrives at `cable2_b` addressed directly to Device B's MAC, with the original IP addresses untouched. Device B's kernel accepts it as ordinary, locally-destined traffic.

### Return direction: Device B → Device A

Identical process, mirrored: Device B ARPs for its own gateway (`192.168.2.1`), the second XDP program instance on `cable2_xdp` passes that ARP through to the kernel, and subsequent IPv4 traffic gets its MACs swapped using **Door 2's own** `mac_map` (holding Device A's MAC) and redirected out `cable1_xdp` via **Door 2's own** `tx_port` (holding `IFINDEX1`).

Because each attachment owns independent maps, Door 1 always points "toward B" and Door 2 always points "toward A" — there's no shared state to get confused between directions.

### Why nothing here uses the kernel's IP routing table

Once a frame is IPv4 and hits the XDP hook, `bpf_redirect_map()` transmits it directly from the driver layer. It never touches `iptables`/`nftables`, never consults `/proc/sys/net/ipv4/ip_forward`, and never performs an IP routing table lookup — the "routing" decision is entirely pre-baked into the two BPF maps at setup time.

---

## Part 3: MAC and IP Assignment

**MAC addresses are auto-generated by the kernel**, not chosen by the scripts. When a veth pair is created:

```bash
ip link add cable1_a type veth peer name cable1_xdp
```

the kernel assigns each end a random, locally-administered MAC address. The setup script only *reads* these afterward:

```bash
cat /sys/class/net/cable1_a/address
```

**IP addresses are static**, assigned by hand with no DHCP involved:

```bash
ip addr add 192.168.1.10/24 dev cable1_a
```

The `/24` here isn't cosmetic — it's what the kernel uses to decide whether a destination address is on the local subnet (send directly) or remote (send via the default gateway, triggering the ARP-for-gateway behavior described above).

---

## Part 4: The eBPF Program (`xdp_router.c`)

Compile with:

```bash
clang -O2 -g -target bpf -c xdp_router.c -o xdp_router.o
```

---

## Part 5: Setup Script — Namespaces, Cables, Subnets, Gateways

```bash
#!/bin/bash
set -e

# Reset namespaces
sudo ip netns del device_a 2>/dev/null || true
sudo ip netns del xdp_server 2>/dev/null || true
sudo ip netns del device_b 2>/dev/null || true

# 1. Create namespaces & cables
sudo ip netns add device_a
sudo ip netns add xdp_server
sudo ip netns add device_b

sudo ip link add cable1_a type veth peer name cable1_xdp
sudo ip link add cable2_b type veth peer name cable2_xdp

sudo ip link set cable1_a netns device_a
sudo ip link set cable1_xdp netns xdp_server

sudo ip link set cable2_b netns device_b
sudo ip link set cable2_xdp netns xdp_server

# 2. Configure Subnet 1 (Device A <-> Server)
sudo ip netns exec device_a ip addr add 192.168.1.10/24 dev cable1_a
sudo ip netns exec device_a ip link set cable1_a up
sudo ip netns exec device_a ip link set lo up
# Add default gateway on Device A pointing to XDP Server
sudo ip netns exec device_a ip route add default via 192.168.1.1

sudo ip netns exec xdp_server ip addr add 192.168.1.1/24 dev cable1_xdp
sudo ip netns exec xdp_server ip link set cable1_xdp up

# 3. Configure Subnet 2 (Server <-> Device B)
sudo ip netns exec device_b ip addr add 192.168.2.10/24 dev cable2_b
sudo ip netns exec device_b ip link set cable2_b up
sudo ip netns exec device_b ip link set lo up
# Add default gateway on Device B pointing to XDP Server
sudo ip netns exec device_b ip route add default via 192.168.2.1

sudo ip netns exec xdp_server ip addr add 192.168.2.1/24 dev cable2_xdp
sudo ip netns exec xdp_server ip link set cable2_xdp up
sudo ip netns exec xdp_server ip link set lo up

echo "Multi-subnet environment successfully built!"
```

---

## Part 6: Attach XDP Programs & Configure Both Maps

```bash
#!/bin/bash
set -e

# 1. Get interface indexes inside xdp_server
IFINDEX1=$(sudo ip netns exec xdp_server cat /sys/class/net/cable1_xdp/ifindex)
IFINDEX2=$(sudo ip netns exec xdp_server cat /sys/class/net/cable2_xdp/ifindex)

# 2. Get MAC addresses of Device A and Device B endpoints
MAC_A=$(sudo ip netns exec device_a cat /sys/class/net/cable1_a/address)
MAC_B=$(sudo ip netns exec device_b cat /sys/class/net/cable2_b/address)

echo "Device A MAC: $MAC_A"
echo "Device B MAC: $MAC_B"

# Helper: convert formatted MAC (aa:bb:cc:dd:ee:ff) to hex bytes for bpftool
mac_to_hex() {
    echo $1 | tr ':' ' ' | awk '{print "0x"$1" 0x"$2" 0x"$3" 0x"$4" 0x"$5" 0x"$6}'
}

HEX_MAC_A=$(mac_to_hex $MAC_A)
HEX_MAC_B=$(mac_to_hex $MAC_B)

# 3. Mount BPF filesystem
sudo ip netns exec xdp_server mount -t bpf bpf /sys/fs/bpf 2>/dev/null || true

# 4. Attach XDP programs to both doors
echo "[+] Attaching xdp_router.o..."
sudo ip netns exec xdp_server ip link set dev cable1_xdp xdpgeneric obj xdp_router.o sec xdp
sudo ip netns exec xdp_server ip link set dev cable2_xdp xdpgeneric obj xdp_router.o sec xdp

# 5. Retrieve Map IDs for tx_port (2 maps: cable1_xdp and cable2_xdp)
TX_MAP_IDS=$(sudo ip netns exec xdp_server bpftool map list | grep tx_port | awk -F':' '{print $1}')
TX_MAP1_ID=$(echo $TX_MAP_IDS | awk '{print $1}')
TX_MAP2_ID=$(echo $TX_MAP_IDS | awk '{print $2}')

# 6. Retrieve Map IDs for mac_map (2 maps: cable1_xdp and cable2_xdp)
MAC_MAP_IDS=$(sudo ip netns exec xdp_server bpftool map list | grep mac_map | awk -F':' '{print $1}')
MAC_MAP1_ID=$(echo $MAC_MAP_IDS | awk '{print $1}')
MAC_MAP2_ID=$(echo $MAC_MAP_IDS | awk '{print $2}')

echo "[+] Updating Maps for Door 1 (cable1_xdp)..."
# Door 1 -> Redirect to cable2_xdp ($IFINDEX2)
sudo ip netns exec xdp_server bpftool map update id $TX_MAP1_ID key 0 0 0 0 value $IFINDEX2 0 0 0
# Door 1 -> Set target dest MAC = Device B's MAC ($HEX_MAC_B)
sudo ip netns exec xdp_server bpftool map update id $MAC_MAP1_ID key 0 0 0 0 value $HEX_MAC_B

echo "[+] Updating Maps for Door 2 (cable2_xdp)..."
# Door 2 -> Redirect to cable1_xdp ($IFINDEX1)
sudo ip netns exec xdp_server bpftool map update id $TX_MAP2_ID key 0 0 0 0 value $IFINDEX1 0 0 0
# Door 2 -> Set target dest MAC = Device A's MAC ($HEX_MAC_A)
sudo ip netns exec xdp_server bpftool map update id $MAC_MAP2_ID key 0 0 0 0 value $HEX_MAC_A

echo "XDP Router and MAC maps successfully configured!"
```

---

## Part 7: Verification

Since redirected traffic never touches the kernel's IP forwarding logic, connectivity should work even with forwarding explicitly disabled: proving the path is genuinely handled by the XDP program and not by the ordinary Linux router:

```bash
sudo ip netns exec xdp_server sysctl -w net.ipv4.ip_forward=0
sudo ip netns exec device_a ping 192.168.2.10
```

A successful ping here confirms the ARP-passthrough and MAC-rewrite/redirect logic is functioning correctly across both subnets.

**OR**

run:
```bash
sudo ip netns exec device_a tcpdump -n -e -i cable1_a
```
or/and
```bash
sudo ip netns exec device_b tcpdump -n -e -i cable2_b
```
while the above ping command runs in another terminal tab.

---
## Part 8: Teardown

```bash
sudo ip netns del device_a
sudo ip netns del xdp_server
sudo ip netns del device_b
```

## Part 9: Command Reference

| Command | Purpose |
|---|---|
| `ip netns add <name>` | Creates an isolated network namespace (own interfaces, routing table, ARP cache). |
| `ip netns del <name> 2>/dev/null \|\| true` | Removes a namespace if present, without aborting the script (`set -e`) on a fresh run. |
| `ip link add <a> type veth peer name <b>` | Creates a veth pair — a virtual "cable" with two ends that mirror each other's traffic. |
| `ip link set <iface> netns <ns>` | Moves an interface into a namespace. |
| `ip addr add <ip>/<prefix> dev <iface>` | Assigns a static IP address and subnet mask to an interface. |
| `ip link set <iface> up` | Administratively enables an interface. |
| `ip route add default via <gw-ip>` | Sets the default gateway — traffic to non-local subnets is sent here, triggering ARP resolution for the gateway's MAC. |
| `mount -t bpf bpf /sys/fs/bpf` | Mounts the BPF filesystem so `bpftool` can list/update maps by ID. |
| `ip link set dev <iface> xdpgeneric obj <file>.o sec xdp` | Attaches the compiled BPF object's `xdp` section to an interface in generic (SKB) mode. |
| `cat /sys/class/net/<iface>/ifindex` | Reads the kernel-assigned numeric interface index, used as a DEVMAP value. |
| `cat /sys/class/net/<iface>/address` | Reads the interface's auto-generated MAC address. |
| `bpftool map list \| grep <name> \| awk -F':' '{print $1}'` | Lists loaded BPF maps and extracts the numeric ID of matching ones. |
| `bpftool map update id <id> key 0 0 0 0 value <ifindex> 0 0 0` | Writes an ifindex (4 bytes, little-endian) into a DEVMAP at key `0`. |
| `bpftool map update id <id> key 0 0 0 0 value <6 hex bytes>` | Writes a raw 6-byte MAC address into `mac_map` at key `0`. |
| `sysctl -w net.ipv4.ip_forward=0` | Disables kernel IP forwarding, used here to prove redirected traffic bypasses it entirely. |

---

