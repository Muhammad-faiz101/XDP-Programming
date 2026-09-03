# XDP HTTP Packet Interception — Main Concepts

## 1. Network Topology

The system has three main roles:

```text
                         Internet
                            ^
                            |
                         eno1
                            |
                    +----------------+
                    |    XDP server  |
                    |                |
                    | Linux routing  |
                    | NAT/MASQUERADE |
                    +----------------+
                            |
                         ens7f0
                            |
                          Laptop
```

- **XDP server**: the Linux machine running the XDP program and acting as the laptop's Internet access point/router.
- **ens7f0**: interface where the laptop is connected.
- **eno1**: Internet-facing interface.
- Normal packets that are not intercepted by XDP are allowed to continue up the Linux networking stack.
- Linux forwarding/routing can then forward Internet-bound traffic from `ens7f0` to `eno1`.
- NAT/MASQUERADE is used on the Internet-facing path so the laptop can access the Internet through the server.

---

## 2. Overall Packet Flow

The intended behavior is:

```text
Laptop
  |
  | packet arrives on ens7f0
  v
XDP program
  |
  +-- Does packet satisfy HTTP interception conditions?
  |          |
  |          +-- NO --> XDP_PASS
  |          |             |
  |          |             v
  |          |       Linux networking stack
  |          |             |
  |          |             v
  |          |       forwarding/routing
  |          |             |
  |          |             v
  |          |           eno1
  |          |             |
  |          |             v
  |          |          Internet
  |          |
  |          +-- YES --> modify packet
  |                         |
  |                         v
  |                    XDP_TX
  |                         |
  |                         v
  |                       Laptop
  |
  +-- TCP handshake is allowed through
      because SYN/SYN-ACK/connection-control packets
      are not intercepted by this program.
```

The important distinction is:

- `XDP_PASS` means **let the packet continue into the normal Linux networking stack**.
- `XDP_TX` means **transmit the modified packet back out through the interface on which it arrived**.

---




Conceptually:

```text
data
 |
 v
+-----------------------------------------------+
| Ethernet | IPv4 | TCP | TCP payload          |
+-----------------------------------------------+
                                             ^
                                             |
                                          data_end
```

`data` points to the beginning of the packet and `data_end` marks the end of valid packet data.

---

# 4. Packet Headers and Payload

The program expects this structure:

```text
Ethernet header
       |
       v
IPv4 header
       |
       v
TCP header
       |
       v
TCP payload
```

The code creates pointers:

```c
struct ethhdr *eth;
struct iphdr *iph;
struct tcphdr *tcph;
```

Then it locates them:

```c
eth = data;

iph = (void *)(eth + 1);

tcph = (void *)((__u8 *)iph + IP_HDR_LEN);
```

For the packets accepted by this program:

```text
Ethernet = 14 bytes
IPv4     = 20 bytes
TCP      = 20 bytes minimum
```

---

# 5. Why the `data_end` Checks Matter

Before accessing packet memory, the XDP/eBPF verifier needs proof that the requested bytes are actually inside the packet.

For example:

```c
if ((void *)(eth + 1) > data_end)
    return XDP_PASS;
```

This means:

> There must be enough packet data for the Ethernet header.

Similar checks are made for the IPv4 and TCP headers.

This protects against invalid memory access and satisfies eBPF verifier requirements.

A useful mental model is:

```text
Can I safely read this header?
        |
     +--+--+
     |     |
    YES    NO
     |     |
     v     v
 continue XDP_PASS
```

---

# 6. Packet Filtering

The program progressively narrows the packets it wants to modify.

## 6.1 IPv4 only

```c
if (eth->h_proto != bpf_htons(ETH_P_IP))
    return XDP_PASS;
```

Non-IPv4 packets are passed normally.

## 6.2 No IPv4 options

```c
if (iph->ihl != 5)
    return XDP_PASS;
```

An IPv4 IHL of `5` means:

```text
5 × 4 = 20 bytes
```

So this program only handles a fixed 20-byte IPv4 header.

## 6.3 TCP only

```c
if (iph->protocol != IPPROTO_TCP)
    return XDP_PASS;
```

Non-TCP traffic is passed normally.

## 6.4 Destination port 80

```c
if (tcph->dest != bpf_htons(80))
    return XDP_PASS;
```

The program targets TCP traffic destined for port 80.

---

# 7. TCP Handshake vs TCP Data

A TCP connection starts with the three-way handshake:

```text
Laptop                         Server
  |                              |
  | -------- SYN --------------> |
  | <------- SYN-ACK ------------ |
  | -------- ACK --------------> |
  |                              |
  |       connection established |
```

The XDP program does not want to replace these packets.

It therefore requires an ACK and rejects connection-control packets:

```c
if (!tcph->ack)
    return XDP_PASS;

if (tcph->syn)
    return XDP_PASS;

if (tcph->rst)
    return XDP_PASS;

if (tcph->fin)
    return XDP_PASS;
```

The purpose is to avoid interfering with the TCP handshake and connection termination.

---

# 8. What Is TCP Payload?

TCP has:

```text
TCP header
+
TCP payload
```

The payload is the application data carried by TCP.

For an HTTP request, the payload could contain:

```http
GET / HTTP/1.1
Host: example.com
Connection: close
```

At the TCP layer, this is simply bytes in the TCP payload.

A TCP packet can also have **zero payload**.

For example, a pure ACK can be:

```text
TCP header
NO payload
```

Therefore:

```text
payload_len == 0
```

means there is no application data in that TCP segment.

The program uses:

```c
if (payload_len == 0)
    return XDP_PASS;
```

So it only modifies packets that actually contain TCP payload.

---

# 9. Calculating TCP Header Length

TCP headers can contain options.

The minimum TCP header is:

```text
20 bytes
```

The maximum is:

```text
60 bytes
```

The TCP `doff` field gives the header length in units of 4 bytes:

```c
incoming_tcp_hdr_len = tcph->doff * 4;
```

Examples:

```text
doff = 5
5 × 4 = 20 bytes

doff = 8
8 × 4 = 32 bytes
```

The program checks:

```c
if (incoming_tcp_hdr_len < TCP_HDR_LEN)
    return XDP_PASS;

if (incoming_tcp_hdr_len > 60)
    return XDP_PASS;
```

---

# 10. Calculating the Original TCP Payload Length

IPv4's `tot_len` field contains the complete IPv4 packet length:

```text
IPv4 total length
=
IPv4 header
+
TCP header
+
TCP payload
```

Therefore:

```text
TCP payload
=
IPv4 total length
-
IPv4 header length
-
TCP header length
```

The code implements:

```c
payload_len =
    ip_total_len -
    IP_HDR_LEN -
    incoming_tcp_hdr_len;
```

Example:

```text
IPv4 total length = 140 bytes
IPv4 header       = 20 bytes
TCP header        = 20 bytes

TCP payload
= 140 - 20 - 20
= 100 bytes
```

This is how the program knows how much application data the incoming TCP segment carries.

---

# 11. Resizing the Packet with `bpf_xdp_adjust_tail()`

The program wants to replace:

```text
original TCP header + original payload
```

with:

```text
new 20-byte TCP header + HTTP response
```

It calculates:

```c
delta =
    (int)(TCP_HDR_LEN + RESP_LEN) -
    (int)(incoming_tcp_hdr_len + payload_len);
```

Conceptually:

```text
delta
=
new TCP section size
-
old TCP section size
```

where:

```text
old TCP section
= incoming TCP header + incoming payload

new TCP section
= new TCP header + new HTTP response
```

## Example: packet gets smaller

Suppose:

```text
Incoming TCP header = 20
Incoming payload    = 100

New TCP header      = 20
HTTP response       = 80
```

Then:

```text
old = 20 + 100 = 120
new = 20 + 80  = 100

delta = 100 - 120
      = -20
```

So:

```c
bpf_xdp_adjust_tail(ctx, -20);
```

means conceptually:

> Reduce the packet's tail by 20 bytes.

## Example: packet gets larger

If:

```text
old TCP + payload = 70 bytes
new TCP + response = 100 bytes
```

then:

```text
delta = 100 - 70
      = +30
```

The packet grows by 30 bytes.

---

# 12. Why Pointers Are Re-read After `adjust_tail()`

After:

```c
bpf_xdp_adjust_tail(ctx, delta)
```

the packet boundaries have changed.

Therefore the program does:

```c
data = (void *)(long)ctx->data;
data_end = (void *)(long)ctx->data_end;
```

again and reconstructs:

```c
eth = data;
iph = (void *)(eth + 1);
tcph = (void *)((__u8 *)iph + IP_HDR_LEN);
```

The principle is:

> After changing packet memory, do not keep relying on old packet pointers.

---

# 13. Turning the Incoming Packet into a Response

The original packet conceptually looks like:

```text
Laptop -> XDP server

Source MAC      = Laptop
Destination MAC = Server

Source IP       = Laptop
Destination IP  = Server

Source port     = Laptop ephemeral port
Destination port= 80

SEQ             = request sequence number
ACK             = request acknowledgement

Payload         = HTTP request
```

The desired injected response looks like:

```text
XDP server -> Laptop

Source MAC      = Server
Destination MAC = Laptop

Source IP       = Server
Destination IP  = Laptop

Source port     = 80
Destination port= Laptop ephemeral port

SEQ             = request ACK
ACK             = request SEQ + request payload length

Payload         = crafted HTTP response
```

---

# 14. Ethernet Address Swap

The code swaps:

```c
eth->h_source
eth->h_dest
```

so the packet goes back toward the laptop.

---

# 15. IPv4 Address Swap

The code swaps:

```c
iph->saddr
iph->daddr
```

This changes:

```text
Laptop -> Server
```

into:

```text
Server -> Laptop
```

---

# 16. TCP Port Swap

The code swaps:

```c
tcph->source
tcph->dest
```

For example:

```text
Original:

Laptop:50000 -> Server:80

After swap:

Server:80 -> Laptop:50000
```

---

# 17. TCP Sequence and Acknowledgement Numbers

Suppose the laptop sends:

```text
SEQ = 1000
ACK = 5000
Payload length = 300
```

The response should begin at the sequence number the laptop expects from the server:

```text
Response SEQ = 5000
```

The response acknowledges the bytes carried by the request:

```text
Response ACK
=
1000 + 300
=
1300
```

The code does:

```c
tcph->seq = old_ack;

tcph->ack_seq =
    bpf_htonl(
        bpf_ntohl(old_seq) + payload_len
    );
```

This makes the fabricated response fit the existing TCP sequence space.

---

# 18. Crafted HTTP Response

The program defines:

```c
#define HTTP_RESP \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: text/html\r\n" \
    "Content-Length: 48\r\n" \
    "Connection: close\r\n" \
    "\r\n" \
    "<html><body>hi welcome to website </body></html>"
```

The response is copied into the TCP payload area:

```c
payload_start =
    (__u8 *)tcph + TCP_HDR_LEN;

__builtin_memcpy(
    payload_start,
    HTTP_RESP,
    RESP_LEN
);
```

So the original TCP payload is replaced by the crafted HTTP response.

---

# 19. TCP Header Is Forced to 20 Bytes

The program sets:

```c
tcph->doff = 5;
```

Because:

```text
5 × 4 = 20 bytes
```

So the fabricated TCP segment uses a 20-byte TCP header.

It also sets:

```c
tcph->ack = 1;
tcph->psh = 1;

tcph->syn = 0;
tcph->fin = 0;
tcph->rst = 0;
```

This makes it look like an established TCP data response.

---

# 20. Updating IPv4 Total Length

The new IPv4 packet contains:

```text
IPv4 header
+
TCP header
+
HTTP response
```

Therefore:

```c
iph->tot_len =
    bpf_htons(
        IP_HDR_LEN +
        TCP_HDR_LEN +
        RESP_LEN
    );
```

This tells the receiver the size of the new IPv4 packet.

---

# 21. Why Checksums Must Be Recalculated

The program has modified many fields:

```text
IPv4:
    source IP
    destination IP
    total length

TCP:
    source port
    destination port
    sequence number
    acknowledgement number
    flags
    header length

TCP payload:
    replaced with new HTTP response
```

The old checksums therefore no longer describe the new packet.

The program recalculates:

```text
IPv4 checksum
TCP checksum
```

---

# 22. Internet Checksum Basics

IPv4 and TCP use the Internet checksum.

The simplified process is:

```text
1. Divide the data into 16-bit words.
2. Add the words.
3. Fold carries back into the lower 16 bits.
4. Take the one's complement.
5. The result is the 16-bit checksum.
```

Conceptually:

```text
16-bit word
     +
16-bit word
     +
16-bit word
     +
...
     |
     v
32-bit accumulated sum
     |
     v
fold upper 16 bits into lower 16 bits
     |
     v
one's complement
     |
     v
16-bit checksum
```

---

# 23. `bpf_csum_diff()`

`bpf_csum_diff()` is the eBPF helper used by the program to accumulate checksum information.

Conceptually, think of it as:

```text
data
+
existing checksum sum
        |
        v
bpf_csum_diff()
        |
        v
updated checksum sum
```

Its general idea supports:

```text
old data
+
new data
+
seed
```

but the program also uses it simply to accumulate a checksum over new data.

For IPv4:

```c
ip_csum =
    bpf_csum_diff(
        NULL,
        0,
        (__be32 *)iph,
        IP_HDR_LEN,
        0
    );
```

The important parts are:

```text
source/old data = NULL
old length      = 0
new data        = IPv4 header
new length      = 20 bytes
seed            = 0
```

So the helper accumulates checksum information over the IPv4 header.

---

# 24. Why `iph->check = 0` Comes First

Before calculating the IPv4 checksum:

```c
iph->check = 0;
```

The checksum field itself must not contain the old checksum during the new calculation.

Conceptually:

```text
IPv4 header

+------------------------+
| other IPv4 fields      |
|                        |
| checksum = 0           | <-- calculate over this
|                        |
| source/destination IP   |
+------------------------+
```

After calculating the checksum, the program writes the new value into `iph->check`.

---

# 25. `csum_fold_u32()`

`bpf_csum_diff()` produces an accumulated value.

`csum_fold_u32()` converts that accumulated value into the final 16-bit Internet checksum:

```c
static __always_inline __u16 csum_fold_u32(__u32 sum)
{
    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);

    return (__u16)~sum;
}
```

## First operation

```c
sum & 0xffff
```

gets the lower 16 bits.

```c
sum >> 16
```

gets the upper 16 bits.

Therefore:

```c
(sum & 0xffff) + (sum >> 16)
```

means:

```text
lower 16 bits
+
upper 16 bits
```

This folds the carry back into the lower half.

It is done twice because the first addition can itself generate another carry.

## Final operation

```c
~sum
```

is the one's complement:

```text
0 -> 1
1 -> 0
```

The result is converted to `__u16`, producing the final 16-bit checksum.

---

# 26. IPv4 Checksum in This Program

The sequence is:

```text
Set IPv4 checksum to zero
        |
        v
bpf_csum_diff()
        |
        v
Accumulated IPv4-header sum
        |
        v
csum_fold_u32()
        |
        v
Final 16-bit IPv4 checksum
        |
        v
iph->check
```

The IPv4 checksum covers the IPv4 header.

It does **not** cover the TCP payload.

---

# 27. TCP Checksum

The TCP checksum is different.

It covers:

```text
Pseudo-header
+
TCP header
+
TCP payload
```

Conceptually:

```text
             TCP checksum
                   |
                   v
        +----------------------+
        | IPv4 pseudo-header   |
        +----------------------+
                   +
        +----------------------+
        | TCP header           |
        +----------------------+
                   +
        +----------------------+
        | TCP payload          |
        +----------------------+
                   |
                   v
          checksum calculation
```

---

# 28. TCP Pseudo-header

The pseudo-header is temporary data used only for the TCP checksum.

It contains:

```text
Source IP
Destination IP
Protocol
TCP length
```

The program builds:

```c
__be32 pseudo_hdr[4];

pseudo_hdr[0] = iph->saddr;
pseudo_hdr[1] = iph->daddr;

pseudo_hdr[2] =
    bpf_htonl(IPPROTO_TCP);

pseudo_hdr[3] =
    bpf_htonl(
        TCP_HDR_LEN + RESP_LEN
    );
```

The pseudo-header is **not transmitted as a separate header**.

It is only included in the checksum calculation.

---

# 29. Why TCP Uses a Pseudo-header

The pseudo-header ties the TCP segment to its IP endpoints.

Conceptually:

```text
TCP checksum depends on:

Source IP
Destination IP
TCP protocol
TCP length
TCP header
TCP payload
```

This helps detect a TCP segment being associated with the wrong IP endpoints or length.

---

# 30. TCP Checksum Calculation in This Program

First:

```c
tcph->check = 0;
```

Then the program starts with the pseudo-header:

```c
tcp_csum =
    bpf_csum_diff(
        NULL,
        0,
        pseudo_hdr,
        sizeof(pseudo_hdr),
        0
    );
```

Conceptually:

```text
seed = 0
      |
      v
pseudo-header
      |
      v
bpf_csum_diff()
      |
      v
partial TCP checksum sum
```

Then the program continues from that partial sum:

```c
tcp_csum =
    bpf_csum_diff(
        NULL,
        0,
        (__be32 *)tcph,
        TCP_HDR_LEN + RESP_LEN,
        (__u32)tcp_csum
    );
```

The important part is the final argument:

```c
tcp_csum
```

This means:

> Continue the checksum accumulation from the pseudo-header result.

The second call includes:

```text
TCP header
+
new HTTP payload
```

So the final calculation is:

```text
pseudo-header
+
TCP header
+
HTTP response
        |
        v
bpf_csum_diff()
        |
        v
accumulated sum
        |
        v
csum_fold_u32()
        |
        v
TCP checksum
```

---

# 31. TCP Payload Is Included in the TCP Checksum

This is especially important for this program.

The program replaces the original HTTP payload:

```text
OLD:
GET / HTTP/1.1
...
```

with:

```text
NEW:
HTTP/1.1 200 OK
...
<html>...
```

Because the TCP payload changed, the TCP checksum must be recalculated.

The code includes the response using:

```c
TCP_HDR_LEN + RESP_LEN
```

in the checksum calculation.

Therefore:

```text
TCP checksum
=
pseudo-header
+
new TCP header
+
new HTTP payload
```

---

# 32. Network Byte Order

The code uses functions such as:

```c
bpf_htons(...)
bpf_htonl(...)
bpf_ntohs(...)
bpf_ntohl(...)
```

These convert values between host byte order and network byte order.

Common examples:

```text
htons = host -> network, 16-bit
htonl = host -> network, 32-bit

ntohs = network -> host, 16-bit
ntohl = network -> host, 32-bit
```

Network protocols use network byte order, which is big-endian.

For example:

```c
bpf_ntohs(iph->tot_len)
```

converts the network-order IPv4 total length into a normal host-order integer so the program can perform arithmetic on it.

---

# 33. Final XDP Action

After the packet has been:

```text
filtered
resized
modified
given new addresses/ports
given new SEQ/ACK
given new HTTP payload
given new IP checksum
given new TCP checksum
```

the program returns:

```c
return XDP_TX;
```

This means:

> Transmit the modified packet back out through the same interface.

For the laptop-facing interface in this design, that means the crafted response is sent back toward the laptop through the interface on which the packet arrived.

---

# 34. XDP_PASS vs XDP_TX

These are two fundamental XDP actions used by this program.

## `XDP_PASS`

```c
return XDP_PASS;
```

Means:

```text
XDP
 |
 v
Linux networking stack
 |
 v
routing / forwarding
 |
 v
eno1
 |
 v
Internet
```

This is what happens for packets that don't satisfy the interception conditions.

## `XDP_TX`

```c
return XDP_TX;
```

Means:

```text
XDP
 |
 v
modified packet
 |
 v
transmit back through receiving interface
 |
 v
Laptop
```

It bypasses normal forwarding for that packet.

---


## Commands for makeing the server router/WAP:
```
sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -A POSTROUTING -o eno1 -s 192.168.50.0/24 -j MASQUERADE

sudo iptables -A FORWARD -i ens7f0 -o eno1 -s 192.168.50.0/24 -j ACCEPT

sudo iptables -A FORWARD -i eno1 -o ens7f0 \
    -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT



```

# 35. NAT/MASQUERADE Context

For packets that are passed up the stack and forwarded toward the Internet, the Linux server can act as a router.

The conceptual path is:

```text
Laptop
  |
  | ens7f0
  v
XDP server
  |
  | Linux routing/forwarding
  v
NAT / MASQUERADE
  |
  | eno1
  v
Internet
```

MASQUERADE changes the source address of forwarded traffic so Internet replies can return through the server.

The XDP program itself does **not** perform the NAT/MASQUERADE operation shown above. That belongs to the Linux forwarding/NAT configuration.

---

# 36. The Complete Mental Model

The entire system can be reduced to:

```text
                      LAPTOP
                         |
                       ens7f0
                         |
                         v
                 +---------------+
                 |   XDP server  |
                 +---------------+
                         |
                         v
                    XDP program
                         |
             +-----------+-----------+
             |                       |
       HTTP conditions          Not matched
             |                       |
            YES                      NO
             |                       |
             v                       v
     Modify packet              XDP_PASS
             |                       |
             v                       v
         XDP_TX              Linux network stack
             |                       |
             v                       v
          Laptop                forwarding/NAT
                                     |
                                     v
                                   eno1
                                     |
                                     v
                                  Internet
```

The key concepts to understand in order are:

1. **Ethernet → IPv4 → TCP → payload**
2. **TCP handshake vs TCP data**
3. **TCP payload length**
4. **TCP header length / `doff`**
5. **`bpf_xdp_adjust_tail()` and `delta`**
6. **Ethernet/IP/TCP address and port swapping**
7. **TCP SEQ/ACK arithmetic**
8. **IPv4 checksum**
9. **TCP checksum**
10. **TCP pseudo-header**
11. **`bpf_csum_diff()`**
12. **`csum_fold_u32()`**
13. **`XDP_PASS` vs `XDP_TX`**
14. **Linux forwarding and NAT/MASQUERADE**
15. **`ens7f0` as laptop-facing interface and `eno1` as Internet-facing interface**
