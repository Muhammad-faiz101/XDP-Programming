# XDP L2 Traffic Steering Lab

## 1. Overview

This document describes the three-laptop + one-server networking lab using an XDP program on the server.

The server performs **Layer-2 forwarding** by rewriting Ethernet source/destination MAC addresses and redirecting packets through a BPF `DEVMAP`.

The server does not perform IP NAT. Laptop F performs IP forwarding and NAT between its Ethernet interface (`enp2s0`) and Wi-Fi interface (`wlp1s0`).

The intended traffic policy is:

| Traffic from Laptop A | Server ingress | Server decision | Server egress | Destination |
|---|---|---|---|---|
| HTTP / TCP 80 | `ens8f0` | HTTP | `ens7f1` | Laptop Z |
| HTTPS / TCP 443 | `ens8f0` | HTTPS | `ens7f0` | Laptop F |
| DNS / UDP 53 | `ens8f0` | DNS | `ens7f0` | Laptop F / Internet path |
| DNS / TCP 53 | `ens8f0` | DNS | `ens7f0` | Laptop F / Internet path |
| ICMP | `ens8f0` | Ping/connectivity | `ens7f0` | Laptop F / Internet path |
| Return IPv4 traffic from F | `ens7f0` | Return path | `ens8f0` | Laptop A |
| Traffic from Z | `ens7f1` | Passive monitor | Drop | — |

HTTP is intentionally sent only to Laptop Z. Because Z is only a monitoring machine and does not respond to the TCP connection, an HTTP connection from Laptop A is allowed to fail.

---

# 2. Overall Topology and Architecture

## 2.1 Physical topology

```text
                                      INTERNET
                                          ^
                                          |
                                   NAT / IP forwarding
                                          |
                                      wlp1s0
                                   Laptop F
                                +----------------+
                                |                |
                                |     enp2s0     |
                                +--------+-------+
                                         |
                                         |
                              192.168.50.2/24
                                         |
                                         |
                              Server ens7f0
                           MAC b4:96:91:12:9d:44
                                         |
                                         |
                +------------------------+------------------------+
                |                         |                        |
                |                         |                        |
          Server ens8f0             Server ens7f1                 |
          MAC ...:9d:5c             MAC ...:9d:46                 |
                |                         |                        |
                |                         |                        |
          Laptop A                  Laptop Z                       |
      10.200.1.6/24              192.168.60.2/24                  |
      MAC fc:45:96:aa:a6:54       MAC 50:a1:32:76:e9:f9            |
                                      Wireshark                     |
```

## 2.2 Server interface roles

| Logical role | Server interface | Purpose | Server MAC |
|---|---|---|---|
| Client | `ens8f0` | Connected to Laptop A | `b4:96:91:12:9d:5c` |
| F | `ens7f0` | Connected to Laptop F | `b4:96:91:12:9d:44` |
| Z | `ens7f1` | Connected to Laptop Z / Wireshark | `b4:96:91:12:9d:46` |

## 2.3 Laptop MAC addresses

```text
Laptop A = fc:45:96:aa:a6:54
Laptop F = 50:a1:32:76:de:bb
Laptop Z = 50:a1:32:76:e9:f9
```

## 2.4 IP configuration

### Laptop A

```text
Interface: enp0s31f6
IP:        10.200.1.6/24
Gateway:   192.168.50.2
```

The gateway is outside A's normal subnet, so `on-link: true` is used and a static neighbor entry is installed.

### Laptop F

```text
Interface: enp2s0
IP:        192.168.50.2/24
```

F also has Wi-Fi interface:

```text
enp2s0 -> wlp1s0 -> Internet
```

### Laptop Z

```text
Interface: enp2s0
IP:        192.168.60.2/24
Gateway:   192.168.60.1
```

Z is intended to be a passive monitoring endpoint for HTTP traffic.

---

# 3. Traffic Architecture

## 3.1 HTTP path

HTTP is deliberately redirected to Laptop Z and does not continue to Laptop F.

```text
Laptop A
   |
   | TCP destination port 80
   v
Server ens8f0
   |
   | XDP classification
   | HTTP -> TX_TO_Z
   v
Server ens7f1
   |
   v
Laptop Z
   |
 Wireshark
```

The HTTP connection may fail at Laptop A because Z is not acting as the HTTP server.

## 3.2 HTTPS path

```text
Laptop A
   |
   | TCP destination port 443
   v
Server ens8f0
   |
   | XDP classification
   | HTTPS -> TX_TO_F
   v
Server ens7f0
   |
   v
Laptop F enp2s0
   |
   | IP forwarding
   | MASQUERADE/NAT
   v
Laptop F wlp1s0
   |
   v
Internet
```

## 3.3 HTTPS return path

```text
Internet
   |
   v
Laptop F wlp1s0
   |
   | reverse NAT / conntrack
   v
Laptop F enp2s0
   |
   v
Server ens7f0
   |
   | XDP return-path rule
   | F -> CLIENT
   v
Server ens8f0
   |
   v
Laptop A
```

## 3.4 DNS and ICMP

DNS and ICMP are forwarded to Laptop F so that normal name resolution and connectivity testing can work.

```text
DNS UDP/53  : A -> ens8f0 -> XDP -> ens7f0 -> F -> Internet DNS
DNS TCP/53  : A -> ens8f0 -> XDP -> ens7f0 -> F -> Internet DNS
ICMP        : A -> ens8f0 -> XDP -> ens7f0 -> F -> Internet
```

---

# 4. Laptop Network Configuration

## 4.1 Laptop F Netplan

Example configuration discussed for Laptop F:

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      addresses:
        - 192.168.50.2/24
      routes:
        - to: 10.200.1.0/24
          scope: link
```

Apply it with:

```bash
sudo netplan apply
```

Verify:

```bash
ip -br addr
ip route
```

## 4.2 Laptop A Netplan

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp0s31f6:
      addresses:
        - 10.200.1.6/24
      routes:
        - to: default
          via: 192.168.50.2
          on-link: true
      nameservers:
        addresses: [8.8.8.8, 1.1.1.1]
```

Apply it:

```bash
sudo netplan apply
```

Verify:

```bash
ip -br addr
ip route
```

## 4.3 Laptop Z Netplan

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      addresses:
        - 192.168.60.2/24
      routes:
        - to: default
          via: 192.168.60.1
      nameservers:
        addresses: [8.8.8.8, 1.1.1.1]
```

Apply it:

```bash
sudo netplan apply
```

---

# 5. Static ARP / Neighbor Configuration

The lab uses static neighbor entries so the machines can send Ethernet frames to the server's MAC addresses even though the logical IP next hops are on different subnets.

## 5.1 Laptop A

```bash
sudo ip neigh replace 192.168.50.2 \
    lladdr b4:96:91:12:9d:5c \
    dev enp0s31f6
```

This associates:

```text
192.168.50.2 -> server ens8f0 MAC b4:96:91:12:9d:5c
```

## 5.2 Laptop F

```bash
sudo ip neigh replace 10.200.1.6 \
    lladdr b4:96:91:12:9d:44 \
    dev enp2s0
```

This associates:

```text
10.200.1.6 -> server ens7f0 MAC b4:96:91:12:9d:44
```

Verify on each machine with:

```bash
ip neigh show
```

---

# 6. Laptop F: IP Forwarding and NAT

Laptop F is the actual Internet gateway for the HTTPS/DNS/ICMP paths.

## 6.1 Enable kernel routing

```bash
sudo sysctl -w net.ipv4.ip_forward=1
```

Verify:

```bash
sysctl net.ipv4.ip_forward
```

Expected:

```text
net.ipv4.ip_forward = 1
```

## 6.2 NAT / MASQUERADE

```bash
sudo iptables -t nat -A POSTROUTING \
    -o wlp1s0 \
    -j MASQUERADE
```

This translates traffic leaving F through Wi-Fi so the Internet sees F's Wi-Fi-side address instead of Laptop A's private address.

## 6.3 Forwarding rules

```bash
sudo iptables -A FORWARD \
    -i enp2s0 \
    -o wlp1s0 \
    -j ACCEPT
```

Return traffic:

```bash
sudo iptables -A FORWARD \
    -i wlp1s0 \
    -o enp2s0 \
    -m state --state RELATED,ESTABLISHED \
    -j ACCEPT
```

Verify:

```bash
sudo iptables -S FORWARD
sudo iptables -t nat -S POSTROUTING
```

---

# 7. XDP Program Architecture

The XDP program is a single program attached to all three server interfaces.

```text
                       xdp_l7_router.o
                              |
                              v
                       ONE XDP PROGRAM
                    /          |          \
                   /           |           \
              ens8f0        ens7f0       ens7f1
              CLIENT           F             Z
```

The program uses three maps.

## 7.1 `ifindex_map`

Type:

```c
BPF_MAP_TYPE_ARRAY
```

Purpose:

```text
logical role -> actual Linux ifindex
```

Logical roles:

```c
#define ROLE_CLIENT 0
#define ROLE_F      1
#define ROLE_Z      2
```

For example, if Linux reports:

```text
ens8f0 = 6
ens7f0 = 8
ens7f1 = 9
```

then:

```text
ifindex_map:

0 -> 6
1 -> 8
2 -> 9
```

The numbers are discovered at runtime; they are not hardcoded in the C source.

## 7.2 `mac_map`

Type:

```c
BPF_MAP_TYPE_ARRAY
```

Purpose:

```text
TX slot -> Ethernet destination/source MAC pair
```

Logical slots:

```c
#define TX_TO_F       0
#define TX_TO_Z       1
#define TX_TO_CLIENT  2
```

Conceptually:

```text
slot 0:
    source = server ens7f0 MAC
    dest   = Laptop F MAC

slot 1:
    source = server ens7f1 MAC
    dest   = Laptop Z MAC

slot 2:
    source = server ens8f0 MAC
    dest   = Laptop A MAC
```

## 7.3 `tx_port`

Type:

```c
BPF_MAP_TYPE_DEVMAP
```

Purpose:

```text
TX slot -> egress interface index
```

For example:

```text
tx_port:

0 -> ens7f0 ifindex
1 -> ens7f1 ifindex
2 -> ens8f0 ifindex
```

The actual redirect is performed using:

```c
bpf_redirect_map(&tx_port, tx_slot, 0);
```

---

# 8. Important XDP Concepts

## 8.1 Loading

Loading places the compiled BPF program into the kernel:

```bash
sudo bpftool prog load xdp_l7_router.o \
    /sys/fs/bpf/xdp_l7_router/prog \
    type xdp \
    pinmaps /sys/fs/bpf/xdp_l7_router
```

After loading, the program and maps exist as kernel BPF objects.

## 8.2 Pinning

Pinning creates persistent BPF filesystem references under:

```text
/sys/fs/bpf/xdp_l7_router/
```

Typical pinned objects:

```text
prog
ifindex_map
mac_map
tx_port
```

Pinning allows later userspace commands such as `bpftool map update` to access the live kernel maps.

The pinned files are references to kernel BPF objects; they are not normal executable files.

## 8.3 Attaching

Attaching connects the loaded XDP program to a network interface:

```bash
sudo bpftool net attach xdpgeneric \
    pinned /sys/fs/bpf/xdp_l7_router/prog \
    dev ens8f0
```

The same program is attached to `ens7f0` and `ens7f1`.

Therefore:

```text
LOAD   = put BPF program in kernel
ATTACH = connect BPF program to an interface
PIN    = keep a filesystem reference to the kernel object
```

---

# 9. XDP Packet Processing Logic

## 9.1 Receive the packet

The program starts with:

```c
SEC("xdp")
int xdp_l7_router(struct xdp_md *ctx)
```

The packet boundaries are obtained using:

```c
void *data = (void *)(long)ctx->data;
void *data_end = (void *)(long)ctx->data_end;
```

The Ethernet header is:

```c
struct ethhdr *eth = data;
```

## 9.2 Determine ingress interface

```c
__u32 ingress = ctx->ingress_ifindex;
```

This is how the single XDP program knows which server interface received the packet.

For example:

```text
packet arrives on ens8f0
        -> ingress_ifindex = ens8f0's ifindex

packet arrives on ens7f0
        -> ingress_ifindex = ens7f0's ifindex
```

The XDP code compares this value against the dynamically populated `ifindex_map`.

## 9.3 Ethernet protocol classification

The program checks `eth->h_proto`.

ARP is handled separately. Non-IPv4 traffic is dropped.

For IPv4:

```c
struct iphdr *iph = (void *)(eth + 1);
```

The IP protocol determines whether the packet is TCP, UDP, ICMP, etc.

---

# 10. Traffic Decision Logic

The intended decision tree is:

```text
                    packet
                      |
                      v
              determine ingress
                      |
        +-------------+-------------+
        |             |             |
    ens8f0          ens7f0        ens7f1
    CLIENT             F              Z
        |              |              |
        |              |              +--> DROP
        |              |
        |              +--------------> CLIENT
        |
        +--> ICMP -------------------> F
        |
        +--> UDP dst 53 -------------> F
        |
        +--> TCP dst 53 -------------> F
        |
        +--> TCP dst 80 -------------> Z
        |
        +--> TCP dst 443 ------------> F
        |
        +--> anything else ----------> DROP
```

## 10.1 HTTP

When a packet arrives from the client and:

```text
a) IP protocol = TCP
b) TCP destination port = 80
```

the program executes the logical equivalent of:

```c
redirect_slot(eth, TX_TO_Z);
```

The Ethernet header is rewritten using `mac_map[TX_TO_Z]`, then the packet is redirected using `tx_port[TX_TO_Z]`.

## 10.2 HTTPS

When:

```text
IP protocol = TCP
TCP destination port = 443
```

it does:

```c
redirect_slot(eth, TX_TO_F);
```

The packet then reaches Laptop F, which performs IP forwarding and NAT.

## 10.3 DNS

DNS may use UDP/53 or TCP/53.

Both are forwarded to F so the client can resolve website names.

## 10.4 ICMP

ICMP is forwarded to F so commands such as:

```bash
ping 8.8.8.8
```

can traverse the lab topology.

## 10.5 Return traffic from F

IPv4 traffic arriving on `ens7f0` enters the return-path branch:

```c
if (ingress == f_if) {
    return redirect_slot(eth, TX_TO_CLIENT);
}
```

The server rewrites the Ethernet header for Laptop A and redirects the packet to `ens8f0`.

---

# 11. Ethernet MAC Rewriting

The server is doing L2 forwarding, so the IP packet is not being routed by the server itself.

For A -> F:

```text
Ethernet source      = server ens7f0 MAC
Ethernet destination = Laptop F MAC
```

For A -> Z:

```text
Ethernet source      = server ens7f1 MAC
Ethernet destination = Laptop Z MAC
```

For F -> A:

```text
Ethernet source      = server ens8f0 MAC
Ethernet destination = Laptop A MAC
```

The relevant function is:

```c
static __always_inline int set_eth_macs(
    struct ethhdr *eth,
    __u32 tx_slot
)
```

It obtains the appropriate `mac_pair` from `mac_map` and performs:

```c
__builtin_memcpy(eth->h_dest, macs->dst_mac, ETH_ALEN);
__builtin_memcpy(eth->h_source, macs->src_mac, ETH_ALEN);
```

---

# 12. DEVMAP Redirection

After the MAC rewrite:

```c
return bpf_redirect_map(&tx_port, tx_slot, 0);
```

The lookup is conceptually:

```text
TX_TO_F
   |
   v
tx_port[0]
   |
   v
ens7f0 ifindex
```

or:

```text
TX_TO_Z
   |
   v
tx_port[1]
   |
   v
ens7f1 ifindex
```

or:

```text
TX_TO_CLIENT
   |
   v
tx_port[2]
   |
   v
ens8f0 ifindex
```

The server therefore has two separate forwarding decisions:

```text
MAC map = what Ethernet header should look like?
DEVMAP  = which interface should transmit it?
```

---

# 13. Loader Script Responsibilities

The loader script performs the following lifecycle:

```text
1. Define interface names
2. Discover ifindexes from /sys
3. Discover server MAC addresses from /sys
4. Clean old XDP attachments and pins
5. Mount BPF filesystem
6. Load BPF program
7. Pin program and maps
8. Attach one program to all three interfaces
9. Populate ifindex_map
10. Populate tx_port
11. Populate mac_map
12. Enable promiscuous mode for the lab
```

## 13.1 Dynamic interface discovery

The loader uses:

```bash
CLIENT_IFINDEX=$(cat /sys/class/net/$IF_CLIENT/ifindex)
F_IFINDEX=$(cat /sys/class/net/$IF_F/ifindex)
Z_IFINDEX=$(cat /sys/class/net/$IF_Z/ifindex)
```

Therefore the C program does not hardcode values such as `6`, `8`, or `9`.

Likewise, server MAC addresses are obtained using:

```bash
cat /sys/class/net/<interface>/address
```

Peer laptop MACs are configured manually because they belong to other machines.

---

# 14. Promiscuous Mode

The loader can enable:

```bash
sudo ip link set dev ens8f0 promisc on
sudo ip link set dev ens7f0 promisc on
sudo ip link set dev ens7f1 promisc on
```

Promiscuous mode makes the NIC more permissive about accepting Ethernet frames whose destination MAC is not the NIC's own MAC. It is useful in this lab's L2 forwarding setup, although the DEVMAP redirect itself is a separate mechanism.

Check it with:

```bash
ip link show ens8f0
ip link show ens7f0
ip link show ens7f1
```

---

# 15. Compile, Load, and Unload

## 15.1 Compile

From the directory containing the C source:

```bash
clang -O2 -g -target bpf \
    -c xdp_l7_router.c \
    -o xdp_l7_router.o
```

This converts the C source into a BPF ELF object. It does not yet attach anything to a network interface.

## 15.2 Load and attach

Run the loader:

```bash
chmod +x load_xdp_l7_router.sh
sudo ./load_xdp_l7_router.sh
```

## 15.3 Unload

```bash
chmod +x unload_xdp_l7_router.sh
sudo ./unload_xdp_l7_router.sh
```

The unload process:

```text
detach XDP
     |
     v
remove pinned objects
     |
     v
clean BPF state
```

---

# 16. Useful Verification Commands

## Server: interface indexes and MACs

```bash
for i in ens8f0 ens7f0 ens7f1; do
    echo "=== $i ==="
    cat /sys/class/net/$i/ifindex
    cat /sys/class/net/$i/address
done
```

## Server: XDP attachments

```bash
sudo bpftool net show
```

## Server: pinned BPF objects

```bash
sudo ls -lah /sys/fs/bpf/xdp_l7_router
```

## Server: dump interface-role map

```bash
sudo bpftool map dump \
    pinned /sys/fs/bpf/xdp_l7_router/ifindex_map
```

## Server: dump DEVMAP

```bash
sudo bpftool map dump \
    pinned /sys/fs/bpf/xdp_l7_router/tx_port
```

## Server: dump MAC map

```bash
sudo bpftool map dump \
    pinned /sys/fs/bpf/xdp_l7_router/mac_map
```

## XDP debug output

```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

The debug separator:

```text
-----------------------------------------
```

is intended to make the trace for each XDP execution easier to visually distinguish.

Because `bpf_printk()` executes per packet, high-volume traffic can generate a large amount of trace output and should not be used for final throughput measurements.

---

# 17. Packet Capture / Wireshark Checks

## Server, client-facing port

```bash
sudo tcpdump -eni ens8f0 tcp
```

## Server, F-facing port

```bash
sudo tcpdump -eni ens7f0 tcp port 443
```

## Server, Z-facing port

```bash
sudo tcpdump -eni ens7f1 tcp port 80
```

## DNS

```bash
sudo tcpdump -eni ens8f0 port 53
sudo tcpdump -eni ens7f0 port 53
```

## ICMP

```bash
sudo tcpdump -eni ens8f0 icmp
sudo tcpdump -eni ens7f0 icmp
```

On Laptop Z, Wireshark should see HTTP packets arriving on its Ethernet interface.

---

# 18. Recommended Test Sequence

Test in this order rather than starting with a browser.

## Step 1: Confirm the XDP program is attached

```bash
sudo bpftool net show
```

## Step 2: Confirm maps are populated

```bash
sudo bpftool map dump pinned /sys/fs/bpf/xdp_l7_router/ifindex_map
sudo bpftool map dump pinned /sys/fs/bpf/xdp_l7_router/tx_port
sudo bpftool map dump pinned /sys/fs/bpf/xdp_l7_router/mac_map
```

## Step 3: Test ICMP

From Laptop A:

```bash
ping 8.8.8.8
```

Expected path:

```text
A -> ens8f0 -> XDP -> ens7f0 -> F -> NAT -> Internet
```

## Step 4: Test DNS

```bash
dig @8.8.8.8 example.com
```

Expected path:

```text
A -> ens8f0 -> XDP -> ens7f0 -> F -> DNS
```

## Step 5: Test HTTPS

```bash
curl -v https://example.com
```

Expected XDP decision:

```text
TCP dst=443
CLIENT -> F
```

## Step 6: Test HTTP

```bash
curl -v http://example.com
```

Expected XDP decision:

```text
TCP dst=80
CLIENT -> Z
```

The HTTP connection is expected to fail because Laptop Z is only a monitoring host.

---

# 19. Debugging the XDP Trace

For a HTTPS packet, the relevant sequence should look conceptually like:

```text
packet received ingress=<ens8f0 ifindex>
CLIENT=<ens8f0 ifindex> F=<ens7f0 ifindex> Z=<ens7f1 ifindex>
EtherType=0x800
IPv4 packet
IP protocol=6
packet received from CLIENT
TCP src=<ephemeral> dst=443
HTTPS detected dst=443
CLIENT -> F
MAC rewrite SUCCESS
bpf_redirect_map slot=0
-----------------------------------------
```

For HTTP:

```text
TCP src=<ephemeral> dst=80
HTTP detected dst=80
CLIENT -> Z
MAC rewrite SUCCESS
bpf_redirect_map slot=1
-----------------------------------------
```

For a DNS TCP packet, the earlier trace showed:

```text
TCP src=51644 dst=53
unsupported TCP dst=53 -> DROP
```

That indicated the first version of the classifier was dropping TCP/53. The updated design forwards TCP/53 to F. The same principle applies to UDP/53 and ICMP: they must be explicitly forwarded to F if DNS and ping are part of the connectivity test.

---

# 20. Important Design Limitations

## HTTP is not a monitor copy

The HTTP packet is **redirected** to Z; it is not duplicated. Therefore:

```text
A -> Server -> Z
```

and not:

```text
A -> Server -> F
          \
           -> Z
```

No XDP fan-out is required for the current experiment because HTTP is intentionally allowed to terminate at Z.

## HTTPS classification is port-based

The program identifies HTTPS initially using TCP destination port `443`. This is sufficient for steering the client request to F.

## Return path is interface-based

Traffic arriving from F is sent back to A through `ens8f0` rather than re-classifying it by destination port.

## Server is L2 forwarding only

The server is rewriting Ethernet headers and redirecting frames. Laptop F performs the IP-layer routing and NAT toward Wi-Fi/Internet.

---

# 21. Complete Architecture Summary

```text
                         INTERNET
                            ^
                            |
                        wlp1s0
                            |
                     +------+------+
                     |   Laptop F  |
                     |  NAT/FWD    |
                     |   enp2s0    |
                     +------+------+
                            |
                       ens7f0 / F
                            |
                            v
                  +----------------------+
                  |        SERVER        |
                  |                      |
Laptop A -------->| ens8f0               |
10.200.1.6         |       XDP           |
                  |        |             |
                  |   classify traffic   |
                  |        |             |
                  |   +----+----+        |
                  |   |         |        |
                  | TCP/80   TCP/443     |
                  |   |         |        |
                  |   v         v        |
                  | ens7f1    ens7f0      |
                  +----+---------+--------+
                       |         |
                       v         v
                      Z         F
                 Wireshark     NAT
```

The core mechanism is:

```text
                      Packet arrives
                            |
                            v
                  ctx->ingress_ifindex
                            |
                            v
                     classify packet
                            |
             +--------------+--------------+
             |              |              |
           HTTP          HTTPS           return
          TCP/80         TCP/443          from F
             |              |              |
             v              v              v
            Z               F             A
             |              |              |
             +--------------+--------------+
                            |
                     MAC rewrite
                            |
                            v
                     bpf_redirect_map()
                            |
                            v
                         DEVMAP
                            |
                            v
                     selected egress
```

This is the complete conceptual model of the current lab.
