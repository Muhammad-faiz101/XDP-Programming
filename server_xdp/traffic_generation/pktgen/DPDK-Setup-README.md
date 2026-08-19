# Ubuntu Server — DPDK 25.11.2 Setup (Intel X540-AT2)

This document explains, step by step, how DPDK 25.11.2 was set up on an Ubuntu server and how the Intel X540-AT2 NIC was prepared for use with Pktgen. Every step explains **why it's needed** and **what it actually does**, not just the commands to run.

The setup stops right before Pktgen traffic configuration and Jetson-side setup — those are separate, later stages.

---

## Table of Contents

- [Step 1 — Server Environment](#step-1--server-environment)
- [Step 2 — Target NIC](#step-2--target-nic)
- [Step 3 — Install Build Dependencies](#step-3--install-build-dependencies)
- [Step 4 — DPDK Source](#step-4--dpdk-source)
- [Step 5 — Configure DPDK](#step-5--configure-dpdk)
- [Step 6 — Build DPDK](#step-6--build-dpdk)
- [Step 7 — Install DPDK](#step-7--install-dpdk)
- [Step 8 — Hugepages](#step-8--hugepages)
- [Step 9 — Hugepage Permissions](#step-9--hugepage-permissions)
- [Step 10 — Test DPDK Before NIC Binding](#step-10--test-dpdk-before-nic-binding)
- [Step 11 — Load VFIO](#step-11--load-vfio)
- [Step 12 — Check Target NIC](#step-12--check-target-nic)
- [Step 13 — Check IOMMU Group](#step-13--check-iommu-group)
- [Step 14 — Remove Linux IP From Target NIC](#step-14--remove-linux-ip-from-target-nic)
- [Step 15 — Bind X540 to VFIO](#step-15--bind-x540-to-vfio)
- [Step 16 — Run DPDK Testpmd](#step-16--run-dpdk-testpmd)
- [Step 17 — Current Server State](#step-17--current-server-state)
- [Step 18 — Next Step](#step-18--next-step)

---

## Step 1 — Server Environment

This is a record of the exact toolchain and hardware the build was validated against. DPDK is sensitive to compiler, Meson/Ninja, and kernel versions — if something breaks later, this table is the first thing to compare against a working state.

| Component | Version / Value |
|---|---|
| Ubuntu | 26.04 LTS |
| Kernel | 7.0.0-29-generic |
| GCC | 15.2.0 |
| Meson | 1.10.1 |
| Ninja | 1.13.2 |
| pkg-config | 2.5.1 |
| CPU | 40 logical CPUs |
| NUMA | 2 nodes |
| NUMA 1 CPUs | 10–19, 30–39 |


**Why NUMA matters here:** the server has two physical CPU/memory regions (NUMA nodes). A CPU can access memory on its own node quickly, but reading memory that physically belongs to the other node is slower. Since the target NIC lives on NUMA node 1, every CPU core and memory allocation used later for DPDK is deliberately kept on node 1 too, to avoid that cross-node latency penalty.

We choose numa node 1 because for the NIC X540 , the corresponding numa node is 1 
for dev in 00:1f.6 02:00.0 04:00.0 86:00.0 86:00.1 af:00.0 d8:00.0 d8:00.1; do
    echo -n "$dev -> NUMA "
    cat /sys/bus/pci/devices/0000:$dev/numa_node
done
00:1f.6 -> NUMA 0
02:00.0 -> NUMA 0
04:00.0 -> NUMA 0
86:00.0 -> NUMA 1
86:00.1 -> NUMA 1
af:00.0 -> NUMA 1
d8:00.0 -> NUMA 1
d8:00.1 -> NUMA 1

to check for port/bus_info
ethtool -i <interface>

## Step 2 — Target NIC

| Field | Value |
|---|---|
| Target NIC | Intel X540-AT2 |
| Target PCI device | `0000:d8:00.1` |
| Original Linux interface | `ens7f1` |
| NIC driver before DPDK | `ixgbe` |
| NIC NUMA node | 1 |
| Link | 10 Gbps / Full Duplex |
| Management interface | `eno1` |
| Management IP | `10.4.136.195` |

**What's happening conceptually:** this NIC currently behaves like a normal Linux network card (`ens7f1`, driven by the kernel's `ixgbe` driver). The whole point of this setup is to take it *away* from the kernel and hand it directly to DPDK instead, so packets can be generated/processed in userspace without going through the Linux networking stack — which is what makes DPDK fast.

`eno1` is a *separate* NIC used only for SSH access to the server. It stays on the normal kernel network stack the entire time.

> ⚠️ **Why this matters:** if `eno1` were ever bound to DPDK/VFIO instead of `ens7f1`, the server would lose its network/SSH connection immediately, and you'd need physical/console access to fix it. Every step below is written to only ever touch `ens7f1` / `0000:d8:00.1`.

## Step 3 — Install Build Dependencies

```bash
sudo apt update
sudo apt install -y build-essential git pkg-config libnuma-dev libpcap-dev libbsd-dev python3 python3-pip python3-pyelftools meson ninja-build
meson --version
ninja --version
pkg-config --version
```

**Why this step exists:** DPDK is not installed from a pre-built package here — it's compiled from source. That requires a full C build toolchain (`build-essential`, `gcc`), the two tools that actually drive the DPDK build (`meson` for configuring the build, `ninja` for compiling it fast in parallel), and a handful of libraries DPDK links against:

- `libnuma-dev` — lets DPDK query and pin itself to specific NUMA nodes (see Step 1).
- `libpcap-dev` — enables DPDK's optional pcap-based virtual devices (useful for testing without real hardware).
- `libbsd-dev` — provides some BSD compatibility functions DPDK's codebase uses.
- `python3-pyelftools` — used by DPDK's build scripts to inspect compiled binaries (ELF files) as part of some build/validation steps.
- `pkg-config` — lets other programs (like Pktgen later) automatically discover DPDK's compile flags and library paths once it's installed.

The three `--version` checks at the end just confirm the tools installed correctly and are on the `PATH` before spending time on a full build.

## Step 4 — DPDK Source

```bash
mkdir -p ~/dpdk-lab
cd ~/dpdk-lab
tar -xf dpdk-25.11.2.tar.xz
cd dpdk-stable-25.11.2
pwd
ls
```

**What this does:** creates a dedicated working directory, then unpacks the official DPDK 25.11.2 source tarball into it. `pwd`/`ls` are just sanity checks to confirm you're standing inside the correct extracted source tree before configuring anything — an easy mistake to make when several DPDK versions or Pktgen sources end up living side by side later.

## Step 5 — Configure DPDK

```bash
meson setup build -Dwerror=false -Denable_drivers=net/ixgbe
meson configure build | grep -E 'buildtype|werror|drivers'
```

**Why:** `meson setup` generates the actual build system (into a `build/` directory) based on the options given.

- `-Dwerror=false` — tells the compiler not to treat warnings as hard errors. Without this, an otherwise-harmless compiler warning (common across different GCC versions) could abort the entire build.
- `-Denable_drivers=net/ixgbe` — DPDK supports dozens of NIC drivers by default. Building only the one actually needed (`ixgbe`, which is what the X540-AT2 uses) keeps the build faster and the resulting binaries smaller, instead of compiling support for hardware that isn't present.

The `meson configure` check afterward just re-prints the settings that were actually applied, as confirmation before committing to a full compile.

## Step 6 — Build DPDK

```bash
ninja -C build
echo $?
```

**What this does:** `ninja` reads the build plan Meson generated in Step 5 and actually compiles all of DPDK's source code into libraries and tools. `-C build` just tells Ninja to run inside the `build/` directory rather than needing to `cd` into it first.

`echo $?` prints the exit code of the previous command — `0` means "success," anything else means the build failed somewhere and needs to be re-checked before moving on. It's a cheap, explicit way to catch a broken build instead of assuming it worked.

## Step 7 — Install DPDK

```bash
sudo meson install -C build
pkg-config --modversion libdpdk
which dpdk-test
which dpdk-testpmd
which dpdk-devbind.py
```

**What this does:** copies the compiled DPDK libraries, headers, and command-line tools out of the local `build/` folder and into standard system locations (so other programs, and other users, can find and use them without needing the source tree).

- `pkg-config --modversion libdpdk` confirms the system now recognizes DPDK as an installed library, and that it reports the expected version (`25.11.2`) — this is exactly the mechanism Pktgen's own build will later use to auto-detect DPDK.
- The three `which` checks confirm the DPDK command-line utilities were installed and are reachable on the `PATH`:
  - `dpdk-test` — a general internal test/diagnostic tool for DPDK itself.
  - `dpdk-testpmd` — a reference packet-forwarding application, used here in Step 16 to prove the NIC works under DPDK before Pktgen enters the picture.
  - `dpdk-devbind.py` — the utility used to switch a NIC between its normal kernel driver and a DPDK-compatible one (used in Step 15).

## Step 8 — Hugepages

```bash
sudo sh -c 'echo 1024 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages'
cat /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
grep -i Huge /proc/meminfo
mount | grep hugetlbfs
```

**Why hugepages exist:** normal system memory is managed in small 4 KB pages. DPDK instead uses "hugepages" — much larger blocks of memory (2 MB each here) — because fewer, bigger pages mean far less CPU overhead spent on memory-address translation. That overhead reduction is a large part of why DPDK can process packets so much faster than the regular kernel networking path.

**What the command does:** writes `1024` into a special kernel file that controls how many 2 MB hugepages are reserved specifically on NUMA node 1 (1024 × 2 MB = 2 GB total). This memory is set aside exclusively for DPDK's use and won't be available to normal applications.

- The `cat` command reads that same file back to confirm the kernel actually accepted and applied the request.
- `grep -i Huge /proc/meminfo` shows a system-wide view of hugepage usage, as a second confirmation.
- `mount | grep hugetlbfs` checks that the special filesystem DPDK uses to access hugepage memory (`hugetlbfs`, mounted at `/dev/hugepages`) is present — without it, DPDK has no way to actually claim the reserved pages at runtime.

## Step 9 — Hugepage Permissions

```bash
sudo chown xupsys:xupsys /dev/hugepages
ls -ld /dev/hugepages
```

**Why:** by default, `/dev/hugepages` may be owned by `root`, but DPDK applications (Pktgen included) are often run as a regular, non-root user. If that user doesn't own or have access to `/dev/hugepages`, DPDK fails to allocate memory at startup even though the hugepages themselves were successfully reserved in Step 8.

**What this does:** changes ownership of that directory to the `xupsys` user/group, so processes running as that user can actually map the reserved hugepage memory. `ls -ld` just confirms the ownership change took effect.

## Step 10 — Test DPDK Before NIC Binding

```bash
dpdk-test --version
```

**Why:** this is a checkpoint. Before touching the NIC's driver binding (which is the riskier, less-reversible-feeling step), this confirms DPDK itself is installed correctly and runnable — independent of any NIC or hardware issues.

If it drops into an interactive `RTE>>` prompt instead of just printing a version and exiting, that's DPDK's internal test shell starting up correctly; exit it normally with `quit` rather than killing the process.

## Step 11 — Load VFIO

```bash
sudo modprobe vfio-pci
lsmod | grep vfio
```

**Why:** `vfio-pci` is the kernel driver that DPDK uses to take direct, userspace control of a PCI device (the NIC) while keeping it safely isolated from the rest of the system via IOMMU protection (see Step 13). It isn't loaded by default, so it has to be explicitly loaded before anything can be bound to it.

**What this does:** `modprobe vfio-pci` loads that kernel module (and its dependencies) into the running kernel. `lsmod | grep vfio` lists currently loaded kernel modules and filters for VFIO-related ones, confirming `vfio_pci`, `vfio_pci_core`, `vfio_iommu_type1`, `vfio`, and `iommufd` are all present and active — everything VFIO needs is now available for Step 15.

## Step 12 — Check Target NIC

```bash
ethtool -i ens7f1
sudo ethtool ens7f1 | grep -E 'Speed|Duplex|Link detected'
```

**Why:** before deliberately removing this NIC from the kernel's control, this confirms it's currently healthy and behaving as expected under its normal driver — a known-good baseline to compare against if anything looks wrong after the DPDK binding later.

**What this does:** `ethtool -i` reports driver/hardware info for the interface (confirming it's `ixgbe` at PCI address `0000:d8:00.1`, matching Step 2). The second command reports live link state — that the physical cable is connected, negotiating the full 10 Gbps, and running full duplex — proving the hardware link itself is fine before DPDK is introduced.

## Step 13 — Check IOMMU Group

```bash
readlink /sys/bus/pci/devices/0000:d8:00.1/iommu_group
ls -l /sys/kernel/iommu_groups/8/devices/
```

**Why this matters:** VFIO hands over direct hardware access to a PCI device, which is inherently risky unless it's isolated. The IOMMU (a hardware feature) groups PCI devices so that memory access from one device in a group can't accidentally read/write another device's memory. VFIO requires that *every* device sharing an IOMMU group be handled consistently — if other, unrelated devices shared the same group as the NIC, binding the NIC to VFIO could affect them too.

**What this does:** the first command finds which IOMMU group the target NIC belongs to (group `8` here). The second lists every PCI device inside that group, confirming the group contains *only* `0000:d8:00.1` — meaning the NIC is cleanly isolated and safe to bind to VFIO without any side effects on other hardware.

## Step 14 — Remove Linux IP From Target NIC

```bash
sudo ip addr flush dev ens7f1
ip addr show dev ens7f1
ip route show dev ens7f1
ip route get 8.8.8.8
```

**Why:** the NIC currently has a normal Linux IP address (`192.168.60.1/24`) assigned, as if it were going to be used for regular networking. Since it's about to be handed entirely to DPDK, that address is no longer meaningful to the kernel and is removed to avoid a stale, non-functional configuration lingering on an interface the kernel won't actually control anymore.

**What this does:** `ip addr flush` removes all IP addresses from `ens7f1`. The two `show` commands confirm no address or route remains attached to it.

> ⚠️ **Critical safety check:** `ip route get 8.8.8.8` shows which interface the system would use to reach the outside internet. This must still report `eno1` (the management interface) with the server's real management IP — confirming that removing the address from `ens7f1` had no effect on SSH/management connectivity, which stays entirely on `eno1`.

## Step 15 — Bind X540 to VFIO

```bash
sudo dpdk-devbind.py --bind=vfio-pci 0000:d8:00.1
sudo dpdk-devbind.py --status
lspci -s d8:00.1 -nnk
```

**Why:** this is the actual handover. Up to this point the NIC has still technically been under the kernel's `ixgbe` driver, just without an IP address. This step detaches it from `ixgbe` and attaches it to `vfio-pci` instead — after this, the NIC no longer appears as a normal Linux network interface (`ens7f1` effectively disappears) and can only be accessed through DPDK.

**What this does:** `dpdk-devbind.py --bind=vfio-pci` performs the driver swap for the specified PCI address. The `--status` check afterward lists all NICs DPDK is aware of and confirms `0000:d8:00.1` now shows `drv=vfio-pci`. `lspci -nnk` is an independent, kernel-level cross-check of the same fact — showing "Kernel driver in use: vfio-pci" straight from the PCI subsystem itself, not just from DPDK's own bookkeeping.

## Step 16 — Run DPDK Testpmd

```bash
sudo dpdk-testpmd -l 10-19 -n 4 -a 0000:d8:00.1 --socket-mem=0,1024 -- --interactive
```

Inside the testpmd prompt:

```text
show port info 0
show config rxtx
start
show port stats 0
stop
quit
```

**Why this step exists:** this is the end-to-end proof that everything above actually works together — DPDK can see the NIC, allocate the right memory, and pass traffic — using DPDK's own reference application, *before* introducing Pktgen as an additional variable.

**What each launch flag does:**
- `-l 10-19` — pins `testpmd`'s worker threads to logical CPUs 10 through 19, which are on NUMA node 1 (same node as the NIC — see Step 1) for the shortest possible memory access path.
- `-n 4` — tells DPDK the number of memory channels on the system, which affects how it interleaves memory allocations for performance.
- `-a 0000:d8:00.1` — explicitly tells DPDK to initialize *only* this PCI device as a usable port, rather than scanning/grabbing every VFIO-bound device on the system.
- `--socket-mem=0,1024` — allocates memory from the hugepage pool per NUMA node: `0` MB from node 0, `1024` MB from node 1 — again keeping memory local to where the NIC and CPUs actually are.
- `-- --interactive` — everything after the bare `--` is passed to the `testpmd` application itself rather than to DPDK's own argument parser; `--interactive` starts it in a live command prompt instead of immediately running a fixed test and exiting.

**What each interactive command does:**
- `show port info 0` — prints details for DPDK port 0 (which maps to the X540), confirming the driver (`net_ixgbe`), and that link status/speed/duplex report up, 10 Gbps, full-duplex — i.e., DPDK sees exactly what `ethtool` saw back in Step 12, just now through the DPDK path instead of the kernel.
- `show config rxtx` — shows the current receive/transmit queue configuration (queue counts and descriptor ring sizes), the baseline settings traffic will run against.
- `start` — begins packet forwarding/processing on the port.
- `show port stats 0` — shows live packet counters, confirming traffic is actually flowing (or, at idle, that the port is active and counting).
- `stop` — halts forwarding.
- `quit` — exits `testpmd` cleanly.

## Step 17 — Current Server State

A snapshot summary of everything confirmed working after Steps 1–16:

```
DPDK:              25.11.2 — WORKING
X540:               0000:d8:00.1 — WORKING
Current driver:     vfio-pci
NUMA:               NIC = node 1, Hugepages = node 1
Hugepages:          1024 × 2 MB = 2 GB
testpmd:            WORKING
10-Gbps X540 link:  WORKING
Management:         eno1 / 10.4.136.195
```

This is the state the *next* stage (Pktgen build) should be started from, and the state to roll back to for comparison if something goes wrong later.

## Step 18 — Next Step

The Ubuntu server-side DPDK setup is **complete**. The NIC is bound, hugepages are configured, and DPDK has been proven working via `testpmd`.

**Next task:** install/build Pktgen-DPDK compatible with DPDK 25.11.2.

> ⚠️ Do not change the current DPDK/VFIO setup while doing this — Pktgen should be built *against* this existing, working configuration, not alongside a modified one.

Target architecture after the Pktgen build:

```
Pktgen
   |
DPDK
   |
vfio-pci
   |
d8:00.1 / X540
```
