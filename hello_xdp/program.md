# XDP Program Workflow: Compiling, Loading, and Attaching

A reference guide for building, injecting, and testing eXpress Data Path (XDP) programs using standard Linux toolchains.

## Prerequisites

Install the necessary compiler and networking utilities:

```bash
sudo apt-get update
sudo apt-get install -y clang llvm libbpf-dev libelf-dev gcc-multilib \
                        linux-headers-$(uname -r) linux-tools-common \
                        linux-tools-$(uname -r) bpftool iproute2 tcpdump ethtool
```

## Compiling an XDP Program

XDP C source code must be compiled into eBPF bytecode using Clang with optimization enabled.

### Direct Clang Command

```bash
clang -O2 -g -Wall -target bpf -c xdp_program.c -o xdp_program.o
```

### Compiler Flags Breakdown

- `-O2`: Mandatory optimization flag required for the LLVM BPF backend to unroll loops and emit code that passes the BPF verifier.
- `-g`: Emits BPF Type Format (BTF) debug records for kernel introspection.
- `-Wall`: Enables standard compiler warnings.
- `-target bpf`: Emits eBPF bytecode instead of host machine instructions.
- `-c`: Compiles and assembles the object without linking.

### Automated Build with Makefile

Create a `Makefile` in the directory:

```makefile
CC = clang
CFLAGS = -O2 -g -Wall -target bpf

TARGET = xdp_pass
BPF_C = $(TARGET).c
BPF_OBJ = $(TARGET).o

all: $(BPF_OBJ)

$(BPF_OBJ): $(BPF_C)
    $(CC) $(CFLAGS) -c $< -o $@

clean:
    rm -f $(BPF_OBJ)

.PHONY: all clean
```

Execute build:

```bash
make
```

Clean build artifacts:

```bash
make clean
```

## Managing Programs with iproute2

The `ip link` command attaches or detaches XDP programs directly to a network interface.

### Generic Mode (xdpgeneric / SKB Mode)

Use generic mode for testing on loopback (`lo`), virtual interfaces (`veth`), or drivers lacking native XDP support:

```bash
# Attach
sudo ip link set dev lo xdpgeneric obj xdp_program.o sec xdp

# Verify
ip link show dev lo

# Detach
sudo ip link set dev lo xdpgeneric off
```

### Native Driver Mode (xdpdrv / Driver Mode)

Use native mode for physical NICs with driver-level XDP hooks:

```bash
# Attach
sudo ip link set dev eth0 xdpdrv obj xdp_program.o sec xdp

# Verify
ip link show dev eth0

# Detach
sudo ip link set dev eth0 xdpdrv off
```

### Hardware Offload Mode (xdpoffload)

Use offload mode on supported SmartNIC hardware:

```bash
# Attach
sudo ip link set dev eth0 xdpoffload obj xdp_program.o sec xdp

# Detach
sudo ip link set dev eth0 xdpoffload off
```

## Managing Programs with bpftool

`bpftool` provides lower-level kernel inspection and persistent object pinning.

### Mount BPF Filesystem

Ensure `/sys/fs/bpf` is mounted:

```bash
sudo mount -t bpf bpf /sys/fs/bpf
```

### Load and Pin Program

```bash
sudo bpftool prog load xdp_program.o /sys/fs/bpf/my_xdp_prog type xdp
```

### Attach Pinned Program to an Interface

```bash
# Generic mode on loopback
sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/my_xdp_prog dev lo

# Native mode on physical interface
sudo bpftool net attach xdpdrv pinned /sys/fs/bpf/my_xdp_prog dev eth0
```

### Inspect Attached Programs and Maps

```bash
# List all network interfaces with attached XDP hooks
sudo bpftool net list

# View program metadata
sudo bpftool prog show name my_xdp_prog

# Dump translated BPF bytecode instructions
sudo bpftool prog dump xlated pinned /sys/fs/bpf/my_xdp_prog
```

### Detach and Clean Up

```bash
# Detach from interface
sudo bpftool net detach xdpgeneric dev lo

# Remove pinned reference
sudo rm -f /sys/fs/bpf/my_xdp_prog
```

## Live Kernel Debugging with trace_pipe

When using `bpf_printk()` inside your C code, messages are sent to the kernel tracing ring buffer.

### Read Live Trace Stream

```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

### Filter Output

```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe | grep "bpf_trace_printk"
