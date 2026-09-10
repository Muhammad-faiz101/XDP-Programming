# XDP Flow Counter – Task Overview

## Objective

Build an XDP/eBPF application that counts TCP packets for each network flow.

A flow is identified by the 5-tuple:

- Source IP
- Destination IP
- Source Port
- Destination Port
- Protocol

The XDP program runs in the kernel at the network interface, updates a BPF hash map for every TCP packet, and a user-space application periodically reads that map and writes the results to `flows.txt`.

---

# Components

## 1. Kernel Space (XDP Program)

**Responsibilities**

- Runs on every incoming packet.
- Parses Ethernet, IPv4, and TCP headers.
- Constructs a flow key (5-tuple).
- Looks up the flow in the BPF hash map.
- If the flow exists:
  - Increment packet count.
- Otherwise:
  - Create a new flow entry with count = 1.
- Returns `XDP_PASS` so the packet continues through the normal networking stack.

**Shared Data Structure**

- **Map:** `flow_counter`
- **Type:** `BPF_MAP_TYPE_HASH`

Key:
- Source IP
- Destination IP
- Source Port
- Destination Port
- Protocol

Value:
- Packet count

---

## 2. User Space Program

**Responsibilities**

1. Parse the network interface name (e.g., `eth0`) and obtain its interface index.
2. Open the compiled XDP object file (`xdp_program.o`).
3. Load the object into the kernel, which:
   - Verifies the eBPF program.
   - Creates the `flow_counter` BPF map.
   - Loads the XDP bytecode.
4. Locate the `xdp_flow_counter` program within the object file.
5. Attach the XDP program to the selected network interface.
6. Obtain a file descriptor for the `flow_counter` BPF map.
7. Every second:
   - Iterate over all flow entries in the map.
   - Look up the packet count for each flow.
   - Convert IP addresses and port numbers into human-readable format.
   - Write the current flow statistics to `flows.txt`.
8. When the user presses **Ctrl+C**:
   - Detach the XDP program from the network interface.
   - Close the BPF object.
   - Exit the application gracefully.
---

# Application Lifecycle

```text
Start
  │
  ▼
Compile XDP program
  │
  ▼
Run user-space application
  │
  ▼
Open xdp_program.o
  │
  ▼
Load into kernel
  │
  ├── Kernel verifies eBPF program
  ├── Creates flow_counter map
  └── Loads XDP bytecode
  │
  ▼
Attach XDP program to network interface
  │
  ▼
Packets arrive
  │
  ▼
XDP program executes
  │
  ├── Parse packet
  ├── Build flow key
  ├── Lookup map
  └── Increment/Create entry
  │
  ▼
User-space wakes every second
  │
  ├── Iterate through map
  ├── Read flow counters
  ├── Convert IPs/ports
  └── Write flows.txt
  │
  ▼
Ctrl+C
  │
  ▼
Detach XDP program
  │
  ▼
Close BPF object
  │
  ▼
Exit
```

---

# Data Flow

```text
Incoming Packet
       │
       ▼
 Network Interface
       │
       ▼
 XDP Program (Kernel)
       │
       ▼
BPF Hash Map (flow_counter)
       ▲
       │
User-Space Program
       │
       ▼
flows.txt
```

---

# Summary

- **Kernel space** performs high-speed packet processing and maintains per-flow packet counts.
- **BPF Hash Map** acts as shared memory between kernel and user space.
- **User space** manages the XDP program, reads flow statistics, and presents them in a human-readable file.