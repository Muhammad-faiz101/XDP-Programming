# ARP (Address Resolution Protocol) in an XDP Router

## What is ARP?

**ARP (Address Resolution Protocol)** is a Layer 2 (Data Link Layer) protocol used to map an **IPv4 address** to a **MAC address** on the local network.

Ethernet communication requires a destination **MAC address**, while applications communicate using **IP addresses**. ARP bridges this gap.

---

# Why is ARP Needed?

Suppose Laptop A wants to send an IP packet to another device.

It knows:

- Destination IP address
- Its own IP address

However, Ethernet cannot transmit a frame using only an IP address.

An Ethernet frame always requires:

- Source MAC
- Destination MAC

Therefore, before sending the packet, the sender must determine the destination MAC address.

---

# Example Network

```
                 Network 1                     Network 2

 Laptop A                               Laptop B
192.168.1.10                           192.168.2.10
MAC A                                  MAC B
    |                                      |
    |                                      |
 ens7f0                               ens7f1
    |                                      |
    +----------- Linux Server -------------+
          ens7f0          ens7f1
       192.168.1.1     192.168.2.1
```

---

# Case: Laptop A Pings Laptop B

Laptop A executes:

```bash
ping 192.168.2.10
```

Before sending the packet, Laptop A determines whether the destination is on the same subnet.

Current configuration:

```
Laptop A
IP      : 192.168.1.10
Mask    : 255.255.255.0
Subnet  : 192.168.1.0/24
```

Destination:

```
192.168.2.10
```

Since this IP belongs to a **different subnet**, Laptop A decides:

> "This packet must be sent to my default gateway."

The gateway is:

```
192.168.1.1
```

---

# Important Point

Laptop A **does NOT ARP for Laptop B**.

Instead, it ARPs for the **default gateway**.

This is one of the most important concepts in IP networking.

---

# ARP Request

Laptop A broadcasts an ARP request:

```
Who has 192.168.1.1?
Tell 192.168.1.10
```

Ethernet Header:

```
Destination MAC : ff:ff:ff:ff:ff:ff (Broadcast)
Source MAC      : Laptop A MAC
EtherType       : 0x0806 (ARP)
```

ARP Payload:

```
Sender IP   : 192.168.1.10
Sender MAC  : Laptop A MAC

Target IP   : 192.168.1.1
Target MAC  : 00:00:00:00:00:00
```

The destination MAC is the broadcast address so that every device on the local network receives the request.

---

# Packet Processing in the Server

The ARP request arrives at interface `ens7f0`.

Packet flow:

```
NIC
 │
 ▼
XDP Program
 │
 ▼
Linux Networking Stack
 │
 ▼
ARP Subsystem
```

The XDP program checks the Ethernet type:

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

Since it is an ARP packet, the program returns:

```
XDP_PASS
```

This tells XDP:

> "Do not process this packet. Let the Linux kernel handle it."

---

# Linux Handles the ARP Request

The Linux ARP subsystem examines the request.

It checks:

```
Is the requested IP address one of my interface addresses?
```

The request asks for:

```
192.168.1.1
```

Since this is assigned to `ens7f0`, Linux generates an ARP reply.

Example:

```
192.168.1.1 is at

MAC of ens7f0
```

The reply is sent back to Laptop A.

---

# ARP Cache Update

Laptop A stores the received mapping in its ARP cache.

Example:

```
IP Address        MAC Address
-----------------------------------------
192.168.1.1   ->  b4:96:91:12:9d:44
```

Future packets can now be sent directly to the gateway without sending another ARP request.

---

# Sending the ICMP Packet

Laptop A now constructs the ping packet.

Ethernet Header:

```
Destination MAC : Gateway MAC
Source MAC      : Laptop A MAC
```

IP Header:

```
Source IP      : 192.168.1.10
Destination IP : 192.168.2.10
```

Notice the difference:

- Ethernet destination is the **gateway**.
- IP destination is **Laptop B**.

This is exactly how IP routing works.

---

# How XDP Handles the IPv4 Packet

The packet reaches the XDP program.

The program checks:

```c
if (eth->h_proto == bpf_htons(ETH_P_IP))
```

Since it is an IPv4 packet, the program:

1. Looks up the output interface in `tx_port`.
2. Looks up the MAC addresses in `mac_map`.
3. Rewrites the Ethernet header.
4. Redirects the packet using `bpf_redirect_map()`.

Only the Ethernet header is modified.

Example:

Before:

```
Ethernet

Dst MAC : Gateway
Src MAC : Laptop A

IP

Src IP : 192.168.1.10
Dst IP : 192.168.2.10
```

After XDP:

```
Ethernet

Dst MAC : Laptop B
Src MAC : ens7f1

IP

Src IP : 192.168.1.10
Dst IP : 192.168.2.10
```

The IP header remains completely unchanged.

---

# Why Doesn't the XDP Program Process ARP?

The XDP program intentionally passes all ARP packets to the Linux kernel.

Reasons:

- Linux already provides a complete ARP implementation.
- Linux maintains the ARP (neighbor) cache.
- Linux automatically generates ARP replies.
- The XDP program focuses only on high-speed packet forwarding.

Handling ARP inside XDP would require implementing:

- ARP request parsing
- ARP reply generation
- Neighbor cache management
- Timeout handling
- Entry updates

This greatly increases complexity and is unnecessary for this example.

---

# Important Limitation of This XDP Program

The XDP program **does not discover MAC addresses**.

Instead, the required MAC addresses are manually inserted into the `mac_map` by user space:

```bash
bpftool map update pinned ... mac_map ...
```

Therefore, the program already knows:

- Destination MAC
- Source MAC

When forwarding a packet, it simply performs a map lookup.

There is **no ARP lookup** inside the XDP program.

---

# Linux Router vs XDP Router

## Normal Linux Router

```
Packet arrives
      │
      ▼
Routing Table Lookup
      │
      ▼
Neighbor (ARP) Table Lookup
      │
      ▼
If MAC unknown
      │
      ▼
Send ARP Request
      │
      ▼
Receive ARP Reply
      │
      ▼
Forward Packet
```

Linux dynamically learns neighbor MAC addresses.

---

## This XDP Router

```
Packet arrives
      │
      ▼
Lookup tx_port Map
      │
      ▼
Lookup mac_map
      │
      ▼
Rewrite Ethernet Header
      │
      ▼
Redirect Packet
```

No routing table lookup is performed.

No ARP lookup is performed.

No neighbor table is consulted.

Everything needed for forwarding is already stored inside BPF maps.

---

# Key Takeaways

- ARP resolves an IPv4 address into a MAC address.
- Ethernet communication always requires MAC addresses.
- A host ARPs only for devices on its own subnet.
- For remote networks, a host ARPs for the default gateway, **not the final destination**.
- The XDP program receives ARP packets first but returns `XDP_PASS`, allowing Linux to process them.
- Linux automatically replies to ARP requests for its own interface IP addresses.
- The XDP program never performs ARP resolution.
- MAC addresses used for forwarding are manually stored in `mac_map`.
- Only the Ethernet header is rewritten; the IP packet remains unchanged.
- Packet forwarding is performed directly by `bpf_redirect_map()` without traversing the Linux routing stack.