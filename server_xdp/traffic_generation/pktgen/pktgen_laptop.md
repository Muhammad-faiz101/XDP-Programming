# Pktgen-DPDK: Ubuntu 10GbE → Laptop 1GbE Experiment

This document walks through a complete end-to-end experiment: generating line-rate Ethernet traffic on an Ubuntu server's 10GbE NIC via Pktgen-DPDK, and receiving/verifying it on a laptop connected over a 1GbE USB Ethernet adapter. It also explains why the *laptop's* link speed — not the server's — ends up being the limiting factor for this particular setup.

Every command includes **why it's run** and **what it does**.

```
Ubuntu Server
    |
    | 10GbE NIC
    | Intel X540-AT2
    | DPDK / vfio-pci
    |
    | Ethernet cable
    |
    v
Laptop
    |
    | USB Ethernet adapter
    | enxf8e43bead16f
    | 1 Gbps
    |
    IP: 192.168.60.3
```

---

## Table of Contents

1. [Ubuntu Server — Identify the DPDK Port](#1-ubuntu-server--identify-the-dpdk-port)
2. [Laptop — Identify Ethernet Interface](#2-laptop--identify-ethernet-interface)
3. [Configure Laptop IP](#3-configure-laptop-ip)
4. [Verify Laptop MAC Address](#4-verify-laptop-mac-address)
5. [Start Pktgen](#5-start-pktgen)
6. [Configure Pktgen Packet](#6-configure-pktgen-packet)
7. [Configure Continuous Transmission](#7-configure-continuous-transmission)
8. [Monitor Pktgen](#8-monitor-pktgen)
9. [Capture Packets on Laptop](#9-capture-packets-on-laptop)
10. [Check Laptop RX Statistics](#10-check-laptop-rx-statistics)
11. [Check Laptop NIC Hardware Statistics](#11-check-laptop-nic-hardware-statistics)
12. [Check Laptop Link Speed](#12-check-laptop-link-speed)
13. [Calculate/Interpret the Pktgen Result](#13-calculateinterpret-the-pktgen-result)
14. [Important Distinction: Two Different Link Capabilities](#14-important-distinction-two-different-link-capabilities)
15. [Useful Troubleshooting Commands](#15-useful-troubleshooting-commands)
16. [Clean Experiment Procedure](#16-clean-experiment-procedure)

---

## 1. Ubuntu Server — Identify the DPDK Port

```bash
sudo dpdk-devbind.py --status
```

**Why:** before generating any traffic, confirm the server's NIC is currently bound to DPDK (`vfio-pci`) rather than its normal kernel driver. The Pktgen port used in this experiment:

```text
0000:d8:00.1
Intel X540-AT2 10GbE
Driver: vfio-pci
```

Check PCI driver:

```bash
sudo lspci -nnk -s d8:00.1
```

**Why:** an independent, kernel-level cross-check of the same binding — confirming what `dpdk-devbind.py` reports also matches what the PCI subsystem itself sees.

Check PCIe link:

```bash
sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'
```

**Why:** confirms the physical PCIe slot is negotiating at its full capable speed/width — ruling out the bus itself as a bottleneck before trying to push 10 Gbps of traffic through it. Expected:

```text
LnkCap: Speed 5GT/s, Width x8
LnkSta: Speed 5GT/s, Width x8
```

Check that the physical link is 10 Gbps:

```bash
sudo ethtool <kernel-interface>
```

**Why this doesn't apply here:** for the DPDK-bound port, there is no normal Linux interface to run `ethtool` against, because the NIC is owned by `vfio-pci` rather than the kernel. This command is only usable *before* the NIC is bound to DPDK, or on a different, kernel-managed interface — it's included here as a reminder of that limitation, not as a step to actually run against `0000:d8:00.1`.

## 2. Laptop — Identify Ethernet Interface

```bash
ip addr
```

**Why:** lists every network interface on the laptop, to identify which one corresponds to the USB Ethernet adapter being used for this test — especially useful if the laptop also has Wi-Fi or a built-in Ethernet port that could otherwise be confused for it. In this setup the USB Ethernet interface is:

```text
enxf8e43bead16f
```

Check link:

```bash
sudo ethtool enxf8e43bead16f
```

**Why:** confirms the adapter's negotiated link state before doing anything else with it. The key result here is:

```text
Speed: 1000Mb/s
Link detected: yes
```

This is the first indication of the experiment's core limitation — the laptop link is **1 Gbps, not 10 Gbps** — established right at the start, before any traffic is generated.

Check interface:

```bash
ip addr show enxf8e43bead16f
```

**Why:** a focused check of just this interface's current address/state, as a baseline before assigning an IP in the next step.

## 3. Configure Laptop IP

```bash
sudo ip addr add 192.168.60.3/24 dev enxf8e43bead16f
```

**Why:** the laptop needs an IP address on the same subnet as the traffic Pktgen will generate, so the kernel recognizes incoming packets addressed to it and processes them at the IP layer rather than ignoring them.

Verify:

```bash
ip addr show enxf8e43bead16f
```

Expected:

```text
inet 192.168.60.3/24
```

**The two ends of this test's addressing:**

```text
Ubuntu/Pktgen source IP :  192.168.60.1
Laptop destination IP   :  192.168.60.3
```

## 4. Verify Laptop MAC Address

```bash
ip link show enxf8e43bead16f
```

**Why:** Pktgen needs to know the laptop's actual hardware MAC address to address frames to it correctly at Layer 2 — an IP address alone isn't enough; without the correct destination MAC, the laptop's NIC would never accept the frame in the first place, regardless of what IP address it contains. This setup's MAC:

```text
MAC: f8:e4:3b:ea:d1:6f
```

This MAC is used as the **Pktgen destination MAC** in [Section 6](#6-configure-pktgen-packet).

## 5. Start Pktgen

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

**Why `sudo env LD_LIBRARY_PATH=...`:** because the DPDK libraries are installed under `/usr/local/lib/x86_64-linux-gnu`, which is outside the default library search path, and `sudo` does not inherit environment variables from the calling shell by default. Wrapping the variable in `env` as part of the `sudo` invocation injects it directly into the root process's environment so Pktgen can find DPDK's shared libraries at startup.

`0000:d8:00.1` is the X540-AT2 10GbE NIC being used by DPDK — the `-a` flag tells DPDK to initialize only this specific PCI device as a usable port.

Check Pktgen process:

```bash
ps aux | grep '[p]ktgen'
```

**Why the brackets around `p`:** this is a small trick to stop `grep` from matching its own process entry in the list. `grep '[p]ktgen'` matches the same text as `grep 'pktgen'`, but since the search pattern itself no longer contains the literal substring `pktgen`, the `grep pktgen` command doesn't show up as a false-positive match against itself.

## 6. Configure Pktgen Packet

Inside the Pktgen interactive prompt (`Pktgen:/>`), configure port 0 with the interface's addressing and packet size:

**Set source/destination MAC:**

```text
set 0 src mac b4:96:91:12:9d:46
set 0 dst mac f8:e4:3b:ea:d1:6f
```

**Set source/destination IP:**

```text
set 0 src ip 192.168.60.1
set 0 dst ip 192.168.60.3
```

**Set packet size:**

```text
set 0 size 1400
```

The `0` in each command refers to Pktgen's port 0 — this is the same DPDK port mapped to `0000:d8:00.1` back in [Section 5](#5-start-pktgen) via the `-a` flag, so every `set 0 ...` command here is configuring that specific interface, not a generic global setting.

Summary of what this configures:

| Field | Value |
|---|---|
| Source IP | `192.168.60.1` |
| Destination IP | `192.168.60.3` |
| Source MAC | `b4:96:91:12:9d:46` |
| Destination MAC | `f8:e4:3b:ea:d1:6f` |
| Packet size | 1400 bytes |

**Why these specific values:** the source IP/MAC identify the Ubuntu server; the destination IP/MAC identify the laptop, exactly as established in Sections 3 and 4. Every generated packet carries these four values in its headers — get any one of them wrong, and the laptop either never receives the frame (wrong destination MAC) or the kernel discards it as not addressed to it (wrong destination IP). The 1400-byte size is deliberately kept under the standard 1500-byte MTU to avoid fragmentation.

Verify the configuration was applied:

```text
show 0
```

**Why:** re-displaying port 0's configuration confirms all four addressing values and the packet size were actually accepted before generating any traffic — much easier to catch a typo here than to debug it later from an empty `tcpdump` capture on the laptop.

Verify Pktgen port link state:

```text
Link State          : <UP-10000-FD>
```

**Why:** confirms the Ubuntu server's physical NIC is linked at 10 Gbps, full duplex — the sending side's own link health, independent of anything happening on the laptop side.

## 7. Configure Continuous Transmission

Pktgen should show:

```text
Tx Count/% Rate     : Forever /100%
```

**Why:** this confirms Pktgen is set to transmit continuously (no fixed packet-count limit — see `Forever`) at 100% of the NIC's line rate, rather than stopping after a small fixed number of packets. This experiment is specifically about testing sustained throughput and how the laptop copes with it, so a bounded test wouldn't reveal the same thing.

Example live readings once running:

```text
Tx Pkts       : 2,333,384
Tx Max        : 88,264 packets/s
```

## 8. Monitor Pktgen

Pktgen's port-level statistics relevant here:

```text
Pkts Rx/Tx
Rate Rx/Tx
Total Rx Pkts
Tx Pkts
Pkts/s Rx Max
Tx Max
Errors Rx/Tx
```

Example:

```text
Tx Pkts : 2,333,384
Tx Max  : 88,264 packets/s
```

**Why these two are the ones to watch:**
- **Tx Pkts** — the cumulative number of packets transmitted since `start`. A steadily climbing value is the clearest possible proof Pktgen is actively generating traffic, regardless of any single instantaneous reading.
- **Tx Max** — the maximum packet rate observed during the run, used directly in [Section 13](#13-calculateinterpret-the-pktgen-result) to work out the actual throughput being generated.

## 9. Capture Packets on Laptop

```bash
sudo tcpdump -i enxf8e43bead16f -nn -e -c 10
```

**Why:** counters alone (Sections 8, 10) prove *that* packets moved, but not *what* they contained. Capturing the actual frames confirms the correct source/destination MACs and IPs are arriving intact — the definitive proof the packets physically reached the laptop's NIC and were visible to Linux. Expected packet content:

```text
b4:96:91:12:9d:46 > f8:e4:3b:ea:d1:6f
192.168.60.1.1234 > 192.168.60.3.5678
```

For continuous capture:

```bash
sudo tcpdump -i enxf8e43bead16f -nn -e
```

**Why:** removing `-c 10` lets the capture run indefinitely instead of stopping after 10 packets — useful for watching traffic live while adjusting Pktgen's rate in real time.

To capture only traffic from the Ubuntu server:

```bash
sudo tcpdump -i enxf8e43bead16f -nn -e 'ether src b4:96:91:12:9d:46'
```

**Why:** filters the capture down to frames whose *source* MAC matches the server's — useful if other, unrelated traffic is also present on the laptop's interface and would otherwise clutter the capture.

Or, filtering at the IP layer instead:

```bash
sudo tcpdump -i enxf8e43bead16f -nn 'host 192.168.60.1'
```

**Why:** an alternative filter matching on IP address rather than MAC — either works; MAC filtering catches traffic even if the IP header were malformed, while IP filtering is often more intuitive when cross-referencing against Pktgen's own IP-based configuration.

## 10. Check Laptop RX Statistics

Before starting Pktgen:

```bash
ip -s link show enxf8e43bead16f
```

**Why establish a baseline first:** recording the RX packet count *before* Pktgen starts makes it possible to clearly attribute any increase afterward specifically to this test's traffic, rather than to background noise already present on the interface.

Start Pktgen and then run the same command again:

```bash
ip -s link show enxf8e43bead16f
```

Example:

```text
RX:
    bytes     packets    errors    dropped
    41460     30         0         0
```

**The fields that matter:**
- **RX packets** — should be climbing compared to the baseline reading, confirming frames are arriving.
- **RX errors** — malformed/corrupted frames detected by the NIC or driver; should stay at zero.
- **RX dropped** — packets the kernel received but discarded (commonly due to buffer/backlog limits); should stay at zero for a clean result.
- **RX missed** — packets the hardware itself couldn't accept in time.

**The core cross-check this enables:** if RX packets on the laptop increase in step with Tx Pkts increasing on the Ubuntu/Pktgen side (Section 8), that's confirmation the packets are actually reaching the laptop — not just being generated and sent into the void.

## 11. Check Laptop NIC Hardware Statistics

```bash
sudo ethtool -S enxf8e43bead16f
```

**Why:** `ip -s link` (Section 10) shows the kernel's own view; `ethtool -S` goes one level deeper, dumping the NIC driver's full internal hardware statistics table — which can reveal issues (like hardware buffer exhaustion) that wouldn't necessarily show up as a simple kernel-level drop count.

Useful filtered version:

```bash
sudo ethtool -S enxf8e43bead16f | grep -Ei 'rx|drop|err|miss|fifo|buf'
```

**Why filter:** the full statistics dump is long and mostly irrelevant to this test. Filtering for receive-related, drop, error, miss, FIFO, and buffer keywords narrows it down to the counters that can reveal hardware-level receive errors or drops — particularly relevant once traffic volume gets high enough to start stressing the adapter.

## 12. Check Laptop Link Speed

```bash
sudo ethtool enxf8e43bead16f
```

The key field:

```text
Speed: 1000Mb/s
```

Therefore:

```text
Ubuntu NIC       = 10 Gbps
Laptop USB NIC   = 1 Gbps
```

**Why this is worth re-confirming here, even though it was already checked in Section 2:** this is the single fact that governs how to interpret every result in this experiment. The end-to-end physical link is limited to whichever end is slower — here, 1 Gbps. The 10GbE Pktgen NIC is fully capable of generating 10GbE-rate traffic, but the laptop-side 1GbE interface physically cannot receive traffic faster than 1 Gbps, no matter what rate Pktgen is configured to send at.

## 13. Calculate/Interpret the Pktgen Result

Pktgen example reading:

```text
Tx Max      : 88,264 packets/s
Packet size : 1400 bytes
```

Approximate payload rate:

```text
88,264 × 1400 × 8 ≈ 989 Mbps
```

**Why this calculation matters:** it converts Pktgen's packets-per-second figure into a bits-per-second throughput figure that can be directly compared against the laptop's known link speed. `packets/s × bytes/packet × 8 bits/byte` gives the approximate raw payload bit rate. The result — **≈989 Mbps** — lines up almost exactly with the laptop's 1 Gbps link, which is exactly what should be expected if the laptop's NIC is the bottleneck limiting throughput, rather than Pktgen or the server's NIC underperforming.

**Why the real wire rate is somewhat higher:** this calculation only accounts for the 1400-byte payload configured in Pktgen. Actual Ethernet frames carry additional overhead — headers, the inter-frame gap, preamble — so the true wire-level bit rate is a bit higher than this payload-only estimate, even though the *received* payload throughput itself is correctly capped near 1 Gbps by the laptop's link.

## 14. Important Distinction: Two Different Link Capabilities

```
Ubuntu X540-AT2
       |
       | 10 Gbps capable
       |
       v
Ethernet cable
       |
       | limited by receiver
       v
Laptop USB Ethernet
       |
       | 1 Gbps
       v
Laptop
```

**Can Ubuntu generate 10 Gbps?**
Yes — the X540-AT2 is a genuine 10GbE NIC, and Pktgen-DPDK can generate traffic at 10GbE rates from the server side.

**Can this particular laptop receive 10 Gbps?**
No — its `enxf8e43bead16f` interface is currently linked at 1000 Mbps (1 Gbps), as confirmed in Sections 2 and 12.

**Why this distinction matters for interpreting any result from this setup:** any throughput ceiling observed here reflects the *laptop's* link, not a limitation of DPDK, Pktgen, or the server's NIC. This setup is valid for testing up to roughly 1 Gbps at the receiver — it should not be used to draw conclusions about 10 Gbps reception performance, since the receiving hardware simply isn't capable of that regardless of how the sender is configured.

## 15. Useful Troubleshooting Commands

### Ubuntu

```bash
sudo dpdk-devbind.py --status
sudo lspci -nnk -s d8:00.1
sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'
ps aux | grep '[p]ktgen'
sudo dpdk-proc-info --proc-type=secondary --file-prefix=pktgen -- --show-port
```

**Why `dpdk-proc-info` is listed separately:** unlike the other commands here, it queries a *running* DPDK primary process (Pktgen, in this case) for live port information, from a secondary process attached to the same DPDK session. It only works while Pktgen is actually running, and only when the DPDK installation/environment matches what Pktgen was built and launched against — a useful but more fragile diagnostic than the others, worth knowing about but not something to rely on if the environment isn't guaranteed consistent.

### Laptop

```bash
ip addr
ip addr show enxf8e43bead16f
ip link show enxf8e43bead16f
sudo ethtool enxf8e43bead16f
ip -s link show enxf8e43bead16f
sudo ethtool -S enxf8e43bead16f
sudo tcpdump -i enxf8e43bead16f -nn -e
```

**Why grouped together like this:** these are the same commands introduced individually in Sections 2–12, gathered here as a single quick-reference block — useful for re-running the full diagnostic sweep without needing to scroll back through the whole document section by section.

## 16. Clean Experiment Procedure

A condensed, ordered run-through of the entire experiment from a clean state.

### Step 1 — Laptop: assign IP

```bash
sudo ip addr add 192.168.60.3/24 dev enxf8e43bead16f
```

### Step 2 — Laptop: verify link

```bash
sudo ethtool enxf8e43bead16f
```

Confirm:

```text
Speed: 1000Mb/s
Link detected: yes
```

### Step 3 — Laptop: start packet capture

```bash
sudo tcpdump -i enxf8e43bead16f -nn -e
```

**Why start the capture *before* Pktgen:** starting the capture first ensures nothing is missed — including the very first packets Pktgen sends once it starts — rather than racing to start a capture after traffic is already flowing.

### Step 4 — Ubuntu: start Pktgen

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

Configure, inside the Pktgen prompt:

```text
set 0 src mac b4:96:91:12:9d:46
set 0 dst mac f8:e4:3b:ea:d1:6f
set 0 src ip 192.168.60.1
set 0 dst ip 192.168.60.3
set 0 size 1400
set 0 count 0
set 0 rate 100
```

Resulting configuration:

```text
Source IP       = 192.168.60.1
Destination IP  = 192.168.60.3
Source MAC      = b4:96:91:12:9d:46
Destination MAC = f8:e4:3b:ea:d1:6f
Packet size     = 1400
Tx Count        = Forever
Tx Rate         = 100%
```

### Step 5 — Ubuntu: check Pktgen TX

Confirm:

```text
Tx Pkts       increasing
Tx Max        increasing
Errors Tx     0
```

### Step 6 — Laptop: check RX

```bash
ip -s link show enxf8e43bead16f
```

Confirm:

```text
RX packets increasing
RX errors  = 0
RX dropped = 0
```

### Step 7 — Laptop: confirm actual packets

```bash
sudo tcpdump -i enxf8e43bead16f -nn -e -c 10
```

You should see:

```text
b4:96:91:12:9d:46 > f8:e4:3b:ea:d1:6f
192.168.60.1 > 192.168.60.3
```

**Why this seven-step order specifically:** each step builds a link in the same chain the traffic itself travels through, in the same order — laptop addressed and listening first, then the sender started, then verified from the sending end outward to the receiving end. If something fails, the step at which it fails immediately narrows down where in this chain the problem is:

```text
Pktgen
  ↓
Ubuntu DPDK X540-AT2 TX
  ↓
10GbE physical link
  ↓
Laptop 1GbE NIC
  ↓
Laptop NIC RX
  ↓
Linux RX
  ↓
tcpdump
```

**Most important conclusion from this setup:** the Ubuntu side is 10GbE-capable, but the laptop's `enxf8e43bead16f` link is 1 Gbps, so this setup should **not** be used to evaluate 10 Gbps reception performance. For a true 10 Gbps experiment, the laptop (or receiving device) needs its own 10GbE NIC/adapter with a link that actually negotiates at 10,000 Mbps.