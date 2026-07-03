# XDP Packet Parser Notes

## Objective

Build an XDP program that intercepts packets at the earliest point in the Linux networking stack and parses:

- Ethernet Header
- IPv4 Header
- TCP Header
- TCP Payload (first 16 bytes)

The program only inspects packets and returns `XDP_PASS`, allowing them to continue through the kernel.

---

# Packet Flow

```
Network Card (NIC)
        │
        ▼
+---------------------+
|     XDP Program     |
+---------------------+
        │
        ▼
Ethernet Header
        │
        ▼
IPv4 Header
        │
        ▼
TCP Header
        │
        ▼
TCP Payload
        │
        ▼
XDP_PASS
        │
        ▼
Linux Networking Stack
```

Unlike normal kernel networking, XDP works directly on raw packet bytes.

---

# xdp_md

The XDP program receives only one argument.

```c
int xdp_parser(struct xdp_md *ctx)
```

Important fields:

```c
ctx->data
```

Pointer to first byte of the packet.

```c
ctx->data_end
```

Pointer to one byte past the end of the packet.

Packet length:

```c
packet_length = data_end - data;
```

---

# Why data_end exists

Packets arriving at the NIC can have different sizes.

```
+--------------------------------+
| Ethernet | IP | TCP | Payload  |
+--------------------------------+
^                                ^
data                         data_end
```

The kernel never trusts packet length.

Every read must first verify that the bytes exist.

---

# Packet Boundary Checks

Before reading any structure:

```c
if ((void *)(eth + 1) > data_end)
    return XDP_PASS;
```

Meaning:

```
Do I have a complete Ethernet header?
```

Similarly:

```c
if ((void *)(ip + 1) > data_end)
    return XDP_PASS;
```

```
Do I have a complete IP header?
```

And

```c
if ((void *)(tcp + 1) > data_end)
    return XDP_PASS;
```

```
Do I have a complete TCP header?
```

---

# Ethernet Header

```
+-----------------------+
| Destination MAC (6B)  |
+-----------------------+
| Source MAC (6B)       |
+-----------------------+
| EtherType (2B)        |
+-----------------------+
```

Access:

```c
struct ethhdr *eth = data;
```

Fields:

```c
eth->h_dest
eth->h_source
eth->h_proto
```

EtherType identifies the next protocol.

Examples:

```
0x0800  IPv4
0x86DD  IPv6
0x0806  ARP
```

Convert to host order:

```c
__builtin_bswap16(eth->h_proto)
```

---

# IPv4 Header

Located immediately after Ethernet.

```c
struct iphdr *ip = (void *)(eth + 1);
```

Important fields:

```c
ip->version
ip->ihl
ip->tot_len
ip->ttl
ip->protocol
ip->saddr
ip->daddr
```

Header size:

```c
ip->ihl * 4
```

Usually:

```
20 bytes
```

Can be larger if IP options exist.

---

# IP Protocol Numbers

```
1  ICMP
6  TCP
17 UDP
```

Used to decide what comes next.

---

# Source and Destination IP

```c
__u8 *src = (__u8 *)&ip->saddr;
```

Print as

```
192.168.1.10
```

instead of

```
0xc0a8010a
```

---

# TCP Header

Located after the IP header.

```c
struct tcphdr *tcp =
    (void *)ip + ip->ihl * 4;
```

Important fields:

```c
tcp->source
tcp->dest
tcp->seq
tcp->ack_seq
tcp->doff
```

Convert network byte order:

```c
__builtin_bswap16()

__builtin_bswap32()
```

---

# TCP Header Size

```
tcp->doff * 4
```

Usually

```
20 bytes
```

May be larger because of TCP options.

---

# Payload

Payload begins immediately after TCP header.

```
payload =
    (void *)tcp + tcp->doff * 4;
```

Payload length:

```c
payload_len =
    data_end - payload;
```

---

# Reading Payload

Before reading

```c
payload[15]
```

Always verify

```c
if (payload + 16 > data_end)
    return XDP_PASS;
```

Otherwise the verifier rejects the program.

---

# Why the Verifier Rejected the Program

Incorrect:

```c
if (payload > data_end)
```

This only proves

```
payload[0]
```

exists.

It does **not** prove

```
payload[15]
```

exists.

Correct:

```c
if (payload + 16 > data_end)
```

Now the verifier knows

```
payload[0]
...
payload[15]
```

are safe.

---

# Printable ASCII

Not every payload byte is printable.

Convert using:

```c
(byte >= 32 && byte <= 126)
```

Otherwise print

```
.
```

Example:

```
GET / HT
```

instead of

```
47 45 54 ...
```

---

# Network Byte Order

Packets use

```
Big Endian
```

Most CPUs use

```
Little Endian
```

Convert using:

```c
__builtin_bswap16()

__builtin_bswap32()
```

Without conversion:

```
0x5000
```

With conversion:

```
80
```

---

# Pointer Arithmetic

Ethernet

```c
struct ethhdr *eth = data;
```

↓

IP

```c
struct iphdr *ip =
    (void *)(eth + 1);
```

↓

TCP

```c
struct tcphdr *tcp =
    (void *)ip + ip->ihl * 4;
```

↓

Payload

```c
unsigned char *payload =
    (unsigned char *)tcp + tcp->doff * 4;
```

---

# Complete Parsing Flow

```
ctx->data
      │
      ▼
Ethernet Header
      │
      ▼
Check EtherType

      │
      ▼
IPv4 Header
      │
      ▼
Check Protocol

      │
      ▼
TCP Header
      │
      ▼
Payload
```

---

# Important XDP Return Codes

```c
XDP_PASS
```

Continue through Linux networking stack.

```c
XDP_DROP
```

Drop packet immediately.

```c
XDP_TX
```

Transmit packet back out the same interface.

```c
XDP_REDIRECT
```

Redirect to another interface or CPU.

```c
XDP_ABORTED
```

Indicates an unexpected error.

---

# Useful Commands

## Compile

```bash
clang -O2 -g -target bpf \
-I/usr/include/x86_64-linux-gnu \
-c xdp_md_analysis.c \
-o xdp_md_analysis.o
```

---

## Attach XDP Program

Generic mode:

```bash
sudo ip link set dev wlp1s0 xdpgeneric obj xdp_md_analysis.o sec xdp
```

Native mode (if supported):

```bash
sudo ip link set dev wlp1s0 xdp obj xdp_md_analysis.o sec xdp
```

---

## Verify XDP Attached

```bash
ip -details link show wlp1s0
```

Look for:

```
prog/xdp id ...
```

or

```
xdpgeneric
```

---

## Remove XDP Program

```bash
sudo ip link set dev wlp1s0 xdpgeneric off
```

or

```bash
sudo ip link set dev wlp1s0 xdp off
```

---

## View Trace Output

```bash
sudo cat /sys/kernel/tracing/trace_pipe
```

This streams live output from:

```c
bpf_printk(...)
```

---

## List XDP Programs

```bash
sudo bpftool prog show
```

---

## Show Network Interfaces

```bash
ip link show
```

---

## Show Interface Details

```bash
ip -details link show
```

---

## Capture Traffic

```bash
sudo tcpdump -i wlp1s0
```

---

## Ping

```bash
ping <IP>
```

Generates ICMP packets.

---

## Netcat Listener

```bash
nc -l 8080
```

---

## Netcat Client

```bash
echo "Hello XDP" | nc <IP> 8080
```

Generates TCP packets with payload.

---

# Key Takeaways

- XDP works on **raw packet bytes**, not `sk_buff`.
- `ctx->data` points to the start of the packet.
- `ctx->data_end` marks the packet boundary.
- Every packet access **must** be preceded by a bounds check.
- The eBPF verifier rejects any memory access it cannot prove is safe.
- Use pointer arithmetic to move through protocol headers:
  - Ethernet → IPv4 → TCP → Payload.
- Convert multi-byte fields from network byte order before printing.
- `bpf_printk()` is useful for debugging but is slow and should not be used in production.
- The parsing pattern (check bounds → cast pointer → read fields) is the standard approach for writing safe XDP programs
