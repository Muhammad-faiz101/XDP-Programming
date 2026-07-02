# XDP, Generic XDP, and `struct sk_buff` — Learning Notes

## 1. `struct sk_buff` (SKB)

`struct sk_buff` is the Linux kernel's primary packet descriptor. It **does not contain the packet bytes**. Instead, it stores metadata about the packet and pointers to where the packet data lives in memory.

Typical fields include:

* `skb->len` — Total packet length.
* `skb->protocol` — Layer-3 protocol (e.g., IPv4, IPv6).
* `skb->dev` — Network device that received or will transmit the packet.
* `skb->head` — Beginning of the allocated packet buffer.
* `skb->data` — Current start of the packet.
* `skb->tail` — Offset to the end of valid packet data.
* `skb->end` — Offset to the end of the allocated buffer.

---

## 2. `skb` vs Packet Buffer

These are two different memory objects.

```
struct sk_buff (metadata)

+------------------------------+
| len                          |
| protocol                     |
| dev                          |
| head ---------------------+
| data ------------------+  |
+-------------------------|-+
                          |
                          v

Packet Buffer

+------------------------------------------------+
| Headroom | Ethernet | IP | TCP | Payload |
+------------------------------------------------+
```

The SKB contains pointers to the packet buffer.

---

## 3. Meaning of Important Pointers

### `skb`

Address of the metadata object.

Example:

```
skb = 0xffff888012340000
```

This is where the `struct sk_buff` itself lives.

---

### `skb->head`

Pointer to the beginning of the allocated packet buffer.

```
head
 |
 v
+--------------------------------------+
| Headroom | Ethernet | IP | Payload |
+--------------------------------------+
```

---

### `skb->data`

Pointer to the current beginning of the packet.

Normally it points to the Ethernet header.

```
head
 |
 v
+-----------+--------------------------+
| Headroom  | Ethernet | IP | Payload |
+-----------+--------------------------+
            ^
            |
           data
```

---

### `skb->tail`

Marks the end of valid packet data.

---

### `skb->end`

Marks the end of the allocated packet buffer.

---

## 4. Why Headroom Exists

Headroom allows the kernel to prepend headers without allocating a new buffer.

Examples:

* VLAN
* MPLS
* GRE
* VXLAN
* Additional Ethernet headers

Instead of copying the packet, the kernel can move `data` backward into the reserved headroom.

---

# XDP

## 5. `struct xdp_md`

An XDP program receives:

```c
SEC("xdp")
int prog(struct xdp_md *ctx)
```

Unlike traditional networking code, it does **not** receive a `struct sk_buff`.

Typical fields are:

* `data`
* `data_end`
* `data_meta`
* `ingress_ifindex`
* `rx_queue_index`

---

## 6. What `xdp_md` Tells You

### `ctx->data`

Start of the packet.

### `ctx->data_end`

End of the packet.

Packet length can be computed as:

```c
len = data_end - data;
```

### `ctx->ingress_ifindex`

Interface that received the packet.

### `ctx->rx_queue_index`

RX queue on which the packet arrived.

### `ctx->data_meta`

Optional metadata placed before the packet.

---

## 7. What `xdp_md` Does NOT Contain

It does not expose:

* `skb->protocol`
* `skb->dev`
* `skb->len`
* `skb->sk`
* Routing information
* Checksum state
* Netfilter metadata
* Traffic-control metadata

Those belong to `struct sk_buff`.

---

# Generic XDP

## 8. Packet Flow

```
Packet

↓

Driver

↓

struct sk_buff already exists

↓

Kernel creates xdp_buff / xdp_md

↓

Runs XDP program
```

In Generic XDP, the SKB already exists before the XDP program runs.

However, the XDP program still receives only `struct xdp_md`.

---

## 9. Native XDP

Packet arrives.

```
NIC

↓

Driver

↓

XDP

↓

Only later (if needed)

↓

struct sk_buff
```

No SKB exists when the XDP program executes.

---

# Relationship Between SKB and XDP

Generic XDP converts information from the existing SKB into an XDP context.

Conceptually:

```
struct sk_buff

    data
      |
      +----------------------+

                             |

                             v

ctx->data
```

The packet bytes are typically the same; only the interface presented to the XDP program changes.

---

# Why XDP Cannot Access SKB

The XDP function prototype is:

```c
int prog(struct xdp_md *ctx)
```

There is no `struct sk_buff *` argument.

The kernel intentionally hides SKB details so that the same XDP program can work in both Native XDP and Generic XDP.

---

# Why bpftrace CAN Access SKB

A tracing program attaches to a kernel function.

Example:

```text
netif_receive_generic_xdp(struct sk_buff **pskb,
                          struct xdp_buff *xdp,
                          ...)
```

When `bpftrace` attaches to this function:

```
kprobe:netif_receive_generic_xdp
```

it can inspect the function arguments:

```
arg0 -> struct sk_buff **
arg1 -> struct xdp_buff *
```

After dereferencing `arg0`, it obtains the real `struct sk_buff *` and can inspect fields such as:

* `skb->len`
* `skb->protocol`
* `skb->head`
* `skb->data`

This is possible because the traced kernel function already has access to the SKB.

---

# BTF (BPF Type Format)

BTF describes kernel types and structure layouts.

Because BTF is available, tools like `bpftrace` know:

* where `len` is located,
* where `protocol` is located,
* how `struct sk_buff` is laid out,

without hard-coded offsets.

---

# Key Takeaways

* `struct sk_buff` is metadata, not the packet itself.
* Packet bytes live in a separate buffer.
* `skb->head` and `skb->data` point into that buffer.
* `xdp_md` is a lightweight context, not a replacement for `sk_buff`.
* Generic XDP creates an XDP view from an existing SKB.
* Native XDP runs before any SKB exists.
* XDP programs cannot obtain `struct sk_buff`.
* Tracing programs can inspect SKBs because they execute in the context of kernel functions that already receive them.
* BTF enables tools like `bpftrace` to understand kernel structures without manually calculating field offsets.
