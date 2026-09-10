# XDP Virtual Bi-Directional Packet Redirection Between Network Namespaces

## Overview

This guide documents the setup, execution, and deep technical breakdown of a **bi-directional XDP (eXpress Data Path) packet redirection ** on Linux (Ubuntu).

Using eBPF , packets arriving at a virtual server interface are intercepted directly at the network driver layer and redirected out of another interface, completely bypassing the standard Linux kernel network stack (TCP/IP stack).

---

## Part 1: Architecture & Topology

```
+------------------+         +--------------------------------------------+         +------------------+
|     Device A     |         |                XDP Server                  |         |     Device B     |
|  192.168.1.10/24 |         |              (No IP Assigned)               |         |  192.168.1.20/24 |
|                  |         |   cable1_xdp                  cable2_xdp    |         |                  |
|    [cable1_a] <--|-------->| [ XDP Hook ]                  [ XDP Hook ] <|-------->| [cable2_b]       |
+------------------+         |       |                            ^        |         +------------------+
                              |       v                            |        |
                              |   [ DEVMAP ] -----------------------+        |
                              |       |                                     |
                              | (If XDP_PASS)                               |
                              |       v                                     |
                              | [ Linux Kernel Network Stack / Router ]     |
                              +--------------------------------------------+
```

### Key Components

1. **Network Namespaces (`netns`)** — Isolated network stack environments simulating three distinct virtual machines: `device_a`, `xdp_server`, `device_b`.
2. **Virtual Ethernet Pairs (`veth`)** — Virtual cables connecting the isolated namespaces:
   - `cable1`: Connects `device_a` (`cable1_a`) to `xdp_server` (`cable1_xdp`).
   - `cable2`: Connects `device_b` (`cable2_b`) to `xdp_server` (`cable2_xdp`).
3. **eBPF Program (`xdp_redirect.c`)** — An XDP C program compiled into BPF bytecode (`xdp_red.o`).
4. **BPF Map (`DEVMAP`)** — A kernel array map (`tx_port`) mapping key `0` to the target interface index (`ifindex`).
5. **Same Subnet Mask** — `Device A` (`192.168.1.10/24`) and `Device B` (`192.168.1.20/24`) both use the `/24` subnet mask, placing them on the same logical subnet (`192.168.1.0/24`). This means neither device requires a gateway/route to reach the other — the XDP redirect purely handles delivery at the link layer as if both were on the same physical switch.


---

## Part 2: Compile the Program

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_red.o
```

---

## Part 3: Setup Script

```bash
#!/bin/bash
set -e

# Cleanup existing namespaces
sudo ip netns del device_a 2>/dev/null || true
sudo ip netns del xdp_server 2>/dev/null || true
sudo ip netns del device_b 2>/dev/null || true

# 1. Create network namespaces
sudo ip netns add device_a
sudo ip netns add xdp_server
sudo ip netns add device_b

# 2. Create virtual ethernet pairs
sudo ip link add cable1_a type veth peer name cable1_xdp
sudo ip link add cable2_b type veth peer name cable2_xdp

# 3. Move endpoints into respective namespaces
sudo ip link set cable1_a netns device_a
sudo ip link set cable1_xdp netns xdp_server

sudo ip link set cable2_b netns device_b
sudo ip link set cable2_xdp netns xdp_server

# 4. Configure IP addresses & bring interfaces UP
sudo ip netns exec device_a ip addr add 192.168.1.10/24 dev cable1_a
sudo ip netns exec device_a ip link set cable1_a up
sudo ip netns exec device_a ip link set lo up

sudo ip netns exec device_b ip addr add 192.168.1.20/24 dev cable2_b
sudo ip netns exec device_b ip link set cable2_b up
sudo ip netns exec device_b ip link set lo up

# Server doors UP (no IPs required - transparent switch)
sudo ip netns exec xdp_server ip link set cable1_xdp up
sudo ip netns exec xdp_server ip link set cable2_xdp up
sudo ip netns exec xdp_server ip link set lo up
```

---

## Part 4: Load & Attach XDP Programs, Populate the DEVMAP
 
```bash
# Retrieve interface indexes (ifindex) inside xdp_server
IFINDEX1=$(sudo ip netns exec xdp_server cat /sys/class/net/cable1_xdp/ifindex)
IFINDEX2=$(sudo ip netns exec xdp_server cat /sys/class/net/cable2_xdp/ifindex)
 
# Mount BPF filesystem
sudo ip netns exec xdp_server mount -t bpf bpf /sys/fs/bpf
 
# Attach XDP program to cable1_xdp (Door 1)
sudo ip netns exec xdp_server ip link set dev cable1_xdp xdpgeneric obj xdp_red.o sec xdp
 
# Attach XDP program to cable2_xdp (Door 2)
sudo ip netns exec xdp_server ip link set dev cable2_xdp xdpgeneric obj xdp_red.o sec xdp
 
# Store the Map IDs in variables
MAP_IDS=$(sudo ip netns exec xdp_server bpftool map list | grep tx_port | awk -F':' '{print $1}')
MAP1_ID=$(echo $MAP_IDS | awk '{print $1}')
MAP2_ID=$(echo $MAP_IDS | awk '{print $2}')
 
# Update Map 1 (cable1_xdp -> cable2_xdp)
sudo ip netns exec xdp_server bpftool map update id $MAP1_ID key 0 0 0 0 value $IFINDEX2 0 0 0
 
# Update Map 2 (cable2_xdp -> cable1_xdp)
sudo ip netns exec xdp_server bpftool map update id $MAP2_ID key 0 0 0 0 value $IFINDEX1 0 0 0
```

 
### Confirm the Kernel Stack Is Bypassed
 
Disable IP forwarding on the `xdp_server` namespace to prove that any successful connectivity is happening purely through the XDP redirect path, not the normal Linux router/forwarding logic:
 
```bash
sudo ip netns exec xdp_server sysctl -w net.ipv4.ip_forward=0
```
 
### Test Connectivity
 
```bash
sudo ip netns exec device_a ping 192.168.1.20
```
 
If the ping succeeds even with `ip_forward` disabled, it confirms packets are being redirected at the driver layer by the XDP program via the `DEVMAP`, entirely bypassing the kernel's routing stack.
 
---
 
## Part 6: Teardown
 
To stop the experiment and remove all virtual network interfaces and namespaces:
 
```bash
sudo ip netns del device_a
sudo ip netns del xdp_server
sudo ip netns del device_b
```
 
