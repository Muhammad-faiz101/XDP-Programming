# Pktgen-DPDK Setup on Ubuntu Server — Intel X540-AT2

This document explains, step by step, how Pktgen-DPDK was set up and run on top of the existing DPDK 25.11.2 / `vfio-pci` configuration, and how to generate and verify Ethernet traffic with it on the Intel X540-AT2 NIC. Every step explains **why it's needed** and **what it actually does**, not just the commands to run.

This picks up exactly where the Ubuntu Server DPDK setup left off — the NIC is already bound to `vfio-pci` and `testpmd` has already proven the link works. Pktgen now replaces `testpmd` as the traffic-generation application.

---

## Table of Contents

- [1. Objective](#1-objective)
- [2. Hardware](#2-hardware)
- [3. Check DPDK NIC Binding](#3-check-dpdk-nic-binding)
- [4. DPDK Installation](#4-dpdk-installation)
- [5. Hugepages](#5-hugepages)
- [6. Pktgen Build](#6-pktgen-build)
- [7. Starting Pktgen](#7-starting-pktgen)
- [8. Verify Pktgen Port](#8-verify-pktgen-port)
- [9. Configure Packet](#9-configure-packet)
- [10. Packet Size](#10-packet-size)
- [11. Traffic Rate](#11-traffic-rate)
- [12. Generate a Fixed Number of Packets](#12-generate-a-fixed-number-of-packets)
- [13. Monitor Pktgen Statistics](#13-monitor-pktgen-statistics)
- [14. DPDK Port Statistics](#14-dpdk-port-statistics)
- [15. Linux Interface vs DPDK Port — Key Concept](#15-linux-interface-vs-dpdk-port--key-concept)
- [16. Checking the PCI Device](#16-checking-the-pci-device)
- [17. Complete Quick-Start](#17-complete-quick-start)
- [18. Current Working Configuration](#18-current-working-configuration)

---

## 1. Objective

Set up Pktgen-DPDK on an Ubuntu server to generate high-speed Ethernet traffic using the Intel X540-AT2 10-Gbps NIC through DPDK.

The NIC is bound to `vfio-pci`, so Pktgen accesses it directly through DPDK rather than through the normal Linux network interface. **This matters throughout the entire document**: because Pktgen bypasses the kernel entirely, almost every normal Linux networking/monitoring command becomes irrelevant for observing what Pktgen is actually doing — this is explained in detail in [Section 15](#15-linux-interface-vs-dpdk-port--key-concept).

## 2. Hardware

### Server

| Component | Value |
|---|---|
| CPU | Intel Xeon Silver 4210 |
| Logical CPUs | 40 |
| Sockets | 2 |
| NUMA nodes | 2 |
| Architecture | x86_64 |

### DPDK NIC

| Component | Value |
|---|---|
| NIC | Intel Ethernet Controller 10-Gigabit X540-AT2 |
| PCI address | `0000:d8:00.1` |
| PCI ID | `8086:1528` |
| Driver | `vfio-pci` |
| Native kernel driver | `ixgbe` |
| Link speed | 10 Gbps |

The PCIe link was verified with:

```bash
sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'
```

```text
LnkCap: Speed 5GT/s, Width x8
LnkSta: Speed 5GT/s, Width x8
```

**Why check this:** `LnkCap` is what the PCIe slot/card is *capable* of, and `LnkSta` is what it's *currently negotiated* at. Confirming these match (and are wide/fast enough — here 8 lanes at 5 GT/s) rules out the PCIe bus itself as a bottleneck before generating 10 Gbps of Ethernet traffic through it. If `LnkSta` were lower than `LnkCap`, the slot would be under-negotiating and could cap throughput regardless of how well Pktgen is configured.

## 3. Check DPDK NIC Binding

```bash
sudo dpdk-devbind.py --status
```

```text
Network devices using DPDK-compatible driver
============================================
0000:d8:00.1 'Ethernet Controller 10-Gigabit X540-AT2 1528'
    drv=vfio-pci
    unused=ixgbe
```

Verify directly:

```bash
sudo lspci -nnk -s d8:00.1
```

```text
Kernel driver in use: vfio-pci
Kernel modules: ixgbe
```

**Why this check matters *before* starting Pktgen:** Pktgen can only take control of the NIC through DPDK if the NIC is currently bound to a DPDK-compatible driver (`vfio-pci`) rather than its normal kernel driver (`ixgbe`). This confirms that binding — done as part of the earlier DPDK setup — is still in place. If it ever shows `drv=ixgbe` instead (for example after a reboot where the binding wasn't made persistent), Pktgen will fail to find the device and needs to be re-bound before continuing.

> ⚠️ **Important:** When the NIC is bound to `vfio-pci`, `0000:d8:00.1` does **not** have a normal Linux interface such as `ens7f1`. Therefore commands like `ip -s link show ens7f1`, `ethtool ens7f1`, or reading `/sys/class/net/ens7f1/` are **not** valid ways to measure Pktgen traffic — that interface effectively no longer exists from the kernel's point of view. Pktgen communicates with the NIC directly through DPDK, not through Linux networking.

## 4. DPDK Installation

```text
/usr/local/
```

```text
/usr/local/lib/x86_64-linux-gnu/
```

```bash
export LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
```

**Why this is needed:** DPDK was built and installed under `/usr/local/`, which is not always part of the operating system's default library search path. Pktgen is a separate application that dynamically links against DPDK's shared libraries (`.so` files) at startup — if the loader can't find them, Pktgen will fail immediately with:

```text
error while loading shared libraries:
librte_kvargs.so.26: cannot open shared object file
```

Setting `LD_LIBRARY_PATH` explicitly tells the dynamic linker an extra directory to search, so it can locate DPDK's libraries at `/usr/local/lib/x86_64-linux-gnu/` even though that path isn't in the system default. This has to be set in the same environment Pktgen is actually launched from — see the note on `sudo env` in [Section 7](#7-starting-pktgen).

## 5. Hugepages

DPDK requires hugepages for its memory management (see the DPDK setup document for the full explanation of *why* hugepages matter for performance). This setup reused:

- NUMA node 1
- 1024 × 2-MB hugepages

```bash
grep -i huge /proc/meminfo
sudo sysctl vm.nr_hugepages
cat /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

**Why re-check this here:** hugepages are a shared system resource — they were reserved once during the DPDK setup, but they don't automatically persist across reboots or get reserved again for you. Before starting Pktgen, this confirms the memory Pktgen expects to allocate (on NUMA node 1, matching the NIC's node) is actually still there. If Pktgen can't get the hugepage memory it asks for at startup, it will fail to initialize regardless of whether the NIC binding is correct.

## 6. Pktgen Build

| Item | Value |
|---|---|
| Pktgen version | Pktgen-DPDK 25.08.2 |
| Source/build directory | `~/dpdk-lab/pktgen-25.08.2` |
| Executable | `./build/app/pktgen` |

**What this represents:** unlike DPDK itself, this document assumes Pktgen has already been built (that build process — compiling Pktgen from source against the installed DPDK 25.11.2 libraries — is the "next task" handed off at the end of the DPDK setup document). The result of that build is a single executable, `pktgen`, sitting inside `build/app/` in the Pktgen source tree. Everything from here on is about *running* that executable correctly, not building it.

## 7. Starting Pktgen

```bash
sudo env LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu \
./build/app/pktgen \
-l 10-19 \
-n 4 \
-a 0000:d8:00.1 \
--socket-mem=0,1024 \
-- \
-m "[11:12].0"
```

**Why `sudo env LD_LIBRARY_PATH=... ./build/app/pktgen` instead of just `sudo ./build/app/pktgen`:** `sudo` runs the command as root, but by default it does **not** carry over environment variables you've exported in your own shell — it starts a cleaner root environment instead. If `LD_LIBRARY_PATH` were just exported normally and then `sudo pktgen` run afterward, root wouldn't see it and the library error from Step 4 would come right back. Wrapping the command as `sudo env LD_LIBRARY_PATH=... pktgen` explicitly injects that variable into the exact environment the root process runs in, so it can find DPDK's shared libraries.

**What each flag does:**

| Option | Meaning |
|---|---|
| `-l 10-19` | Pins Pktgen's DPDK worker threads (lcores) to logical CPUs 10–19 — the same NUMA-node-1 CPU range used for `testpmd` earlier, keeping compute close to the NIC's memory. |
| `-n 4` | Tells DPDK how many memory channels the system has, so it can lay out memory access patterns efficiently. |
| `-a 0000:d8:00.1` | Explicitly tells DPDK to initialize only this one PCI device as a usable port — the X540 NIC — rather than grabbing every VFIO-bound device present. |
| `--socket-mem=0,1024` | Allocates hugepage memory per NUMA node: 0 MB from node 0, 1024 MB from node 1, matching where the NIC actually lives. |
| `-- -m "[11:12].0"` | Everything after the bare `--` is passed to Pktgen itself rather than to DPDK's own argument parser. `-m` maps specific lcores to specific Pktgen roles/ports — here, lcores 11 and 12 are assigned to handle RX/TX work for port 0. |

The important relationship this whole invocation sets up:

```
DPDK Port 0
     |
     +---- PCI: 0000:d8:00.1
     |
     +---- Intel X540-AT2
```

Once launched, Pktgen takes over the terminal with its own interactive prompt (`Pktgen:/>`), similar to `testpmd`'s interactive mode.

## 8. Verify Pktgen Port

Inside Pktgen:

```text
Pktgen:/> show 0
```

```text
Port:Flags          : 0:---------      Single
Link State          : <UP-10000-FD>
```

**Why check this first, before anything else:** this is the same kind of sanity check as `show port info 0` was for `testpmd` — before configuring any packet contents or trying to send traffic, confirm Pktgen can actually see the NIC as "port 0" and that DPDK reports the physical link as up, at 10 Gbps, full duplex. If this doesn't show `UP-10000-FD`, nothing sent afterward will actually leave the NIC, no matter how correctly the rest of Pktgen is configured — so this is the first thing to check if traffic later appears not to be flowing.

## 9. Configure Packet

| Field | Value |
|---|---|
| Source IP | `192.168.60.1` |
| Destination IP | `192.168.60.3` |
| Source MAC | `b4:96:91:12:9d:46` |
| Destination MAC | `48:b0:2d:ff:10:d0` |

```text
set 0 src mac b4:96:91:12:9d:46
set 0 dst mac 48:b0:2d:ff:10:d0
set 0 src ip 192.168.60.1
set 0 dst ip 192.168.60.3
```

Verify:

```text
show 0
```

**Why this is needed:** Pktgen doesn't know what a "valid" test packet should look like — it generates whatever header fields it's told to. These four `set` commands build the actual Ethernet/IP header that will be stamped onto every generated packet: which MAC addresses the frame claims to be from/to, and which IP addresses the packet claims to be from/to. This is what lets a receiving device (or a packet capture) recognize this traffic as intentional test traffic from this rig, rather than arbitrary garbage. Re-running `show 0` afterward confirms all four values were actually accepted and applied.

## 10. Packet Size

```text
Pkt Size: 1400
```

```text
Pkt Size/Rx:Tx Burst: 1400 / 64:32
```

This means:

- **Packet size = 1400 bytes** — the total size of each generated frame. This is deliberately kept under the standard 1500-byte Ethernet MTU, leaving headroom for any encapsulation overhead so packets aren't fragmented.
- **RX burst = 64** — the number of packets Pktgen tries to pull from the NIC's receive queue in a single batch operation, rather than one at a time. Larger batches amortize per-packet processing overhead and improve throughput.
- **TX burst = 32** — the same idea for transmission: packets are pushed to the NIC in batches of 32 per operation, rather than individually.

**Why batching matters:** this is one of the core techniques that makes DPDK/Pktgen fast — touching the NIC's hardware queues per-packet is expensive; touching them once per *batch* of packets amortizes that cost across many packets at once.

## 11. Traffic Rate

```text
set 0 rate 1
start 0
```

```text
stop 0
```

```text
Tx Count/% Rate: Forever /1%
```

**Why this matters and what it actually means:** `rate` is a *percentage of line rate* (the theoretical maximum the 10 Gbps link could carry), **not** an absolute value like "1 Gbps." Setting `rate 1` means Pktgen will attempt to transmit at roughly 1% of 10 Gbps (~100 Mbps), not 1 Gbps. This distinction matters a lot when trying to hit a specific target throughput for a test — the rate value always needs to be interpreted relative to the link speed, not as a fixed unit.

`start 0` begins transmission on port 0 at that rate; `stop 0` halts it. `Forever` in the status output indicates no packet-count limit is set (see next section) — it will run indefinitely until manually stopped.

## 12. Generate a Fixed Number of Packets

```text
set 0 count 1000
start 0
```

```text
set 0 count 0
start 0
```

```text
stop 0
```

**Why this option exists:** for a controlled, repeatable experiment (e.g., measuring exactly how many packets a downstream device receives/drops), it's useful to send a *known, finite* number of packets rather than a continuous stream. `set 0 count 1000` configures Pktgen to automatically stop itself after transmitting exactly 1000 packets — no need to manually time a `stop`.

Setting `count 0` switches back to continuous/unlimited transmission (this is effectively the default "Forever" mode from Section 11), which then requires a manual `stop 0` to end.

## 13. Monitor Pktgen Statistics

Key metrics to watch:

- **Pkts/s Tx** — packets per second currently being transmitted; the live throughput rate in packet terms.
- **MBits/s Tx** — the same throughput expressed in megabits per second; useful for comparing directly against the 10 Gbps link capacity.
- **Total Tx Pkts** — the running total of packets transmitted since `start` — this is the number that should be steadily climbing while traffic is flowing.
- **Errors Tx** — transmit-side errors reported by Pktgen/DPDK; ideally stays at zero.
- **Tx Max** — the peak transmit rate observed during the run.

```text
Pkts/s Tx       : 100
Tx Pkts         : 300
Tx Max          : 100
Errors Rx/Tx    : 0/0
```

**Why `Total Tx Pkts` is the single most important number here:** the per-second rate (`Pkts/s Tx`) can momentarily read zero just from screen refresh timing even while things are working, but `Total Tx Pkts` is cumulative — if it is visibly increasing between two checks, that's unambiguous proof Pktgen actually handed those packets off to the DPDK port for transmission, regardless of what any single instantaneous reading shows.

## 14. DPDK Port Statistics

Beyond Pktgen's own view, DPDK exposes a lower-level per-port statistics view:

- **Pkts Rx/Tx** — raw packet counts at the DPDK port level (as opposed to Pktgen's application-level counters in Section 13).
- **Rx Errors/Missed** — packets DPDK's driver failed to receive or process correctly.
- **Rate Rx/Tx** — instantaneous rate as seen by the DPDK port itself.
- **MAC Address** — the hardware MAC address DPDK reports for this port (should match what was configured in Section 9).
- **Link Status** — the live link-up/speed/duplex state, same style of output as Section 8.

```text
PCI Address     : 0000:d8:00.1
Pkts Rx/Tx      : 47/10
Rx Errors/Missed: 0/0
Rate Rx/Tx      : 0/0
MAC Address     : B4:96:91:12:9D:46
Link Status     : <UP-10000-FD>
```

```text
Pkts Rx/Tx
        ^
        TX
```

**Why this view is useful in addition to Pktgen's own stats:** Pktgen's statistics (Section 13) reflect what the *application* thinks it sent. This view reflects what the *DPDK port/driver layer* actually recorded at the hardware-facing level. Comparing the two is a way to sanity-check that Pktgen's counters and the actual NIC-level activity agree — if they diverged significantly, that would point to a problem between the application and the driver rather than the physical link itself.

## 15. Linux Interface vs DPDK Port — Key Concept

This is the single most important concept for interpreting *anything* observed in this setup.

The X540 used by Pktgen (`0000:d8:00.1`) is bound to `vfio-pci`, so the traffic path looks like this:

```
Linux network stack
       |
       X
       |
0000:d8:00.1
       |
    vfio-pci
       |
      DPDK
       |
    Pktgen
```

The **X** marks where the connection to the normal Linux network stack is deliberately broken — Pktgen's traffic never touches it.

**Why this matters practically:** it means standard Linux tools that people normally reach for to check traffic — `ip -s link`, `tc -s qdisc`, `ss`, or reading `/proc/net/snmp` — report **nothing meaningful** about Pktgen's traffic on this port. They aren't wrong or broken; they simply have no visibility into a device that has been deliberately removed from the kernel's control. Relying on them here would give a false impression that no traffic is happening at all, even while Pktgen is actively transmitting at full rate.

**What to use instead**, layered together for a complete picture:

- Pktgen's own statistics (Section 13) — the application-level view.
- DPDK port statistics (Section 14) — the driver-level view.
- NIC hardware statistics where available — the lowest-level view, closest to the physical hardware counters.

## 16. Checking the PCI Device

```bash
lspci -nn -s d8:00.1
sudo lspci -nnk -s d8:00.1
sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'
```

```text
Intel Ethernet Controller 10-Gigabit X540-AT2
PCI ID: 8086:1528
PCI address: 0000:d8:00.1
```

**Why these are useful as a standalone reference:** all three of these were already used individually earlier in this document (Sections 2 and 3), but they're worth having together as a quick device-identity and link-health check whenever something needs re-verifying mid-session — confirming the device is still present, still correctly identified by PCI ID, and still bound the way it's expected to be — without having to hunt back through earlier sections.

## 17. Complete Quick-Start

The condensed end-to-end workflow, once the system is already configured as described above:

### 1. Verify NIC

```bash
sudo dpdk-devbind.py --status
```

Confirms `0000:d8:00.1` still shows `drv=vfio-pci` — i.e., nothing has reverted the binding since the last session (see Section 3 for why this can happen).

### 2. Start Pktgen

```bash
cd ~/dpdk-lab/pktgen-25.08.2
sudo env LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu \
./build/app/pktgen \
-l 10-19 \
-n 4 \
-a 0000:d8:00.1 \
--socket-mem=0,1024 \
-- \
-m "[11:12].0"
```

Launches Pktgen with the correct library path, CPU pinning, and NIC selection (Section 7).

### 3. Configure packet

```text
set 0 src mac b4:96:91:12:9d:46
set 0 dst mac 48:b0:2d:ff:10:d0
set 0 src ip 192.168.60.1
set 0 dst ip 192.168.60.3
```

Sets the header fields every generated packet will carry (Section 9).

### 4. Verify

```text
show 0
```

Confirms `Link State: <UP-10000-FD>` and that the MAC/IP fields were applied correctly before sending anything (Section 8).

### 5. Generate traffic

```text
set 0 rate 1
start 0
```

Begins transmission at 1% of line rate (Section 11).

```text
stop 0
```

Halts it.

### 6. Check TX

Look at:

- Total Tx Pkts
- Pkts/s Tx
- MBits/s Tx
- Errors Tx

Confirms traffic was actually generated and counted, with `Total Tx Pkts` as the definitive indicator (Section 13).

## 18. Current Working Configuration

A full snapshot of the working setup for quick reference:

```
====================================================
              PKTGEN-DPDK CONFIGURATION
====================================================

OS:
Ubuntu Linux x86_64

CPU:
Intel Xeon Silver 4210
40 logical CPUs
2 NUMA nodes

DPDK NIC:
Intel X540-AT2
PCI: 0000:d8:00.1
PCI ID: 8086:1528
Driver: vfio-pci
Link: 10 Gbps Full Duplex

Pktgen:
25.08.2

Pktgen binary:
~/dpdk-lab/pktgen-25.08.2/build/app/pktgen

DPDK libraries:
/usr/local/lib/x86_64-linux-gnu/

Pktgen lcores:
10-19

Pktgen mapping:
[11:12].0

NUMA memory:
socket 0: 0 MB
socket 1: 1024 MB

Packet:
Size: 1400 bytes
Source IP: 192.168.60.1
Destination IP: 192.168.60.3
Source MAC: b4:96:91:12:9d:46
Destination MAC: 48:b0:2d:ff:10:d0

DPDK Port:
Port 0
PCI: 0000:d8:00.1
====================================================
```

This is the state to compare against if a future session behaves differently — CPU pinning, socket memory split, packet size, and MAC/IP values are all things that can silently drift if reconfigured differently between sessions.
