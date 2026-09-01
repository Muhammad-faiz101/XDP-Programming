# XDP TCP TTL Manipulation & Packet Redirection

## Objective

Implement an XDP/eBPF program that:

* Redirects packets between two Ethernet interfaces.
* Modifies Ethernet source/destination MAC addresses.
* Identifies IPv4 TCP packets.
* Rewrites the IPv4 TTL to a custom value.
* Updates the IPv4 checksum using an incremental checksum technique.
* Preserves the TCP checksum.

---

## Topology

```text
       Laptop A
           |
        Ethernet
           |
        ens7f0
       ifindex 8
           |
           v
   +----------------+
   |    XDP Server  |
   |                |
   |  XDP Program   |
   |                |
   |  TTL -> 10     |
   |  Checksum      |
   |  MAC Rewrite   |
   +----------------+
           |
        ens7f1
       ifindex 9
           |
        Ethernet
           |
       Laptop B
```

The same XDP program handles traffic in both directions.

---

## Packet Processing

```text
Ethernet
    ↓
Check Ethernet header
    ↓
IPv4?
    ↓
TCP?
    ↓
Read old TTL
    ↓
Set TTL = 10
    ↓
Incrementally update IPv4 checksum
    ↓
Rewrite Ethernet MAC addresses
    ↓
Redirect packet
```

ARP packets are passed normally because ARP is required for network communication.

---

## TTL Manipulation

The custom TTL is defined using:

```c
#define CUSTOM_TTL 10
```

For example:

```text
Original TTL = 64
Custom TTL   = 10
```

The TTL is an 8-bit field, but the Internet checksum operates on 16-bit words. Therefore, the TTL is processed together with the adjacent Protocol field.

For TCP:

```text
Old word = 0x4006    // TTL 64 + TCP protocol 6
New word = 0x0A06    // TTL 10 + TCP protocol 6
```

---

## Incremental Checksum

Changing TTL modifies the IPv4 header, so the IPv4 checksum must also be updated.

Instead of recalculating the complete IPv4 checksum, the old and new 16-bit words are used:

```text
HC' = ~(~HC + ~old_word + new_word)
```

The important operations are:

```c
sum = (~bpf_ntohs(iph->check) & 0xffff);

sum += (~old_word & 0xffff);
sum += new_word;

sum = (sum & 0xffff) + (sum >> 16);
sum = (sum & 0xffff) + (sum >> 16);

iph->check = bpf_htons(~sum);
```

Only the **IPv4 checksum** needs to change. The TCP checksum remains unchanged because TTL is not part of the TCP checksum calculation.

---

## Important Files

```text
xdp_redirect.c     → XDP source code
xdp_redirect.o     → Compiled eBPF object
ttl_ins.md         → Project documentation
```

---

## Compilation

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

Compiles the C program into an eBPF object file.

---

## Attach XDP

```bash
sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp
sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp
```

Attaches the XDP program to both interfaces.

---

## Verify XDP

```bash
ip -details link show ens7f0
ip -details link show ens7f1
```

Checks whether the XDP program is attached.

---

## Remove XDP

```bash
sudo ip link set dev ens7f0 xdp off
sudo ip link set dev ens7f1 xdp off
```

Detaches the XDP program.

---

## Testing

Generate TCP traffic:

```bash
curl http://<destination-ip>
```

Capture packets:

```bash
sudo tcpdump -i ens7f0 -nn -v tcp
```

```bash
sudo tcpdump -i ens7f1 -nn -v tcp
```

Verify that:

```text
TTL = 10
IPv4 checksum = updated
TCP checksum = unchanged
Source MAC = rewritten
Destination MAC = rewritten
```

Wireshark can be used for detailed packet inspection.

---

## Challenges & Solutions

### 1. Incorrect Interface Index

The XDP program uses interface indexes:

```c
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9
```

Check the actual indexes with:

```bash
ip link
```

Update the values if they differ.

### 2. Packet Header Bounds

XDP accesses raw packet memory, so every header must be checked against `data_end` to satisfy the eBPF verifier and avoid invalid memory access.

### 3. IPv4 Header Length

IPv4 headers are not always 20 bytes. The program uses:

```c
iph->ihl * 4
```

to determine the actual header size.

### 4. Checksum After TTL Modification

Changing TTL invalidates the original IPv4 checksum. An incremental checksum update is used instead of recalculating the complete checksum.

### 5. ARP Connectivity

ARP packets are passed using:

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

This allows normal ARP resolution to continue.

---

## Result

The final packet transformation is:

```text
                BEFORE
                  |
            TTL = 64
            IP Checksum = OLD
                  |
                 XDP
                  |
                  v
                AFTER
            TTL = 10
            IP Checksum = NEW
            TCP Checksum = SAME
            MAC Addresses = REWRITTEN
                  |
                  v
              REDIRECT
```

This demonstrates low-level packet manipulation and forwarding using XDP/eBPF.
