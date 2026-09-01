# XDP Packet Redirection and TCP TTL Manipulation

## Project Overview

This project demonstrates **high-performance packet processing using XDP (eXpress Data Path)** and eBPF on Linux.

The XDP program runs directly at the network interface and processes packets before they travel through the normal Linux networking stack.

The current implementation performs:

* Ethernet frame inspection
* IPv4 packet identification
* TCP packet identification
* Custom IPv4 TTL rewriting
* Incremental IPv4 checksum update
* Ethernet source MAC rewriting
* Ethernet destination MAC rewriting
* Packet redirection between two Ethernet interfaces
* ARP pass-through
* Packet monitoring using `tcpdump` and Wireshark

The current custom TTL value is:

```c
#define CUSTOM_TTL 10
```

---

# 1. Objective

The main objective of this project is to develop an XDP-based packet manipulation and forwarding mechanism capable of modifying selected fields of network packets at high speed.

Specifically, the project aims to:

1. Understand how packets are received by a Linux network interface.
2. Intercept packets using XDP.
3. Identify IPv4 packets.
4. Identify TCP packets within IPv4 traffic.
5. Read the original IPv4 TTL.
6. Replace the TTL with a custom value.
7. Correctly update the IPv4 header checksum.
8. Preserve the TCP checksum when only the TTL is changed.
9. Rewrite Ethernet MAC addresses.
10. Redirect packets from one Ethernet interface to another.
11. Test and verify the modified packets using packet-capture tools.
12. Understand practical challenges involved in low-level packet manipulation.

---

# 2. Technologies Used

| Technology | Purpose                                    |
| ---------- | ------------------------------------------ |
| Linux      | Operating system                           |
| XDP        | High-performance packet processing         |
| eBPF       | Execution environment for the XDP program  |
| C          | XDP program implementation                 |
| Clang      | Compiling C code into eBPF                 |
| LLVM       | Inspecting/disassembling eBPF object files |
| libbpf     | eBPF helper definitions                    |
| `iproute2` | Network/interface management               |
| tcpdump    | Packet capture and debugging               |
| Wireshark  | Detailed packet inspection                 |

---

# 3. Network Topology

The setup consists of:

* One Linux XDP server
* Two Ethernet interfaces on the XDP server
* Laptop A
* Laptop B

```text
                         ┌──────────────────────┐
                         │      XDP SERVER      │
                         │      Linux PC        │
                         │                      │
                         │   XDP Program        │
                         │                      │
                         │ ens7f0    ens7f1     │
                         │  ifindex 8  ifindex 9 │
                         └──────┬────────┬──────┘
                                │        │
                                │        │
                             Ethernet Ethernet
                                │        │
                                │        │
                         ┌──────▼───┐ ┌──▼──────┐
                         │ Laptop A │ │ Laptop B│
                         └──────────┘ └─────────┘
```

---

# 4. Packet Flow

## Laptop A → Laptop B

```text
Laptop A
    │
    ▼
ens7f0
    │
    ▼
XDP Program
    │
    ├── Ethernet validation
    ├── IPv4 validation
    ├── TCP detection
    ├── Read old TTL
    ├── Change TTL → 10
    ├── Update IPv4 checksum
    ├── Rewrite MAC addresses
    │
    ▼
ens7f1
    │
    ▼
Laptop B
```

## Laptop B → Laptop A

```text
Laptop B
    │
    ▼
ens7f1
    │
    ▼
XDP Program
    │
    ├── Ethernet validation
    ├── IPv4 validation
    ├── TCP detection
    ├── Read old TTL
    ├── Change TTL → 10
    ├── Update IPv4 checksum
    ├── Rewrite MAC addresses
    │
    ▼
ens7f0
    │
    ▼
Laptop A
```

---

# 5. Interface Configuration

The current program uses:

```text
ens7f0 → Interface index 8
ens7f1 → Interface index 9
```

These values are defined in the program:

```c
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9
```

Interface indexes can be different on another machine.

Always check them using:

```bash
ip link
```

**Explanation:** Displays all network interfaces, their states, MAC addresses, and interface indexes.

---

# 6. Basic Linux Network Commands

## Display Interfaces

```bash
ip link
```

**Explanation:** Displays all available network interfaces and their interface indexes.

---

## Display a Specific Interface

```bash
ip link show ens7f0
```

**Explanation:** Displays detailed information about `ens7f0`.

```bash
ip link show ens7f1
```

**Explanation:** Displays detailed information about `ens7f1`.

---

## Display IP Addresses

```bash
ip addr show ens7f0
```

**Explanation:** Displays IPv4 and IPv6 addresses assigned to `ens7f0`.

```bash
ip addr show ens7f1
```

**Explanation:** Displays IPv4 and IPv6 addresses assigned to `ens7f1`.

---

## Display MAC Addresses

```bash
ip link show ens7f0
```

**Explanation:** The `link/ether` value in the output shows the MAC address of `ens7f0`.

```bash
ip link show ens7f1
```

**Explanation:** The `link/ether` value shows the MAC address of `ens7f1`.

---

## Display Routing Table

```bash
ip route
```

**Explanation:** Displays the Linux IPv4 routing table.

---

## Display ARP/Neighbor Table

```bash
ip neigh
```

**Explanation:** Displays IPv4 neighbor entries used for Layer-2 address resolution.

---

## Bring Interfaces Up

```bash
sudo ip link set ens7f0 up
```

**Explanation:** Enables the `ens7f0` interface.

```bash
sudo ip link set ens7f1 up
```

**Explanation:** Enables the `ens7f1` interface.

---

# 7. Installing Required Packages

Update the package repository information:

```bash
sudo apt update
```

**Explanation:** Updates Ubuntu's local package index.

Install the required tools:

```bash
sudo apt install clang llvm libbpf-dev linux-headers-$(uname -r) bpftool tcpdump
```

**Explanation:** Installs the compiler, LLVM tools, libbpf headers, kernel headers, BPF utilities, and tcpdump.

Check Clang:

```bash
clang --version
```

**Explanation:** Displays the installed Clang version.

Check LLVM:

```bash
llvm-objdump --version
```

**Explanation:** Verifies that LLVM's object-file inspection tool is installed.

Check bpftool:

```bash
bpftool version
```

**Explanation:** Displays the installed bpftool version.

---

# 8. XDP Program

The source file is:

```text
xdp_redirect.c
```

The program performs:

```text
Ethernet
   ↓
IPv4
   ↓
TCP
   ↓
TTL rewriting
   ↓
IPv4 checksum update
   ↓
MAC rewriting
   ↓
XDP redirect
```

## Complete Code

```c
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9

#define CUSTOM_TTL 10

static __always_inline void rewrite_ttl(struct iphdr *iph)
{
    __u16 old_word;
    __u16 new_word;
    __u32 sum;

    old_word = ((__u16)iph->ttl << 8) | iph->protocol;

    new_word = ((__u16)CUSTOM_TTL << 8) | iph->protocol;

    sum = (~bpf_ntohs(iph->check) & 0xffff);

    sum += (~old_word & 0xffff);
    sum += new_word;

    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);

    iph->check = bpf_htons(~sum);

    iph->ttl = CUSTOM_TTL;
}

SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    if (eth->h_proto == bpf_htons(ETH_P_ARP))
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_DROP;

    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_DROP;

    if (iph->ihl < 5)
        return XDP_DROP;

    if ((void *)iph + (iph->ihl * 4) > data_end)
        return XDP_DROP;

    if (iph->protocol == IPPROTO_TCP)
        rewrite_ttl(iph);

    if (ctx->ingress_ifindex == ENS7F0_IFINDEX) {

        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xe9, 0xf9
            },
            ETH_ALEN
        );

        __builtin_memcpy(
            eth->h_source,
            (unsigned char[]){
                0xb4, 0x96, 0x91,
                0x12, 0x9d, 0x46
            },
            ETH_ALEN
        );

        return bpf_redirect(ENS7F1_IFINDEX, 0);
    }

    if (ctx->ingress_ifindex == ENS7F1_IFINDEX) {

        __builtin_memcpy(
            eth->h_dest,
            (unsigned char[]){
                0x50, 0xa1, 0x32,
                0x76, 0xde, 0xbb
            },
            ETH_ALEN
        );

        __builtin_memcpy(
            eth->h_source,
            (unsigned char[]){
                0xb4, 0x96, 0x91,
                0x12, 0x9d, 0x44
            },
            ETH_ALEN
        );

        return bpf_redirect(ENS7F0_IFINDEX, 0);
    }

    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

---

# 9. Understanding TTL Rewriting

TTL is an 8-bit field in the IPv4 header.

For this project, the TTL is not simply decremented.

The program replaces the existing TTL with a custom value.

For example:

```text
Before:

TTL = 64
```

After:

```text
TTL = 10
```

The custom value is controlled by:

```c
#define CUSTOM_TTL 10
```

Changing it to:

```c
#define CUSTOM_TTL 20
```

will change TCP/IPv4 packets to:

```text
TTL = 20
```

---

# 10. Why the IPv4 Checksum Must Change

The IPv4 checksum covers the IPv4 header.

TTL is part of that header.

Therefore:

```text
TTL changes
     ↓
IPv4 header changes
     ↓
IPv4 checksum becomes invalid
     ↓
IPv4 checksum must be updated
```

The TCP checksum does not need to change simply because TTL changes.

```text
IPv4:
TTL       → Changed
Checksum  → Changed

TCP:
Checksum  → Unchanged
```

---

# 11. Incremental Checksum

Instead of recalculating the entire IPv4 checksum, the program performs an incremental update.

The Internet checksum uses 16-bit words.

TTL and Protocol are adjacent in the IPv4 header:

```text
┌───────────────────┬───────────────────┐
│ TTL               │ Protocol          │
│ 8 bits            │ 8 bits            │
└───────────────────┴───────────────────┘
```

For TCP:

```text
Protocol = 6
```

If:

```text
TTL = 64
```

then:

```text
64 = 0x40
```

and the 16-bit word is:

```text
0x4006
```

After changing TTL to 10:

```text
10 = 0x0A
```

and the new word is:

```text
0x0A06
```

Therefore:

```text
Old word = 0x4006
New word = 0x0A06
```

---

# 12. Incremental Checksum Formula

The update is based on:

```text
HC' = ~(~HC + ~m + m')
```

Where:

```text
HC  = old checksum
m   = old 16-bit word
m'  = new 16-bit word
HC' = new checksum
```

The program implements this using:

```c
sum = (~bpf_ntohs(iph->check) & 0xffff);

sum += (~old_word & 0xffff);
sum += new_word;
```

The carry is then folded:

```c
sum = (sum & 0xffff) + (sum >> 16);
sum = (sum & 0xffff) + (sum >> 16);
```

Finally, the new checksum is written into the packet:

```c
iph->check = bpf_htons(~sum);
```

Then the TTL is overwritten:

```c
iph->ttl = CUSTOM_TTL;
```

---

# 13. Why the Old TTL Is Needed

Suppose the incoming packet contains:

```text
TTL = 64
```

and we want:

```text
TTL = 10
```

The checksum calculation needs:

```text
Old TTL = 64
New TTL = 10
```

Therefore, we cannot overwrite the TTL before obtaining its original value.

The logical process is:

```text
Read old TTL
     ↓
Build old word
     ↓
Build new word
     ↓
Calculate new checksum
     ↓
Write new checksum
     ↓
Write new TTL
```

---

# 14. Compiling the Program

Compile the source file:

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

**Explanation:** Compiles the C source into an optimized eBPF object file.

The output is:

```text
xdp_redirect.o
```

---

## Compile With Warnings

```bash
clang -O2 -g -Wall -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

**Explanation:** Compiles the program with optimization, debugging information, and compiler warnings.

---

# 15. Verify the Object File

```bash
llvm-objdump -h xdp_redirect.o
```

**Explanation:** Displays the sections inside the generated eBPF object file.

You should find the XDP section generated by:

```c
SEC("xdp")
```

---

# 16. Disassemble the eBPF Program

```bash
llvm-objdump -S xdp_redirect.o
```

**Explanation:** Displays the generated eBPF instructions along with source information.

This is useful for understanding what Clang generated from the C program.

---

# 17. Loading the XDP Program

Attach the program to `ens7f0`:

```bash
sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Loads and attaches the XDP program to `ens7f0`.

Attach the program to `ens7f1`:

```bash
sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Loads and attaches the XDP program to `ens7f1`.

---

# 18. Verify XDP Attachment

```bash
ip -details link show ens7f0
```

**Explanation:** Displays detailed information about `ens7f0`, including XDP status.

```bash
ip -details link show ens7f1
```

**Explanation:** Displays detailed information about `ens7f1`, including XDP status.

---

# 19. Using bpftool

List loaded BPF programs:

```bash
sudo bpftool prog show
```

**Explanation:** Displays BPF programs currently loaded into the kernel.

List BPF maps:

```bash
sudo bpftool map show
```

**Explanation:** Displays BPF maps currently loaded into the kernel.

---

# 20. Removing the XDP Program

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

---

# 21. Packet Capture With tcpdump

Capture packets on `ens7f0`:

```bash
sudo tcpdump -i ens7f0 -nn -e
```

**Explanation:** Captures packets while displaying Ethernet information without DNS/service-name resolution.

Capture packets on `ens7f1`:

```bash
sudo tcpdump -i ens7f1 -nn -e
```

**Explanation:** Captures Ethernet frames arriving on `ens7f1`.

---

# 22. Capture TCP Packets

```bash
sudo tcpdump -i ens7f0 -nn tcp
```

**Explanation:** Captures TCP packets on `ens7f0`.

```bash
sudo tcpdump -i ens7f1 -nn tcp
```

**Explanation:** Captures TCP packets on `ens7f1`.

---

# 23. Display TTL

```bash
sudo tcpdump -i ens7f1 -nn -v tcp
```

**Explanation:** Captures TCP packets and displays verbose IPv4 information such as TTL.

Expected output should contain something similar to:

```text
IP 192.168.x.x > 192.168.x.x: Flags [...], ttl 10, ...
```

The important value is:

```text
ttl 10
```

---

# 24. Testing TCP Traffic

The XDP program modifies TCP/IPv4 traffic, so TCP traffic should be generated during testing.

For example:

```bash
curl http://<destination-ip>
```

**Explanation:** Generates TCP traffic that can pass through the XDP program.

Another option:

```bash
nc <destination-ip> <port>
```

**Explanation:** Creates a TCP connection to the specified destination and port.

---

# 25. Ping Limitation

You can use:

```bash
ping <destination-ip>
```

**Explanation:** Generates ICMP traffic to test basic connectivity.

However, the current program only modifies:

```text
IPv4 + TCP
```

Therefore, ICMP packets will **not** have their TTL rewritten.

```text
TCP     → TTL modified
UDP     → TTL unchanged
ICMP    → TTL unchanged
ARP     → Passed
```

---

# 26. Wireshark Testing

Wireshark can be used to inspect:

* Ethernet source MAC
* Ethernet destination MAC
* IPv4 source address
* IPv4 destination address
* TTL
* IPv4 checksum
* TCP checksum
* TCP source port
* TCP destination port
* TCP flags

Useful filter:

```text
tcp
```

**Explanation:** Displays TCP packets only.

IPv4 filter:

```text
ip
```

**Explanation:** Displays IPv4 packets.

TCP protocol filter:

```text
ip.proto == 6
```

**Explanation:** Displays IPv4 packets using TCP.

Specific IP:

```text
ip.addr == <IP_ADDRESS>
```

**Explanation:** Displays packets where the specified IP is the source or destination.

---

# 27. Expected Packet Transformation

## Before XDP

```text
Ethernet
├── Source MAC:      Laptop A
├── Destination MAC: XDP Server
│
IPv4
├── Source IP:       A
├── Destination IP:  B
├── TTL:              64
├── Protocol:         TCP
└── Checksum:         OLD
│
TCP
├── Source Port
├── Destination Port
└── TCP Checksum
```

## After XDP

```text
Ethernet
├── Source MAC:      XDP Server
├── Destination MAC: Laptop B
│
IPv4
├── Source IP:       A
├── Destination IP:  B
├── TTL:              10
├── Protocol:         TCP
└── Checksum:         NEW
│
TCP
├── Source Port
├── Destination Port
└── TCP Checksum
```

---

# 28. Challenges and Solutions

## Challenge 1 — Identifying the Correct Network Interfaces

### Problem

The XDP program depends on specific interfaces:

```text
ens7f0
ens7f1
```

and specific interface indexes.

If the indexes are incorrect, the redirect logic will not work correctly.

### Solution

Run:

```bash
ip link
```

**Explanation:** Displays the interface indexes and names.

Then update:

```c
#define ENS7F0_IFINDEX 8
#define ENS7F1_IFINDEX 9
```

with the correct values.

---

# 29. Challenge 2 — Network Interface Activation

### Problem

An Ethernet interface may appear as DOWN or may fail to activate.

### Solution

Check:

```bash
ip link
```

**Explanation:** Shows whether the interface is UP or DOWN.

Bring it up:

```bash
sudo ip link set ens7f0 up
sudo ip link set ens7f1 up
```

**Explanation:** Enables both Ethernet interfaces.

If NetworkManager is managing the interfaces, also inspect:

```bash
nmcli device status
```

**Explanation:** Displays the NetworkManager state of each network device.

---

# 30. Challenge 3 — ARP Connectivity

### Problem

Before normal IP communication can occur, devices need to resolve IP addresses to MAC addresses using ARP.

Dropping ARP packets can prevent connectivity.

### Solution

The program explicitly allows ARP:

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

This allows ARP traffic to pass through normally.

Check the ARP table:

```bash
ip neigh
```

**Explanation:** Displays the current IPv4 neighbor/ARP entries.

---

# 31. Challenge 4 — Packet Header Bounds

### Problem

XDP operates directly on packet memory.

The packet may be shorter than the expected header.

Accessing memory beyond `data_end` can cause the eBPF verifier to reject the program.

### Solution

The program checks:

```c
if ((void *)(eth + 1) > data_end)
    return XDP_DROP;
```

and:

```c
if ((void *)(iph + 1) > data_end)
    return XDP_DROP;
```

It also validates the variable IPv4 header length:

```c
if (iph->ihl < 5)
    return XDP_DROP;

if ((void *)iph + (iph->ihl * 4) > data_end)
    return XDP_DROP;
```

These checks make packet parsing safer and verifier-friendly.

---

# 32. Challenge 5 — IPv4 Header Length Is Not Always 20 Bytes

### Problem

An IPv4 header normally has a minimum size of 20 bytes, but IPv4 options can make it larger.

Assuming:

```text
IPv4 header = 20 bytes
```

for every packet can cause incorrect parsing.

### Solution

Use:

```c
iph->ihl * 4
```

because `ihl` specifies the IPv4 header length in 32-bit words.

For example:

```text
IHL = 5
5 × 4 = 20 bytes
```

---

# 33. Challenge 6 — Updating the Checksum

### Problem

Changing TTL changes the IPv4 header.

Leaving the old checksum untouched produces an invalid IPv4 header checksum.

### Solution

Use incremental checksum updating.

Instead of recalculating every IPv4 header word:

```text
Old checksum
     ↓
Remove old TTL contribution
     ↓
Add new TTL contribution
     ↓
New checksum
```

This is faster and appropriate for packet manipulation.

---

# 34. Challenge 7 — Understanding the Old and New Values

### Problem

The incremental checksum algorithm requires both:

```text
old value
new value
```

If TTL is overwritten before the old value is saved, the original TTL is lost.

### Solution

Construct:

```c
old_word = ((__u16)iph->ttl << 8) | iph->protocol;
```

before changing:

```c
iph->ttl
```

Then construct:

```c
new_word = ((__u16)CUSTOM_TTL << 8) | iph->protocol;
```

The checksum is calculated from these two values.

---

# 35. Challenge 8 — TTL Is Only 8 Bits

### Problem

The Internet checksum operates on 16-bit words, while TTL is only 8 bits.

### Solution

TTL is combined with the adjacent Protocol field:

```text
┌──────────────┬──────────────┐
│ TTL          │ Protocol     │
│ 8 bits       │ 8 bits       │
└──────────────┴──────────────┘
```

For TCP:

```text
Protocol = 6
```

For TTL 64:

```text
0x40 + 0x06 = 0x4006
```

For TTL 10:

```text
0x0A + 0x06 = 0x0A06
```

---

# 36. Challenge 9 — TCP Checksum

### Problem

It may initially seem that changing any IPv4 field requires recalculating TCP's checksum.

### Solution

TTL is not included in the TCP pseudo-header.

Therefore, changing only IPv4 TTL does not require changing the TCP checksum.

```text
IPv4 TTL
    ↓
IPv4 checksum changes

TCP checksum
    ↓
Remains unchanged
```

---

# 37. Challenge 10 — Verifying the Packet Modification

### Problem

It is not enough to compile the program; the actual packet on the wire needs to be inspected.

### Solution

Use:

```bash
sudo tcpdump -i ens7f1 -nn -v tcp
```

**Explanation:** Captures TCP packets and displays verbose packet-header information including TTL.

Wireshark can then be used for detailed verification.

Check:

```text
TTL = 10
```

and verify:

```text
IPv4 checksum = valid/new value
TCP checksum  = unchanged
```

---

# 38. Challenge 11 — Changing the TTL

The current TTL is:

```c
#define CUSTOM_TTL 10
```

To use another value:

```c
#define CUSTOM_TTL 20
```

Then compile again:

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

**Explanation:** Recompiles the XDP program with the new TTL value.

Remove the old program:

```bash
sudo ip link set dev ens7f0 xdp off
sudo ip link set dev ens7f1 xdp off
```

**Explanation:** Removes the previous XDP program from both interfaces.

Attach the new program:

```bash
sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp
sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Attaches the newly compiled XDP program to both interfaces.

---

# 39. Complete Testing Procedure

## Step 1 — Check Interfaces

```bash
ip link
```

**Explanation:** Confirms that `ens7f0` and `ens7f1` exist and shows their interface indexes.

---

## Step 2 — Check MAC Addresses

```bash
ip link show ens7f0
ip link show ens7f1
```

**Explanation:** Displays the MAC addresses that should be used in the Ethernet rewrite logic.

---

## Step 3 — Check IP Addresses

```bash
ip addr
```

**Explanation:** Displays all IP addresses configured on the system.

---

## Step 4 — Bring Interfaces Up

```bash
sudo ip link set ens7f0 up
sudo ip link set ens7f1 up
```

**Explanation:** Enables both Ethernet interfaces.

---

## Step 5 — Compile

```bash
clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o
```

**Explanation:** Compiles the XDP C program into an eBPF object file.

---

## Step 6 — Attach XDP

```bash
sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp
sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp
```

**Explanation:** Attaches the XDP program to both interfaces.

---

## Step 7 — Verify

```bash
ip -details link show ens7f0
ip -details link show ens7f1
```

**Explanation:** Confirms that XDP is attached.

---

## Step 8 — Start Packet Capture

On one terminal:

```bash
sudo tcpdump -i ens7f0 -nn -v tcp
```

**Explanation:** Monitors TCP packets entering/leaving the first interface.

On another terminal:

```bash
sudo tcpdump -i ens7f1 -nn -v tcp
```

**Explanation:** Monitors TCP packets entering/leaving the second interface.

---

## Step 9 — Generate TCP Traffic

From Laptop A:

```bash
curl http://<destination-ip>
```

**Explanation:** Generates TCP traffic through the test topology.

---

## Step 10 — Verify TTL

Look for:

```text
ttl 10
```

in the packet capture.

---

## Step 11 — Verify MAC Rewriting

Check the Ethernet header in tcpdump or Wireshark.

Expected:

```text
Source MAC      → XDP server interface MAC
Destination MAC → Destination laptop MAC
```

---

# 40. Changing the MAC Addresses

The MAC addresses are hard-coded in the program.

For example:

```c
__builtin_memcpy(
    eth->h_dest,
    (unsigned char[]){
        0x50, 0xa1, 0x32,
        0x76, 0xe9, 0xf9
    },
    ETH_ALEN
);
```

This rewrites the destination MAC.

The source MAC is rewritten using:

```c
__builtin_memcpy(
    eth->h_source,
    (unsigned char[]){
        0xb4, 0x96, 0x91,
        0x12, 0x9d, 0x46
    },
    ETH_ALEN
);
```

The MAC addresses must correspond to the actual topology.

---

# 41. Important Limitations

## IPv4 Only

The current implementation handles IPv4.

IPv6 uses:

```text
Hop Limit
```

instead of:

```text
TTL
```

The current program does not modify IPv6 Hop Limit.

---

## TCP Only

The TTL rewrite occurs only for:

```c
iph->protocol == IPPROTO_TCP
```

Therefore:

```text
TCP  → TTL modified
UDP  → TTL unchanged
ICMP → TTL unchanged
```

---

## ARP

ARP is explicitly passed:

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

This allows address resolution to continue working.

---

# 42. Quick Command Reference

| Command                                                                            | Purpose                                 |
| ---------------------------------------------------------------------------------- | --------------------------------------- |
| `ip link`                                                                          | Display interfaces and indexes          |
| `ip addr`                                                                          | Display IP addresses                    |
| `ip route`                                                                         | Display routing table                   |
| `ip neigh`                                                                         | Display ARP/neighbor entries            |
| `sudo ip link set ens7f0 up`                                                       | Enable interface                        |
| `sudo apt update`                                                                  | Update package information              |
| `sudo apt install clang llvm libbpf-dev linux-headers-$(uname -r) bpftool tcpdump` | Install development and debugging tools |
| `clang -O2 -g -target bpf -c xdp_redirect.c -o xdp_redirect.o`                     | Compile XDP program                     |
| `llvm-objdump -h xdp_redirect.o`                                                   | Inspect eBPF object sections            |
| `llvm-objdump -S xdp_redirect.o`                                                   | Disassemble eBPF program                |
| `sudo ip link set dev ens7f0 xdp obj xdp_redirect.o sec xdp`                       | Attach XDP to ens7f0                    |
| `sudo ip link set dev ens7f1 xdp obj xdp_redirect.o sec xdp`                       | Attach XDP to ens7f1                    |
| `ip -details link show ens7f0`                                                     | Check XDP attachment                    |
| `sudo bpftool prog show`                                                           | List loaded BPF programs                |
| `sudo bpftool map show`                                                            | List BPF maps                           |
| `sudo ip link set dev ens7f0 xdp off`                                              | Remove XDP                              |
| `sudo tcpdump -i ens7f0 -nn -e`                                                    | Capture Ethernet packets                |
| `sudo tcpdump -i ens7f1 -nn -e`                                                    | Capture Ethernet packets                |
| `sudo tcpdump -i ens7f0 -nn -v tcp`                                                | Capture verbose TCP traffic             |
| `sudo tcpdump -i ens7f1 -nn -v tcp`                                                | Capture verbose TCP traffic             |
| `ping <IP>`                                                                        | Test basic connectivity using ICMP      |
| `curl http://<IP>`                                                                 | Generate TCP traffic                    |

---

# 43. Project Workflow

```text
             ┌─────────────────────┐
             │    Laptop A         │
             └──────────┬──────────┘
                        │
                        ▼
                  ┌───────────┐
                  │  ens7f0   │
                  └─────┬─────┘
                        │
                        ▼
              ┌───────────────────┐
              │    XDP Program    │
              │                   │
              │ Ethernet check    │
              │ IPv4 check       │
              │ TCP check        │
              │                   │
              │ TTL X → 10       │
              │ IPv4 checksum    │
              │ MAC rewriting    │
              │                   │
              │ Redirect         │
              └─────────┬─────────┘
                        │
                        ▼
                  ┌───────────┐
                  │  ens7f1   │
                  └─────┬─────┘
                        │
                        ▼
             ┌─────────────────────┐
             │    Laptop B         │
             └─────────────────────┘
```

The reverse direction follows the same process.

---

# 44. Final Result

The final system performs packet manipulation directly at the XDP layer.

For a TCP/IPv4 packet:

```text
Incoming Packet
      │
      ▼
Original TTL = X
      │
      ▼
XDP
      │
      ├── TTL → 10
      │
      ├── IPv4 checksum → updated
      │
      ├── TCP checksum → unchanged
      │
      ├── Source MAC → rewritten
      │
      └── Destination MAC → rewritten
      │
      ▼
Redirect to opposite interface
      │
      ▼
Destination Laptop
```

The core transformation is:

```text
                BEFORE
                  │
                  ▼
          TTL = Original
          IP Checksum = Old
          TCP Checksum = C
                  │
                  │
                XDP
                  │
                  ▼
          TTL = 10
          IP Checksum = New
          TCP Checksum = C
                  │
                  ▼
               FORWARD
```

---

# 45. Conclusion

This project demonstrates how XDP can be used to perform packet processing before packets enter the traditional Linux networking stack.

The implementation combines:

* Layer-2 Ethernet manipulation
* Layer-3 IPv4 packet processing
* Layer-4 TCP identification
* TTL rewriting
* Incremental Internet checksum updating
* XDP packet redirection

The most important technical concept demonstrated is that **changing an IPv4 header field requires the IPv4 checksum to remain consistent with the modified header**.

Instead of recalculating the entire checksum, the program performs an **incremental checksum update**, making the modification efficient and suitable for high-speed packet processing.

The resulting packet has:

```text
IPv4 TTL        → Custom value (10)
IPv4 checksum   → Correctly updated
TCP checksum    → Preserved
Ethernet MACs   → Rewritten
Packet path     → Redirected through XDP
```

