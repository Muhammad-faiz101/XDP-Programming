# XDP TCP TTL Manipulation — Build, Load, Verify, Teardown

XDP program that intercepts IPv4 TCP packets, rewrites the IPv4 TTL to a
custom value, performs an incremental IPv4 checksum update, rewrites
Ethernet MAC addresses, and redirects packets between `ens7f0` and
`ens7f1`.

ARP packets are passed to the kernel stack. Non-IPv4 traffic is dropped.

---

## 0. Objective

The objective is to manipulate IPv4 TCP packets at the XDP layer while
preserving packet integrity.

The program:

* Detects IPv4 packets.
* Processes TCP packets only.
* Rewrites TTL to a configurable value (`10`).
* Updates the IPv4 checksum incrementally.
* Keeps the TCP checksum unchanged.
* Rewrites Ethernet source/destination MAC addresses.
* Redirects packets between two network interfaces.

---

## 1. Topology

```text
                         XDP SERVER
                    ┌─────────────────┐
                    │                 │
        ens7f0      │    XDP Program  │      ens7f1
      ifindex 8     │                 │     ifindex 9
    ───────────────►│  TTL → 10       │──────────────►
                    │  Checksum       │
                    │  MAC Rewrite   │
                    │  Redirect       │
                    └─────────────────┘
                         ▲       │
                         │       │
                    ┌────┘       └────┐
                    │                 │
                Laptop A          Laptop B
```

Traffic in the reverse direction is processed in the same way.

---

## 2. Prerequisites

* Linux system with XDP/eBPF support
* `clang` with BPF target support
* `llvm`
* `libbpf-dev`
* `bpftool`
* `tcpdump`
* Root/sudo access

Install required packages:

```bash
sudo apt update
sudo apt install clang llvm libbpf-dev linux-headers-$(uname -r) bpftool tcpdump
```

**Explanation:** Installs the compiler, BPF libraries, kernel headers, BPF
tools, and packet-capture utilities.

---

## 3. Check Network Interfaces

```bash
ip link
```

**Explanation:** Displays interface names, states, MAC addresses, and indexes.

Check the individual interfaces:

```bash
ip link show ens7f0
ip link show ens7f1
```

**Explanation:** Displays information for the two interfaces used by the XDP
router.

The program currently expects:

```text
ens7f0 → ifindex 8
ens7f1 → ifindex 9
```

Verify the actual values:

```bash
cat /sys/class/net/ens7f0/ifindex
cat /sys/class/net/ens7f1/ifindex
```

If the indexes are different, update:

```c
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9
```

---

## 4. Source: `xdp_redirect.c`

The main configuration is:

```c
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9

#define CUSTOM_TTL 10
```

`CUSTOM_TTL` controls the TTL written into TCP/IPv4 packets.

For example:

```c
#define CUSTOM_TTL 20
```

changes the rewritten TTL from `10` to `20`.

---

## 5. Packet Processing

```text
Incoming Ethernet Frame
          │
          ▼
    Ethernet Header
          │
          ▼
       IPv4?
       /    \
     NO      YES
     │        │
   DROP       ▼
           TCP?
          /    \
        NO      YES
        │        │
      PASS       ▼
             Read old TTL
                  │
                  ▼
             TTL → 10
                  │
                  ▼
       Incremental checksum
                  │
                  ▼
           Rewrite MACs
                  │
                  ▼
              Redirect
```

ARP packets are passed using `XDP_PASS`.

---

## 6. TTL and Checksum

The original TTL is saved before modifying the packet.

Example:

```text
Old TTL = 64
New TTL = 10
```

Because TTL is part of the IPv4 header, changing it requires updating the
IPv4 checksum.

The incremental checksum update uses:

```text
HC' = ~(~HC + ~old_word + new_word)
```

For TCP:

```text
Old word = 0x4006
New word = 0x0A06
```

where:

```text
0x40 = TTL 64
0x0A = TTL 10
0x06 = TCP protocol
```

The new checksum is written back to:

```c
iph->check
```

The TCP checksum is **not modified**, because IPv4 TTL is not included in
the TCP checksum calculation.

---

## 7. Compile

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

**Explanation:** Compiles the XDP C source into an optimized eBPF object
file.

Verify the generated object:

```bash
llvm-objdump -h xdp_redirect.o
```

**Explanation:** Displays the sections contained in the eBPF object.

A valid object should contain a non-empty `xdp` section.

---

## 8. Load and Attach

Attach the program to `ens7f0`:

```bash
sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Loads and attaches the XDP program to `ens7f0`.

Attach it to `ens7f1`:

```bash
sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Loads and attaches the XDP program to `ens7f1`.

---

## 9. Verify Attachment

```bash
ip -details link show ens7f0
ip -details link show ens7f1
```

**Explanation:** Verifies that an XDP program is attached to both interfaces.

Alternatively:

```bash
sudo bpftool net show
```

**Explanation:** Displays BPF/XDP programs attached to network interfaces.

---

## 10. Verify the Loaded Program

```bash
sudo bpftool prog show
```

**Explanation:** Lists BPF programs currently loaded in the kernel.

---

## 11. Test TCP Traffic

Generate TCP traffic from Laptop A:

```bash
curl http://<destination-ip>
```

**Explanation:** Generates TCP traffic that can be processed by the XDP
program.

Capture traffic on the second interface:

```bash
sudo tcpdump -i ens7f1 -nn -e -v tcp
```

**Explanation:** Captures TCP packets and displays Ethernet and verbose IPv4
information.

Verify:

```text
TTL = 10
```

Also verify the rewritten source and destination MAC addresses.

---

## 12. Wireshark Verification

Useful Wireshark filter:

```text
tcp
```

**Explanation:** Displays TCP packets only.

IPv4 TCP filter:

```text
ip.proto == 6
```

**Explanation:** Displays IPv4 packets whose protocol is TCP.

Check the following fields:

```text
IPv4
 ├── TTL              → 10
 └── Header Checksum  → Valid/updated

TCP
 └── Checksum         → Unchanged

Ethernet
 ├── Source MAC       → Rewritten
 └── Destination MAC  → Rewritten
```

---

## 13. ARP Testing

ARP is passed directly:

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

Check ARP traffic:

```bash
sudo tcpdump -i ens7f0 -nn arp
```

**Explanation:** Captures ARP packets to verify that ARP traffic is not
being modified or redirected by the XDP program.

---

## 14. Non-TCP Traffic

The TTL modification applies only to TCP.

```text
IPv4 TCP   → TTL rewritten
IPv4 UDP   → TTL unchanged
IPv4 ICMP  → TTL unchanged
ARP        → XDP_PASS
Non-IPv4   → XDP_DROP
```

---

## 15. Challenges & Solutions

### 15.1 Packet Data Bounds

XDP directly accesses packet memory, so headers must be checked against
`data_end`.

```c
if ((void *)(eth + 1) > data_end)
    return XDP_DROP;
```

This prevents invalid memory access and satisfies the eBPF verifier.

### 15.2 Variable IPv4 Header Length

An IPv4 header is not always exactly 20 bytes.

The program uses:

```c
iph->ihl * 4
```

to calculate the actual IPv4 header length.

### 15.3 TTL Modification Invalidates IPv4 Checksum

Changing TTL changes the IPv4 header, so the original checksum is no
longer valid.

An incremental checksum update is therefore performed instead of
recalculating the entire header checksum.

### 15.4 Old TTL Must Be Preserved

The checksum calculation requires both the old and new values.

Therefore:

```text
Read old TTL
      ↓
Calculate checksum
      ↓
Write new checksum
      ↓
Write new TTL
```

### 15.5 Interface Index Differences

Interface indexes can differ between systems.

Always verify using:

```bash
cat /sys/class/net/ens7f0/ifindex
cat /sys/class/net/ens7f1/ifindex
```

and update the source code if necessary.

---

## 16. Teardown

Remove XDP from `ens7f0`:

```bash
sudo ip link set dev ens7f0 xdp off
```

**Explanation:** Detaches the XDP program from `ens7f0`.

Remove XDP from `ens7f1`:

```bash
sudo ip link set dev ens7f1 xdp off
```

**Explanation:** Detaches the XDP program from `ens7f1`.

Verify:

```bash
sudo bpftool net show
```

**Explanation:** Confirms that the XDP program is no longer attached.

---

## 17. Complete Workflow

```text
1. Check interfaces
       ↓
2. Verify ifindexes/MACs
       ↓
3. Compile xdp_redirect.c
       ↓
4. Verify xdp_redirect.o
       ↓
5. Attach XDP to ens7f0/ens7f1
       ↓
6. Verify attachment
       ↓
7. Generate TCP traffic
       ↓
8. Capture packets
       ↓
9. Verify TTL = 10
       ↓
10. Verify IPv4 checksum
       ↓
11. Verify MAC rewriting
       ↓
12. Detach XDP
```

---

## 18. Expected Result

For an incoming TCP/IPv4 packet:

```text
Original Packet
    │
    ├── TTL = 64
    ├── IPv4 checksum = OLD
    └── TCP checksum = X
    │
    ▼
    XDP
    │
    ├── TTL = 10
    ├── IPv4 checksum = NEW
    ├── TCP checksum = X
    ├── Source MAC = rewritten
    └── Destination MAC = rewritten
    │
    ▼
Redirected Packet
```

The packet is therefore forwarded with the custom TTL while maintaining a
valid IPv4 checksum and preserving the TCP checksum.
