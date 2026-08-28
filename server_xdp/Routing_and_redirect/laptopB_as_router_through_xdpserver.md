
``` ```

# XDP MAC-Rewriting Middlebox & Cross-Subnet NAT Router

This project demonstrates a high-performance, Layer 2 "stealth" packet redirector using eBPF/XDP, bridging two isolated subnets. The architecture consists of a Client (Laptop A), a custom XDP Middlebox (Ubuntu Server), and a NAT Router (Laptop B) that shares a Wi-Fi connection back to the client over an Ethernet link.

Because the XDP program rewrites MAC addresses directly in the network card driver, the Ubuntu Server's operating system network stack is completely bypassed. 

## 🏗️ Architecture & Topology

```text
+-------------------+             +-----------------------+             +-------------------+
|     Laptop A      |             |  Ubuntu Server (XDP)  |             |     Laptop B      |
|     (Client)      |             |    (L2 Middlebox)     |             |   (NAT Router)    |
|                   |             |                       |             |                   |
| IP: 192.168.50.2  |---(eth)--- >| ens7f0         ens7f1 |---(eth)--- >| IP: 192.168.60.2  |
| GW: 192.168.60.2  |             | (No IP)       (No IP) |             | Wi-Fi: wlp1s0     |
+-------------------+             +-----------------------+             +---------+---------+
                                                                                  | (NAT)
                                                                                  v
                                                                            [ INTERNET ]

```

### Packet Flow Summary

1. **Laptop A** generates a packet destined for the internet, but forces the Ethernet frame's destination MAC to be the Ubuntu Server's `ens7f0` MAC via a static ARP entry.
2. **The Ubuntu Server** intercepts the packet at the driver level using XDP. It rewrites the Source MAC to `ens7f1` and the Destination MAC to Laptop B, bypassing the Linux kernel entirely, and redirects it out `ens7f1`.
3. **Laptop B** receives the packet, recognizes it is destined for the internet, and translates the Source IP to its own Wi-Fi IP (Masquerade NAT).
4. Returning traffic follows the exact reverse path.

---

## 🛠️ Prerequisites

* `clang` / `llvm` with BPF target support.
* `libbpf-dev` (for `bpf/bpf_helpers.h`, `bpf/bpf_endian.h`).
* `bpftool`.
* `/sys/fs/bpf` bpffs support (kernel config `CONFIG_BPF`, `CONFIG_XDP_SOCKETS`, etc.).
* Root/sudo access on all three machines.

---

## 📂 1. Source Code: `xdp_router.c`

Create this file on the **Ubuntu Server**.

```c
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

// Ensure SEC macro is defined explicitly if bpf_helpers header missed it
#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif

struct mac_pair {
    unsigned char dst_mac[6];
    unsigned char src_mac[6];
};

struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u32);
} tx_port SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct mac_pair);
} mac_map SEC(".maps");

SEC("xdp")
int xdp_router(struct xdp_md *ctxt)
{
    void *data = (void *)(long)ctxt->data;
    void *data_end = (void *)(long)ctxt->data_end;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    if (eth->h_proto == bpf_htons(ETH_P_ARP))
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_DROP;

    __u32 key = 0;
    struct mac_pair *macs = bpf_map_lookup_elem(&mac_map, &key);

    if (macs) {
        __u32 *ifindex = bpf_map_lookup_elem(&tx_port, &key);

        if (ifindex) {
            bpf_printk("tx_port[%u] = %u", key, *ifindex);
        }

        bpf_printk("SRC %02x:%02x:%02x:%02x:%02x:%02x",
                macs->src_mac[0], macs->src_mac[1],
                macs->src_mac[2], macs->src_mac[3],
                macs->src_mac[4], macs->src_mac[5]);

        bpf_printk("DST %02x:%02x:%02x:%02x:%02x:%02x",
                macs->dst_mac[0], macs->dst_mac[1],
                macs->dst_mac[2], macs->dst_mac[3],
                macs->dst_mac[4], macs->dst_mac[5]);

        __builtin_memcpy(eth->h_source, macs->src_mac, 6);
        __builtin_memcpy(eth->h_dest, macs->dst_mac, 6);

        return bpf_redirect_map(&tx_port, key, 0);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

```

---

## 🚀 2. Phase 1: Deploying the XDP Middlebox (Ubuntu Server)

The middlebox requires no IP configuration. It only requires the compiled eBPF object and populated BPF maps.

### Compile

```bash
clang -O2 -g -target bpf -c xdp_router.c -o xdp_router.o

```

*(Verify the object contains a non-empty `xdp` section using `llvm-objdump -h xdp_router.o`)*

### Load, Pin, and Attach Maps

Run the following script to load the program onto both interfaces and populate the BPF maps with the correct cross-interface `ifindex` and MAC addresses.

```bash
IF1="ens7f0"
IF2="ens7f1"

IFINDEX1=$(cat /sys/class/net/$IF1/ifindex)
IFINDEX2=$(cat /sys/class/net/$IF2/ifindex)

# Hardware MAC Addresses
MAC_A="50:a1:32:76:de:bb"
MAC_B="50:a1:32:76:e9:f9"
MAC_ENS7F0="b4:96:91:12:9d:44"
MAC_ENS7F1="b4:96:91:12:9d:46"

mac_to_hex() {
    echo $1 | tr ':' ' ' | awk '{print "0x"$1" 0x"$2" 0x"$3" 0x"$4" 0x"$5" 0x"$6}'
}

HEX_MAC_A=$(mac_to_hex$MAC_A)
HEX_MAC_B=$(mac_to_hex$MAC_B)
HEX_ENS7F0=$(mac_to_hex$MAC_ENS7F0)
HEX_ENS7F1=$(mac_to_hex$MAC_ENS7F1)

# Cleanup old pins
sudo ip link set dev $IF1 xdp off 2>/dev/null || true
sudo ip link set dev $IF2 xdp off 2>/dev/null || true
sudo rm -rf /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2 2>/dev/null || true

# Mount BPF filesystem and create pin directories
sudo mount -t bpf bpf /sys/fs/bpf 2>/dev/null || true
sudo mkdir -p /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2

# Load and Attach XDP
sudo bpftool prog load xdp_router.o /sys/fs/bpf/xdp_router_if1/prog type xdp pinmaps /sys/fs/bpf/xdp_router_if1
sudo bpftool prog load xdp_router.o /sys/fs/bpf/xdp_router_if2/prog type xdp pinmaps /sys/fs/bpf/xdp_router_if2

sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/xdp_router_if1/prog dev $IF1
sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/xdp_router_if2/prog dev $IF2

# Populate Maps (Cross-Wiring interfaces)
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if1/tx_port key 0 0 0 0 value $IFINDEX2 0 0 0
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if1/mac_map key 0 0 0 0 value $HEX_MAC_B$HEX_ENS7F1

sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if2/tx_port key 0 0 0 0 value $IFINDEX1 0 0 0
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if2/mac_map key 0 0 0 0 value $HEX_MAC_A$HEX_ENS7F0

```

---

## 🌐 3. Phase 2: Configuring the NAT Router (Laptop B)

Laptop B receives the `.50.x` subnet traffic, translates it via NAT, and forwards it to its Wi-Fi gateway.

### Netplan Configuration (`/etc/netplan/01-netcfg.yaml`)

We use a link-scoped route so Laptop B knows the `.50.x` network is physically attached to the Ethernet link.

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      addresses: 
        - 192.168.60.2/24
      routes:
        - to: 192.168.50.0/24
          scope: link

```

Apply with: `sudo netplan apply`

### Enable IP Forwarding & NAT (iptables)

Turn on the kernel router switch and set up Masquerade to hide the `.50.x` subnet from the external internet.

```bash
# Enable Kernel Routing
sudo sysctl -w net.ipv4.ip_forward=1

# Intercept and translate outgoing traffic to the Wi-Fi IP
sudo iptables -t nat -A POSTROUTING -o wlp1s0 -j MASQUERADE

# Allow traffic flow between interfaces
sudo iptables -A FORWARD -i enp2s0 -o wlp1s0 -j ACCEPT
sudo iptables -A FORWARD -i wlp1s0 -o enp2s0 -m state --state RELATED,ESTABLISHED -j ACCEPT

```

---

## 💻 4. Phase 3: Configuring the Client (Laptop A)

Laptop A is on a completely different subnet than its gateway. We use `on-link: true` to bypass standard routing restrictions, and a static ARP entry to bypass dynamic address resolution.

### Netplan Configuration (`/etc/netplan/01-netcfg.yaml`)

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      addresses: 
        - 192.168.50.2/24
      routes:
        - to: default
          via: 192.168.60.2
          on-link: true
      nameservers:
        addresses: [8.8.8.8, 1.1.1.1]

```

Apply with: `sudo netplan apply`

### Inject Static ARP

Force Laptop A to send packets destined for `192.168.60.2` directly into the Ubuntu Server's hardware MAC address.

```bash
sudo ip neigh replace 192.168.60.2 lladdr b4:96:91:12:9d:44 dev enp2s0 (laptop A)
sudo ip neigh replace 192.168.50.2 lladdr b4:96:91:12:9d:46 dev enp2s0 (laptop B)
```

---

## ✅ 5. Verification & Testing

From **Laptop A**, run the following tests in sequence to verify the routing chain:

1. **Verify Layer 2/3 link to the Router:**
```bash
ping 192.168.60.2

```


2. **Verify External Routing & NAT:**
```bash
ping 8.8.8.8

```


3. **Verify DNS Resolution:**
```bash
ping google.com

```



*Note: You can monitor the traffic in real-time by viewing the iptables counters on Laptop B (`sudo iptables -t nat -L -v -n`). Standard packet sniffers (`tcpdump`) on the Ubuntu Server will **not** see the forwarded ICMP/TCP traffic, as XDP handles it below the OS network stack.*

---

## 🧹 6. Teardown (Ubuntu Server)

To detach the programs and clean up the pins on the Ubuntu Server, run:

```bash
IF1="ens7f0"
IF2="ens7f1"

# Detach XDP programs from both interfaces
sudo ip link set dev $IF1 xdp off
sudo ip link set dev $IF2 xdp off

# Remove pinned program/map directories
sudo rm -rf /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2

```