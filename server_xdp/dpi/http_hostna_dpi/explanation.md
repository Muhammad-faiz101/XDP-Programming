```markdown
# XDP HTTP DPI — Packet Parsing and Hostname Extraction

## 1. Project Goal

The goal of this project is to use an **XDP/eBPF program** as an inline Deep Packet Inspection (DPI) component.

The XDP program should:

1. Parse the Ethernet header.
2. Parse the IPv4 header.
3. Determine whether the packet uses TCP.
4. Determine whether the traffic is HTTP.
5. Locate the TCP payload.
6. Search the HTTP request for the `Host:` header.
7. Extract the hostname.
8. Print the hostname using `bpf_printk()`.
9. Continue with the existing MAC-rewriting and packet-forwarding logic.

The DPI functionality should **inspect the packet without changing the existing forwarding behavior**.

---

## 2. Network Topology

The intended topology is:

```text
                         XDP / DPI Server
                    ┌─────────────────────────┐
                    │                         │
Laptop A            │  ens7f0       ens7f1    │            Laptop B
192.168.50.2 ───────┤     XDP        XDP      ├─────── 192.168.60.2
                    │     DPI                 │
                    └─────────────────────────┘
                                                       │
                                                       ▼
                                                    Internet
```

On the XDP server:

* `ens7f0` → interface connected to Laptop A
* `ens7f1` → interface connected to Laptop B

The XDP program forwards packets using:

`bpf_redirect()`

and performs Ethernet MAC rewriting before redirecting the packet.

---

## 3. Overall Packet Processing Flow

The packet-processing flow is:

```text
Incoming Ethernet frame
          |
          v
    Ethernet header
          |
          v
        IPv4?
       /     \
     NO       YES
     |         |
   Drop        v
            TCP?
           /   \
         NO     YES
         |       |
      Forward    v
             TCP header
                 |
                 v
             TCP payload
                 |
                 v
              Port 80?
              /      \
            NO        YES
            |          |
         Forward       v
                    HTTP check
                        |
                        v
                    Find Host:
                        |
                        v
                 Extract hostname
                        |
                        v
                   bpf_printk()
                        |
                        v
               Existing forwarding
```

---

## 4. Ethernet Parsing

An XDP program receives the packet as raw packet memory.
The first header is normally the Ethernet header.

The packet can be viewed as:

```text
+-------------------+
| Ethernet header   |
+-------------------+
| IPv4 header       |
+-------------------+
| TCP header        |
+-------------------+
| TCP payload       |
+-------------------+
```

The Ethernet header tells us what protocol comes next.
For example:

`eth->h_proto`

can identify:

* `ETH_P_IP`  → IPv4
* `ETH_P_ARP` → ARP

Before accessing the Ethernet header, we must verify that it exists inside the packet:

```c
if ((void *)(eth + 1) > data_end)
    return XDP_DROP;
```

### Why is this check necessary?
XDP works directly with packet memory.
The valid memory range is:

```text
data                         data_end
 |                              |
 v                              v
 +------------------------------+
 |            packet            |
 +------------------------------+
```

The eBPF verifier must prove that every memory access stays inside this range.

---

## 5. IPv4 Parsing

After parsing Ethernet, we check whether the next protocol is IPv4:

```c
if (eth->h_proto != bpf_htons(ETH_P_IP))
    return XDP_DROP;
```

Then we locate the IPv4 header:

```c
struct iphdr *iph = (void *)eth + sizeof(struct ethhdr);
```

Before accessing it:

```c
if ((void *)(iph + 1) > data_end)
    return XDP_DROP;
```

The IPv4 header contains the protocol field:

`iph->protocol`

This tells us which transport protocol follows IPv4.
Examples:

* `IPPROTO_TCP`  → TCP
* `IPPROTO_UDP`  → UDP
* `IPPROTO_ICMP` → ICMP

For HTTP DPI, we are interested in TCP.

---

## 6. Determine Whether the Packet Uses TCP

We check:

```c
if (iph->protocol != IPPROTO_TCP)
    goto forward;
```

Conceptually:

```text
IPv4
 |
 +-- ICMP → don't perform HTTP DPI
 |
 +-- UDP  → don't perform HTTP DPI
 |
 +-- TCP  → continue parsing
```

The packet is still forwarded using the existing forwarding logic.
The DPI code should only inspect traffic that can contain HTTP.

---

## 7. IPv4 Header Length

The IPv4 header is not necessarily always 20 bytes.
The `ihl` field specifies the header length in units of 32-bit words.
Therefore:

```c
__u32 ip_header_len = iph->ihl * 4;
```

For a normal IPv4 header:

`ihl = 5`
`5 × 4 = 20 bytes`

First validate the length:

```c
if (ip_header_len < sizeof(struct iphdr))
    goto forward;
```

Then verify that the complete IPv4 header is inside the packet:

```c
if ((void *)iph + ip_header_len > data_end)
    goto forward;
```

This allows us to safely locate the TCP header.

---

## 8. Locate the TCP Header

The TCP header starts immediately after the IPv4 header:

```c
struct tcphdr *tcph = (void *)iph + ip_header_len;
```

Before reading it:

```c
if ((void *)(tcph + 1) > data_end)
    goto forward;
```

The packet now looks like:

```text
+-------------------+
| Ethernet          |
+-------------------+
| IPv4              |
+-------------------+
| TCP               | ← tcph
+-------------------+
| TCP payload       |
+-------------------+
```

---

## 9. TCP Header Length

TCP headers can contain optional fields, so they are not always exactly 20 bytes.
The TCP `doff` field specifies the TCP header length in 32-bit words.

```c
__u32 tcp_header_len = tcph->doff * 4;
```

For a normal TCP header:

`doff = 5`
`5 × 4 = 20 bytes`

Validate the header length:

```c
if (tcp_header_len < sizeof(struct tcphdr))
    goto forward;
```

Then verify that the complete TCP header exists:

```c
if ((void *)tcph + tcp_header_len > data_end)
    goto forward;
```

---

## 10. Locate the TCP Payload

The TCP payload starts immediately after the TCP header.

```c
unsigned char *payload = (void *)tcph + tcp_header_len;
```

The structure is now:

```text
+-------------------+
| Ethernet          |
+-------------------+
| IPv4              |
+-------------------+
| TCP header        |
+-------------------+
| TCP payload       | ← payload
+-------------------+
```

Before accessing the payload:

```c
if ((void *)payload >= data_end)
    goto forward;
```

This is important because some TCP packets contain no application data.
For example, a TCP ACK can contain only headers.

---

## 11. Identifying HTTP

For this project, we can first check the TCP destination port.
Standard HTTP uses:

* TCP port 80

Therefore:

```c
if (tcph->dest != bpf_htons(80))
    goto forward;
```

However, port 80 alone does not guarantee that the payload is HTTP.
Therefore we also inspect the beginning of the payload.

Typical HTTP requests look like:

```http
GET / HTTP/1.1
Host: example.com
```

or:

```http
POST /login HTTP/1.1
Host: example.com
```

We can recognize common HTTP methods such as:

* `GET`
* `POST`
* `HEAD`

---

## 12. Detecting GET

Example:

```c
int is_http = 0;

if (payload + 4 <= (unsigned char *)data_end &&
    payload[0] == 'G' &&
    payload[1] == 'E' &&
    payload[2] == 'T' &&
    payload[3] == ' ') {

    is_http = 1;
}
```

For:

`GET / HTTP/1.1`

the beginning of the payload is:

`G` `E` `T` `[space]`

The program checks those bytes.
The bounds check:

`payload + 4 <= data_end`

makes sure that four bytes are available before accessing them.

---

## 13. Detecting POST

For POST:

```c
if (payload + 5 <= (unsigned char *)data_end &&
    payload[0] == 'P' &&
    payload[1] == 'O' &&
    payload[2] == 'S' &&
    payload[3] == 'T' &&
    payload[4] == ' ') {

    is_http = 1;
}
```

This recognizes:

`POST / HTTP/1.1`

The program checks:

`P` `O` `S` `T` `[space]`

---

## 14. Detecting HEAD

For HEAD:

```c
if (payload + 5 <= (unsigned char *)data_end &&
    payload[0] == 'H' &&
    payload[1] == 'E' &&
    payload[2] == 'A' &&
    payload[3] == 'D' &&
    payload[4] == ' ') {

    is_http = 1;
}
```

This recognizes:

`HEAD / HTTP/1.1`

---

## 15. Why HTTPS Cannot Be Parsed Like HTTP

HTTP normally contains plaintext application data:

```text
TCP
 |
 +-- HTTP
      |
      +-- GET
      +-- Host:
      +-- User-Agent
```

HTTPS normally contains TLS:

```text
TCP
 |
 +-- TLS
      |
      +-- encrypted application data
```

The HTTP request is encrypted inside TLS.
Therefore, the XDP program cannot normally search an HTTPS payload for:

`Host:`

and obtain the HTTP hostname from the encrypted HTTP header.

For this project:

* HTTP  → HTTP headers can be inspected
* HTTPS → HTTP headers are encrypted

---

## 16. Searching for Host:

Once:

`is_http == 1`

the program searches the TCP payload for:

`Host:`

For example:

```http
GET / HTTP/1.1\r\n
Host: example.com\r\n
Connection: close\r\n
```

The search code is:

```c
int host_position = -1;

#pragma unroll
for (int i = 0; i < MAX_HTTP_SCAN - 5; i++) {

    if (payload + i + 5 > (unsigned char *)data_end)
        break;

    if (payload[i]     == 'H' &&
        payload[i + 1] == 'o' &&
        payload[i + 2] == 's' &&
        payload[i + 3] == 't' &&
        payload[i + 4] == ':') {

        host_position = i;
        break;
    }
}
```

---

## 17. Meaning of host_position

Initially:

`int host_position = -1;`

means:

`Host:` has not been found

If the program finds `Host:`:

`host_position = i;`

For example:

```text
GET / HTTP/1.1\r\nHost: example.com
                ^
                H
```

The value of `i` represents the location of the `H`.

---

## 18. Why payload + i + 5?

The program accesses five bytes:

* `payload[i]`
* `payload[i + 1]`
* `payload[i + 2]`
* `payload[i + 3]`
* `payload[i + 4]`

These five bytes represent:

`H` `o` `s` `t` `:`

Therefore we first make sure that all five bytes are inside the packet:

```c
if (payload + i + 5 > (unsigned char *)data_end)
    break;
```

This is mainly required because of the eBPF verifier.
The verifier must prove that every packet-memory access is safe.

---

## 19. Extracting the Hostname

After finding:

`Host:`

the hostname begins five bytes later:

```c
int host_start = host_position + 5;
```

For:

`Host: example.com`

the structure is:

```text
H o s t :   e x a m p l e . c o m
          ^
          hostname starts here
```

---

## 20. Skipping Spaces

HTTP normally uses:

`Host: example.com`

There is a space after the colon.
The program skips spaces:

```c
#pragma unroll
for (int i = 0; i < 8; i++) {

    if (payload + host_start + 1 > (unsigned char *)data_end)
        break;

    if (payload[host_start] == ' ')
        host_start++;
    else
        break;
}
```

This changes:

```text
Host: example.com
     ^
     space
```

into:

```text
Host: example.com
      ^
      e
```

---

## 21. Hostname Buffer

We create a buffer to store the hostname:

```c
char host[MAX_HOST_LEN + 1] = {};
```

The extra byte is for the null terminator:

`hostname + '\0'`

For example:

`example.com\0`

The `\0` tells C that the string has ended.

---

## 22. Copying the Hostname

The hostname is copied one character at a time:

```c
int host_len = 0;

#pragma unroll
for (int i = 0; i < MAX_HOST_LEN; i++) {

    if (payload + host_start + i + 1 > (unsigned char *)data_end)
        break;

    char c = payload[host_start + i];

    if (c == '\r' || c == '\n')
        break;

    host[i] = c;
    host_len++;
}
```

For:

`Host: example.com\r\n`

the program copies:

`e` `x` `a` `m` `p` `l` `e` `.` `c` `o` `m`

---

## 23. Detecting the End of the Host Header

HTTP headers normally end with CRLF:

`\r\n`

For example:

```http
Host: example.com\r\n
Connection: close\r\n
```

Therefore:

```c
if (c == '\r' || c == '\n')
    break;
```

stops the hostname extraction when the end of the header is reached.
Without this check, the program could continue copying the following headers.

---

## 24. Null Termination

After copying the hostname:

```c
host[host_len] = '\0';
```

The buffer becomes:

`e` `x` `a` `m` `p` `l` `e` `.` `c` `o` `m` `\0`

Now it is a valid C string.

---

## 25. Printing the Hostname

The hostname can be printed with:

```c
bpf_printk("HTTP Host: %s", host);
```

To observe the output:

```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

Example output:

```text
HTTP Host: example.com
```

---

## 26. DPI and Forwarding Are Separate

The DPI section should only inspect the packet.
It should not replace the existing forwarding logic.

Conceptually:

```text
                  Packet
                    |
                    v
             +-------------+
             | XDP program |
             +-------------+
                    |
             +------+------+
             |             |
             v             v
          Inspect       Forward
             |             |
             v             v
          HTTP DPI     MAC rewrite
                           |
                           v
                     bpf_redirect()
```

The two responsibilities are different:

* **DPI:** "What is inside the packet?"
* **Forwarding:** "Where should the packet go?"

---

## 27. Existing Forwarding Logic

For traffic entering from the A-facing interface:

```c
if (ctx->ingress_ifindex == ENS7F0_IFINDEX) {

    /* A -> B */

    __builtin_memcpy(
        eth->h_dest,
        B_MAC,
        ETH_ALEN
    );

    __builtin_memcpy(
        eth->h_source,
        ENS7F1_MAC,
        ETH_ALEN
    );

    return bpf_redirect(ENS7F1_IFINDEX, 0);
}
```

The reverse direction remains:

```c
if (ctx->ingress_ifindex == ENS7F1_IFINDEX) {

    /* B -> A */

    __builtin_memcpy(
        eth->h_dest,
        A_MAC,
        ETH_ALEN
    );

    __builtin_memcpy(
        eth->h_source,
        ENS7F0_MAC,
        ETH_ALEN
    );

    return bpf_redirect(ENS7F0_IFINDEX, 0);
}
```

The DPI logic should run before these forwarding blocks but should not modify them.

---

## 28. Ping Traffic

Ping normally uses ICMP:

```text
Ethernet
    |
    v
IPv4
    |
    v
ICMP
```

It does not contain an HTTP payload.
Therefore the DPI section should not try to parse it as HTTP.
The existing forwarding logic should still forward the packet.

Conceptually:

```text
Ping
  |
  v
Ethernet
  |
  v
IPv4
  |
  v
ICMP
  |
  v
Skip HTTP DPI
  |
  v
Existing forwarding
```

---

## 29. eBPF Verifier and Packet Bounds

One of the most important concepts in XDP programming is packet bounds checking.
XDP provides:

* `void *data;`
* `void *data_end;`

The valid packet memory is:

```text
data                              data_end
 |                                   |
 v                                   v
 +-----------------------------------+
 |             packet                |
 +-----------------------------------+
```

An access such as:

`payload[i]`

must be proven to be inside this range.

For example:

```c
if (payload + i + 5 > (unsigned char *)data_end)
    break;
```

allows the verifier to understand that the following accesses are safe:

* `payload[i]`
* `payload[i + 1]`
* `payload[i + 2]`
* `payload[i + 3]`
* `payload[i + 4]`

If the verifier cannot prove an access is safe, the program may fail to load with:

`BPF program load failed: -EACCES`

and a verifier message such as:

`invalid access to packet`

---

## 30. Why #pragma unroll Is Used

The HTTP search uses:

`#pragma unroll`

because eBPF programs have restrictions around loops.

For example:

```c
#pragma unroll
for (int i = 0; i < 32; i++) {
    ...
}
```

allows the compiler to expand the bounded loop into a sequence of operations.
This makes the loop easier for the eBPF verifier to analyze.
The scan limit should remain reasonably small.

---

## 31. Example HTTP Request

Suppose Laptop A sends:

```http
GET /index.html HTTP/1.1\r\n
Host: example.com\r\n
User-Agent: curl\r\n
Accept: */*\r\n
```

The XDP program sees:

```text
Ethernet
    |
    v
IPv4
    |
    v
TCP
    |
    v
TCP payload
    |
    +-- GET /index.html HTTP/1.1
    |
    +-- Host: example.com
    |
    +-- User-Agent: curl
    |
    +-- Accept: */*
```

The DPI process becomes:

```text
Ethernet
    |
    v
IPv4
    |
    v
TCP
    |
    v
Destination port 80
    |
    v
Detect GET
    |
    v
Search for Host:
    |
    v
Find Host:
    |
    v
Extract example.com
    |
    v
bpf_printk()
    |
    v
Existing MAC rewriting
    |
    v
bpf_redirect()
```

The trace output is:

```text
HTTP Host: example.com
```

---

## 32. Complete Conceptual Flow

The complete project can be summarized as:

```text
                    Incoming packet
                           |
                           v
                    Ethernet header
                           |
                           v
                         IPv4
                           |
                           v
                          TCP?
                           |
                    +------+------+
                    |             |
                   NO            YES
                    |             |
                    v             v
                Forward       TCP header
                                  |
                                  v
                             TCP payload
                                  |
                                  v
                              Port 80?
                                  |
                                  v
                            HTTP method?
                                  |
                                  v
                              Find Host:
                                  |
                                  v
                          Extract hostname
                                  |
                                  v
                         bpf_printk()
                                  |
                                  v
                         Existing forwarding
                                  |
                                  v
                           MAC rewriting
                                  |
                                  v
                          bpf_redirect()
```

---

## 33. Main Concepts Learned

### Layered Packet Parsing
The packet is parsed layer by layer:

```text
Ethernet → IPv4 → TCP → HTTP
```

Each layer provides information needed to locate the next layer.

### Protocol Identification
IPv4 tells us which transport protocol is being used:

* TCP
* UDP
* ICMP

TCP ports then provide information about the likely application protocol.

### Application-Layer DPI
After reaching the TCP payload, the program can inspect application-layer data.
For plaintext HTTP:

```http
GET / HTTP/1.1
Host: example.com
```

the XDP program can search for application-layer fields such as:

`Host:`

### Packet Bounds Checking
Every packet-memory access must be proven safe.
The eBPF verifier enforces this.
This is why the program repeatedly checks:

`data_end`

before accessing packet data.

### HTTP vs HTTPS
Plain HTTP:

```text
TCP → HTTP plaintext → Host: example.com
```

HTTPS:

```text
TCP → TLS → Encrypted HTTP
```

Therefore the HTTP headers cannot normally be directly parsed from HTTPS traffic.

### DPI vs Forwarding
* **DPI determines:** "What is this packet carrying?"
* **Forwarding determines:** "Where should this packet go?"

The XDP program combines both:

```text
              XDP
               |
       +-------+-------+
       |               |
       v               v
     DPI           Forwarding
       |               |
       v               v
 Host extraction   MAC rewriting
                       |
                       v
                  bpf_redirect()
```

The key design principle is:

> Inspect the packet, extract the information required for DPI, and then continue with the existing forwarding path without altering the forwarding behavior.
```