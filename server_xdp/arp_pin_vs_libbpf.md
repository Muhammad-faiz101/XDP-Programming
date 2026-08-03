# XDP Router Notes: Program Loading, Pinning, Maps, and Alternatives

## Overview

The XDP router consists of:

- An eBPF XDP program (`xdp_router.o`)
- Two BPF maps:
  - `tx_port` (DEVMAP)
  - `mac_map` (ARRAY)
- A userspace loader (`bpftool` or libbpf application)

The userspace program is responsible for:

1. Loading the eBPF object.
2. Creating the maps.
3. Configuring the maps.
4. Attaching the program to network interfaces.

The XDP program itself only forwards packets based on information stored inside the maps.

---

# Overall Packet Flow

```
Incoming Packet
        │
        ▼
Network Interface
        │
        ▼
XDP Program
        │
        ├── ARP → XDP_PASS
        │
        ├── IPv4
        │      │
        │      ▼
        │ Lookup Maps
        │      │
        │ Rewrite MACs
        │      │
        │ Redirect
        │
        └── Others → DROP
```

---

# ARP Handling

The program intentionally ignores ARP.

```c
if (eth->h_proto == bpf_htons(ETH_P_ARP))
    return XDP_PASS;
```

Reason:

ARP must reach the Linux networking stack.

Linux automatically:

- receives ARP request
- checks local IP addresses
- generates ARP reply
- updates ARP cache

If XDP redirected or dropped ARP:

- Linux would never see it
- no ARP replies
- no MAC resolution
- IPv4 communication would fail

---

# Program Compilation

```
xdp_router.c
      │
clang
      ▼
xdp_router.o
```

The object file contains:

- XDP program
- map definitions
- license
- metadata

Nothing is loaded into the kernel yet.

---

# Loading the Program

```
bpftool prog load
```

Internally:

```
bpftool
     │
     ▼
bpf() syscall
     │
     ▼
Kernel
```

Kernel performs:

- parses ELF
- creates maps
- creates program
- verifies safety
- loads objects into kernel memory

---

# Objects Created

```
Kernel Memory

Program

tx_port map

mac_map
```

These are kernel objects.

They are **not files**.

---

# The Verifier

Before loading:

Kernel checks:

- memory safety
- packet bounds
- invalid pointers
- illegal helper usage
- loops
- stack usage

Only verified programs are accepted.

---

# What is bpffs?

```
/sys/fs/bpf
```

is a virtual filesystem.

Like:

- /proc
- /sys
- /dev

Nothing is stored on disk.

Entries simply reference kernel objects.

---

# Pinning

Pinning creates a filesystem reference to a kernel object.

```
Kernel Program
       │
       ▼
/sys/fs/bpf/router/prog
```

Similarly

```
Kernel Map
      │
      ▼
/sys/fs/bpf/router/mac_map
```

The path is simply another handle to the kernel object.

---

# Why Pinning Exists

Without pinning:

```
Program ID = 42
Map ID = 17
```

IDs are temporary.

Processes cannot rely on them.

Pinning provides a stable name:

```
/sys/fs/bpf/router/mac_map
```

Any process can reopen it later.

---

# Why Pin Maps

Later commands use:

```
bpftool map update pinned ...
```

Example:

```
bpftool map update pinned \
/sys/fs/bpf/router/mac_map
```

Without pinning:

there would be no pathname to reopen the map.

---

# Loading vs Pinning vs Attaching

## Load

Creates objects.

```
Kernel

Program

Maps
```

---

## Pin

Creates filesystem references.

```
Filesystem

↓

Kernel Object
```

---

## Attach

Connects program to interface.

```
ens7f0

↓

XDP Hook

↓

Program
```

Only after attachment does packet processing begin.

---

# Why Two Program Loads?

The same object is loaded twice.

```
Load 1

Program A

tx_port A

mac_map A
```

```
Load 2

Program B

tx_port B

mac_map B
```

Each interface receives its own independent maps.

This allows different forwarding configurations.

---

# Runtime Packet Flow

```
Packet

↓

Read Ethernet

↓

Bounds Check

↓

ARP?

↓

PASS

↓

IPv4?

↓

Lookup tx_port

↓

Lookup mac_map

↓

Rewrite MAC

↓

Redirect
```

---

# Why MAC Rewriting?

Original frame:

```
Dst MAC = Router
Src MAC = Host A
```

After XDP:

```
Dst MAC = Host B
Src MAC = ens7f1
```

Only Ethernet changes.

The IP header remains untouched.

---

# Alternative to Pinning (Method 2)

Instead of filesystem paths:

keep BPF file descriptors.

```
Loader Process

FD 5 → Program

FD 6 → tx_port

FD 7 → mac_map
```

All operations use FDs directly.

```
bpf_map_update_elem(fd,...)
```

instead of

```
bpftool map update pinned ...
```

---

# Why Shell Scripts Cannot Use Method 2

Every command runs in a separate process.

```
bpftool load

↓

Exit
```

File descriptors disappear.

Next command:

```
bpftool map update
```

cannot access the previous FDs.

Therefore shell scripts require pinning.

---

# libbpf Approach

A libbpf loader typically performs:

```
Open object

↓

Load

↓

Get Program FD

↓

Get Map FDs

↓

Update Maps

↓

Attach

↓

Keep Running
```

No pinning required.

---

# Object Lifetime

A BPF object survives as long as **at least one reference exists**.

References include:

- File Descriptor
- XDP attachment
- Pin
- Another BPF object

---

## Process exits after attachment

```
Process exits

↓

FD removed

↓

Program still attached

↓

Program survives

↓

Maps survive
```

---

## Process exits before attachment

```
FD removed

No attachment

No pin

↓

Kernel destroys everything
```

---

## Detached before exit

```
No FD

No attach

No pin

↓

Objects destroyed
```

---

# Do Maps Outlive Userspace?

Depends.

If the program remains attached:

YES

Maps remain alive because:

```
Network Interface

↓

Program

↓

Maps
```

If nothing references them:

NO

Kernel frees them.

---

# Pinning vs Method 2

## Pinning Advantages

- Easy debugging with bpftool
- Easy map inspection
- Multiple processes can access maps
- Loader can exit
- Maps configurable later
- Stable filesystem path
- Easier development
- Persistent management

---

## Pinning Disadvantages

- Requires bpffs
- Manual cleanup
- Extra filesystem management
- Naming required

---

## Method 2 Advantages

- Simpler implementation
- No filesystem
- Automatic cleanup
- Private ownership
- Good for single daemon
- Fewer moving parts

---

## Method 2 Disadvantages

- No external access after loader exits
- Cannot use bpftool easily
- Harder debugging
- Not suitable for shell scripts
- File descriptors cannot be recovered

---

# Which Method Fits Which Use Case?

## Use Pinning When

- Using bpftool
- Using shell scripts
- Need debugging
- Multiple programs manage maps
- Need persistent configuration

---

## Use File Descriptors When

- Single libbpf application
- Long-running daemon
- High-performance production service
- No external map management needed

---

# Mental Models

## Pinning

```
Filesystem

↓

Kernel Object

↓

Any Process
```

---

## Method 2

```
Loader Process

↓

File Descriptor

↓

Kernel Object
```

---

# Key Takeaways

- The XDP program is generic; runtime behavior comes from maps.
- `tx_port` determines the egress interface.
- `mac_map` determines Ethernet source/destination MAC rewriting.
- ARP is passed to Linux so normal address resolution continues.
- Loading creates kernel objects.
- Pinning gives those objects stable filesystem names.
- Attaching connects the program to the NIC's XDP hook.
- Without pinning, libbpf applications keep BPF object FDs alive.
- BPF objects exist as long as at least one valid reference remains.
- For `bpftool` workflows, pinning is the most practical choice.
- For a single long-running libbpf daemon, file descriptors alone are usually sufficient.