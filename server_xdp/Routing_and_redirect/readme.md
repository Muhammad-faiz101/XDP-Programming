# XDP MAC-Rewriting Router — Build, Load, Verify, Teardown

Two-legged XDP router that rewrites Ethernet source/dest MACs and redirects
IPv4 traffic between `ens7f0` and `ens7f1` using `bpf_redirect_map`. ARP is
passed through to the kernel stack; non-IP/non-ARP traffic is dropped.

## 0. Prerequisites

- `clang`/`llvm` with BPF target support
- `libbpf-dev` (for `bpf/bpf_helpers.h`, `bpf/bpf_endian.h`)
- `bpftool`
- `/sys/fs/bpf` bpffs support (kernel config `CONFIG_BPF`, `CONFIG_XDP_SOCKETS`, etc.)
- Root/sudo access

## 1. Source: `xdp_router.c`


## 2. Compile

```bash
clang -O2 -g -target bpf -c xdp_router.c -o xdp_router.o
```

**Sanity-check the object file before loading it** — this bit the first attempt:

```bash
llvm-objdump -h xdp_router.o
```

A good build shows an `xdp` section with non-zero size and a `.maps` section:

```
Idx Name                   Size     VMA              Type
  3 xdp                    000001c0 0000000000000000 TEXT
  5 .maps                  00000040 0000000000000000 DATA
  6 license                00000004 0000000000000000 DATA
```

> **Known failure mode:** if `xdp` shows `Size 00000000` and there's no
> `.maps`/`license` section, the compiler silently produced an empty object
> (e.g. stale/half-written source, or a syntax issue that got swallowed).
> `bpftool prog load` will then fail with:
> `Error: object file doesn't contain any bpf program`
> and every step after it cascades into `No such file or directory` errors
> because the pins were never created. Fix: re-check the source, recompile,
> and re-run `llvm-objdump -h` until the `xdp` section is non-empty before
> proceeding.

## 3. Load, Pin, and Attach

```bash
IF1="ens7f0"
IF2="ens7f1"

IFINDEX1=$(cat /sys/class/net/$IF1/ifindex)
IFINDEX2=$(cat /sys/class/net/$IF2/ifindex)

MAC_A="50:a1:32:76:de:bb"
MAC_B="50:a1:32:76:e9:f9"
MAC_ENS7F0="b4:96:91:12:9d:44"
MAC_ENS7F1="b4:96:91:12:9d:46"

mac_to_hex() {
    echo $1 | tr ':' ' ' | awk '{print "0x"$1" 0x"$2" 0x"$3" 0x"$4" 0x"$5" 0x"$6}'
}

HEX_MAC_A=$(mac_to_hex $MAC_A)
HEX_MAC_B=$(mac_to_hex $MAC_B)
HEX_ENS7F0=$(mac_to_hex $MAC_ENS7F0)
HEX_ENS7F1=$(mac_to_hex $MAC_ENS7F1)

# Cleanup old pins
sudo ip link set dev $IF1 xdp off 2>/dev/null || true
sudo ip link set dev $IF2 xdp off 2>/dev/null || true
sudo rm -rf /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2 2>/dev/null || true

sudo mount -t bpf bpf /sys/fs/bpf 2>/dev/null || true
sudo mkdir -p /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2

echo "[+] Loading program 1 for $IF1..."
sudo bpftool prog load xdp_router.o /sys/fs/bpf/xdp_router_if1/prog type xdp pinmaps /sys/fs/bpf/xdp_router_if1

echo "[+] Loading program 2 for $IF2..."
sudo bpftool prog load xdp_router.o /sys/fs/bpf/xdp_router_if2/prog type xdp pinmaps /sys/fs/bpf/xdp_router_if2

echo "[+] Attaching programs to network interfaces..."
sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/xdp_router_if1/prog dev $IF1
sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/xdp_router_if2/prog dev $IF2

echo "[+] Populating Pinned Maps..."
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if1/tx_port key 0 0 0 0 value $IFINDEX2 0 0 0
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if1/mac_map key 0 0 0 0 value $HEX_MAC_B $HEX_ENS7F1

sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if2/tx_port key 0 0 0 0 value $IFINDEX1 0 0 0
sudo bpftool map update pinned /sys/fs/bpf/xdp_router_if2/mac_map key 0 0 0 0 value $HEX_MAC_A $HEX_ENS7F0

echo "[+] SUCCESS: XDP Router loaded, attached, and configured!"
```

**Note:** the two loads share the same compiled `xdp_router.o`, so each
interface gets its own independent copy of `tx_port`/`mac_map` (via
`pinmaps`), pinned separately under `xdp_router_if1/` and `xdp_router_if2/`.
The map entries cross-wire them: `if1`'s `tx_port` points at `IFINDEX2` and
its `mac_map` carries `IF2`'s peer MAC pair, and vice versa for `if2` —
that's what makes traffic arriving on one leg get redirected out the other
with rewritten MACs.

## 4. Verification

Run these after step 3 completes with no errors.

### 4.1 Confirm programs are attached

```bash
ip link show ens7f0
ip link show ens7f1
```

Expect `xdpgeneric/id:<N>` (or `xdp/id:<N>` if driver-mode offload is used)
in the link flags output.

```bash
sudo bpftool net show
```

Should list `xdp_router` attached to both `ens7f0` and `ens7f1` under the
`xdpgeneric` mode.

### 4.2 Confirm program + pins exist

```bash
sudo bpftool prog show pinned /sys/fs/bpf/xdp_router_if1/prog
sudo bpftool prog show pinned /sys/fs/bpf/xdp_router_if2/prog
```

Each should report `type xdp`, a valid `name xdp_router`, and non-zero
`run_cnt`/`xlated` size once traffic starts flowing.

### 4.3 Confirm map contents

```bash
sudo bpftool map dump pinned /sys/fs/bpf/xdp_router_if1/tx_port
sudo bpftool map dump pinned /sys/fs/bpf/xdp_router_if1/mac_map

sudo bpftool map dump pinned /sys/fs/bpf/xdp_router_if2/tx_port
sudo bpftool map dump pinned /sys/fs/bpf/xdp_router_if2/mac_map
```

Check that:
- `tx_port` key `0` value equals the **peer** interface's ifindex
  (`if1` → `IFINDEX2`, `if2` → `IFINDEX1`).
- `mac_map` key `0` value bytes match the expected `dst_mac`/`src_mac` pair
  for that leg.

### 4.4 Confirm ifindexes match what was programmed

```bash
cat /sys/class/net/ens7f0/ifindex
cat /sys/class/net/ens7f1/ifindex
```

Compare against the `IFINDEX1`/`IFINDEX2` values used when populating
`tx_port`.

### 4.5 Functional/traffic test

- Send IPv4 traffic into one interface (e.g. `ping`, or a crafted packet via
  `scapy`/`hping3`) and capture on the peer interface:

```bash
sudo tcpdump -i ens7f1 -e -n
```

  Confirm frames egress `ens7f1` with the rewritten source/dest MACs
  (`MAC_ENS7F1` as source, `MAC_B` as destination, per the `if1` map
  config), and vice versa for traffic entering `ens7f1`.

- Send ARP and confirm it's **not** redirected (`XDP_PASS` — should reach
  the normal kernel networking stack, e.g. visible to `tcpdump -i ens7f0`
  without MAC rewrite, or answerable by the host's ARP stack).

- Send a non-IP/non-ARP ethertype (e.g. IPv6) and confirm it's dropped
  (no egress on the peer interface, and `bpftool prog show` run stats
  increment while forwarded-packet counters on the peer don't).

### 4.6 Program-level counters (optional, needs stats enabled)

```bash
sudo bpftool prog load xdp_router.o ... # (only if reloading with stats)
sudo bpftool prog show name xdp_router --pretty
```

Or globally enable run-time stats before loading:

```bash
sudo sysctl -w kernel.bpf_stats_enabled=1
```

then re-check `run_cnt` / `run_time_ns` via `bpftool prog show`.

## 5. Teardown

```bash
IF1="ens7f0"
IF2="ens7f1"

# Detach XDP programs from both interfaces
sudo ip link set dev $IF1 xdpgeneric off
sudo ip link set dev $IF2 xdpgeneric off

# Remove pinned program/map directories
sudo rm -rf /sys/fs/bpf/xdp_router_if1 /sys/fs/bpf/xdp_router_if2


```

### 5.1 Verify teardown

```bash
ip link show ens7f0
ip link show ens7f1
```

Confirm no `xdp`/`xdpgeneric` id remains in the link flags.

```bash
sudo bpftool net show
```

Confirm `xdp_router` no longer appears for either interface.

```bash
ls /sys/fs/bpf/ | grep xdp_router
```

Confirm no output (pins removed).

```bash
sudo bpftool prog show name xdp_router
```

Confirm no matching program (nothing left holding a reference once both
the netdev attachment and the pins are gone).