# TRex Traffic Generator — Learning Notes

## 1. What is TRex?

TRex is a high-performance network traffic generator used to test network devices such as:

- Routers
- Switches
- Firewalls
- Load balancers
- Network appliances

### What problem does TRex solve?

The main problem is:

> How do we generate controlled, repeatable, high-rate network traffic so that we can test how a network device behaves under load?

TRex allows us to control things such as:

- Packet size
- Packet rate (PPS)
- Bandwidth (bps)
- Packet structure
- Number of streams
- Traffic duration
- CPU worker cores
- Stateless vs stateful traffic

Basic test setup:

```text
                    TRex
                      |
             Generate traffic
                      |
                 Port 0 / NIC
                      |
                      v
                    DUT
                      |
                      v
                 Port 1 / NIC
                      |
                    TRex
```

Where:

- **TRex** = traffic generator
- **DUT** = Device Under Test
- **NIC/Port 0 and Port 1** = interfaces used to send/receive traffic

---

# 2. STL vs ASTF

TRex has two important traffic-generation models:

```text
STL
 |
 +--> Stateless Traffic

ASTF
 |
 +--> Advanced Stateful Traffic
```

## STL — Stateless Traffic

STL stands for **Stateless Traffic**.

TRex generates packets according to predefined streams.

Conceptually:

```text
Packet -> Packet -> Packet -> Packet -> ...
```

The focus is mainly on **packet generation**, rather than maintaining application-level connection state.

Typical STL use cases:

- Packet forwarding tests
- Throughput testing
- PPS testing
- Packet-size testing
- Line-rate testing
- Protocol/header testing
- Network-device performance testing

Our profile:

```text
stl/1pkt_cmd.py
```

is an STL profile.

---

## ASTF — Advanced Stateful Traffic

ASTF stands for **Advanced Stateful Traffic**.

ASTF is designed for stateful client/server traffic, particularly TCP connections and application-like behavior.

Conceptually:

```text
Client
   |
   | TCP connection
   v
  DUT
   |
   v
Server
```

ASTF is useful for testing:

- TCP connection setup rate
- Concurrent connections
- Stateful traffic
- Client/server behavior
- Application-like traffic

### Simple mental model

```text
STL  -> packet/stream oriented
ASTF -> connection/state oriented
```

---

# 3. STL Profile Structure

Our STL profile contains a function such as:

```python
def get_streams(self, tunables, **kwargs):
```

It uses Python `argparse` to accept custom profile parameters.

For example:

```python
parser.add_argument(
    '--mode',
    type=int,
    default=0,
    choices=range(6),
    help='Packet structure: 0-5'
)

parser.add_argument(
    '--fsize',
    type=int,
    default=64,
    help='Packet size in bytes'
)
```

Then:

```python
args = parser.parse_args(tunables)

self.mode = args.mode
self.fsize = args.fsize

return [self.create_stream()]
```

The flow is:

```text
TRex command
     |
     v
-t --mode 0 --fsize 1400
     |
     v
get_streams(tunables)
     |
     v
argparse
     |
     +---- mode = 0
     |
     +---- fsize = 1400
     |
     v
create_stream()
     |
     v
STLStream
     |
     v
Traffic
```

Important:

> `--mode` and `--fsize` are arguments of the profile, not arguments of TRex's `start` command.

---

# 4. Passing STL Profile Tunables

TRex's `start` command contains:

```text
-t, --tunables ...
```

The important rule is:

> `-t` MUST be the last TRex-level flag.

Everything after `-t` is passed to the profile's `argparse`.

Correct:

```text
trex> start -f stl/1pkt_cmd.py -m 1gbps -t --mode 0 --fsize 1400
```

To see the profile-specific help:

```text
trex> start -f stl/1pkt_cmd.py -m 1gbps -t --help
```

This produced:

```text
usage: python3 -m trex.console.trex_console [-h] [--mode {0,1,2,3,4,5}] [--fsize FSIZE]

UDP packet traffic profile

options:
  -h, --help            show this help message and exit
  --mode {0,1,2,3,4,5}  Packet structure: 0-5
  --fsize FSIZE         Packet size in bytes
```

---

## Incorrect way 1

This failed:

```text
trex> start -f stl/1pkt_cmd.py -m 1gbps -- --mode 0 --fsize 1400
```

because `--` is not how TRex's `start` command passes profile tunables.

---

## Incorrect way 2

This also failed:

```text
trex> start -f stl/1pkt_cmd.py -m 1gbps -t "--mode 0 --fsize 1400"
```

The quotes became part of the argument.

As a result, the profile received something equivalent to:

```text
'1400"'
```

instead of:

```text
1400
```

and `argparse` could not convert it to an integer.

Use:

```text
-t --mode 0 --fsize 1400
```

without wrapping the entire tunable string in quotes.

---

# 5. `STLStream`

An STL profile creates one or more `STLStream` objects.

A simplified stream looks like:

```python
stream = STLStream(
    packet=STLPktBuilder(pkt=pkt),
    mode=STLTXCont(pps=1000)
)
```

An `STLStream` contains things such as:

```text
STLStream
   |
   +-- Packet definition
   |
   +-- Transmission mode
   |
   +-- Rate
   |
   +-- Stream behavior
```

The important part for rate control is:

```python
STLTXCont(...)
```

---

# 6. Per-Stream Rate

The transmission rate can be defined inside the stream's transmission mode.

For example:

```python
STLTXCont(pps=10000)
```

means:

> This stream should transmit at 10,000 packets per second.

Other supported rate concepts include:

```python
STLTXCont(pps=10000)
```

```python
STLTXCont(bps_L2=1_000_000)
```

```python
STLTXCont(bps_L1=1_000_000)
```

```python
STLTXCont(percentage=10)
```

So conceptually:

```text
STLStream
    |
    +--> STLTXCont
             |
             +--> pps
             +--> bps_L2
             +--> bps_L1
             +--> percentage
```

---

# 7. `pps` vs `bps_L2` vs `bps_L1`

The easiest mental model is:

```text
pps
 |
 +--> "How many packets per second?"

bps_L2
 |
 +--> "How many bits/sec are in the Ethernet frames?"

bps_L1
 |
 +--> "How much physical wire capacity is being consumed?"
```

---

## 7.1 PPS

Example:

```python
STLTXCont(pps=100000)
```

means:

```text
100,000 packets/sec
```

The requested packet rate stays 100,000 PPS regardless of packet size.

However, the resulting bandwidth changes with packet size.

For example:

```text
64-byte packet  × 100k PPS -> lower bandwidth
1400-byte packet × 100k PPS -> much higher bandwidth
```

### Useful for

PPS is especially useful when testing:

- Packet-processing capacity
- Forwarding performance
- CPU-bound packet processing
- Packets-per-second limits

---

# 8. `bps_L2`

Example:

```python
STLTXCont(bps_L2=1_000_000_000)
```

means approximately:

```text
1 Gbps at Layer 2
```

Here packet size matters.

Suppose the packet size is 1000 bytes.

```text
1000 bytes × 8
= 8000 bits
```

To produce approximately 1 Gbps:

```text
1,000,000,000 / 8000
= 125,000 packets/sec
```

So:

```text
bps_L2
   |
   +--> desired bandwidth
            |
            v
      packet size
            |
            v
       required PPS
```

---

# 9. `bps_L1`

`bps_L1` represents the physical transmission rate.

Conceptually:

```text
                 Layer 1
        +-------------------------+
        | Preamble + SFD          |
        | Ethernet frame          |
        | FCS                     |
        | Inter-packet gap        |
        +-------------------------+
```

Therefore:

```text
bps_L1 > bps_L2
```

for the same traffic.

The difference is particularly important with small Ethernet packets because fixed physical overhead becomes a larger percentage of each transmission.

---

# 10. Example: 1400-byte Packet

Suppose:

```text
Packet size = 1400 bytes
PPS         = 100,000
```

Simplified bandwidth:

```text
1400 bytes × 8
= 11,200 bits
```

Then:

```text
11,200 × 100,000
= 1,120,000,000 bits/sec
```

Therefore:

```text
≈ 1.12 Gbps
```

This is a simplified calculation and does not include all physical-layer overhead.

---

# 11. Why Packet Size Matters

At the same PPS:

```text
64-byte packet
    |
    +--> lower bandwidth

1400-byte packet
    |
    +--> much higher bandwidth
```

Example:

```text
100,000 PPS

64-byte packets
    -> approximately 51.2 Mbps of frame data

1400-byte packets
    -> approximately 1.12 Gbps of frame data
```

Therefore:

> PPS and bandwidth are directly related through packet size.

---

# 12. Multiple STL Streams

An STL profile can return multiple streams.

Example:

```python
stream1 = STLStream(
    packet=STLPktBuilder(pkt=pkt1),
    mode=STLTXCont(pps=10000)
)

stream2 = STLStream(
    packet=STLPktBuilder(pkt=pkt2),
    mode=STLTXCont(pps=50000)
)

stream3 = STLStream(
    packet=STLPktBuilder(pkt=pkt3),
    mode=STLTXCont(bps_L2=1_000_000)
)

return [stream1, stream2, stream3]
```

Conceptually:

```text
                       TRex
                         |
          +--------------+--------------+
          |              |              |
       Stream 1       Stream 2       Stream 3
       10k PPS        50k PPS         1 Mbps
```

Each stream can have:

- Its own packet
- Its own packet size
- Its own rate
- Its own stream behavior

---

# 13. Stream Rate vs `start -m`

This is an important distinction.

Inside the profile:

```python
STLTXCont(pps=100000)
```

defines the stream's transmission mode/rate.

At the TRex console:

```text
-m 5gbps
```

controls the runtime traffic multiplier/rate.

Conceptually:

```text
                         TRex
                          |
                    start -m 5gbps
                          |
                  runtime rate control
                          |
             +------------+------------+
             |            |            |
          Stream 1     Stream 2     Stream 3
             |            |            |
          own rate      own rate     own rate
```

Therefore:

```text
STLTXCont(...)
```

and:

```text
start -m ...
```

are not the same concept.

When troubleshooting a traffic rate, always identify whether the rate is coming from:

```text
1. Profile / STLTXCont
```

or:

```text
2. TRex start command / -m
```

---

# 14. TRex NIC / DPDK Setup

The system has two Intel X540-AT2 ports:

```text
0000:d8:00.0  Intel X540-AT2
0000:d8:00.1  Intel X540-AT2
```

Both are using:

```text
drv=vfio-pci
```

This means the interfaces are bound to the DPDK-compatible `vfio-pci` driver.

Conceptually:

```text
TRex Port 0 -> d8:00.0
TRex Port 1 -> d8:00.1
```

---

# 15. NIC NUMA Placement

We checked the NUMA node for the NICs.

Both ports returned:

```text
1
```

Therefore:

```text
d8:00.0 -> NUMA node 1
d8:00.1 -> NUMA node 1
```

For high-performance traffic generation, it is generally desirable to use CPU workers on the same NUMA node as the NIC.

Conceptually:

```text
NUMA node 1
   |
   +-- NIC
   |
   +-- TRex worker CPUs
```

This reduces the need for traffic/data to cross NUMA nodes.

---

# 16. TRex CPU Configuration

The current `/etc/trex_cfg.yaml` contains:

```yaml
platform:
    master_thread_id: 0
    latency_thread_id: 1
    dual_if:
      - socket: 1
        threads: [10,11,12,13,14,15,16,17,18,19,30,31,32,33,34,35,36,37,38,39]
```

This means:

```text
Master thread
    -> CPU 0

Latency thread
    -> CPU 1

Worker pool on NUMA socket 1
    -> CPUs 10-19
    -> CPUs 30-39
```

There are therefore:

```text
20 configured worker CPU IDs
```

However:

> Configuring 20 worker threads does NOT mean every test automatically uses all 20 cores.

The actual number of active workers depends on the traffic configuration and TRex's worker allocation.

---

# 17. `--core_mask`

TRex provides:

```text
--core_mask
```

to explicitly select worker CPU cores.

A CPU mask uses one bit per CPU ID.

Conceptually:

```text
mask = 1 << CPU_ID
```

For CPU 11:

```text
1 << 11
= 2048
= 0x800
```

For CPU 12:

```text
1 << 12
= 4096
= 0x1000
```

Therefore:

```text
CPU 11 -> 0x800
CPU 12 -> 0x1000
```

---

# 18. Important Correction About Core Masks

Do not assume that:

```text
0x800 0x1000
```

necessarily means:

```text
Port 0 -> CPU 11
Port 1 -> CPU 12
```

The exact interpretation of `--core_mask` depends on the TRex version and command semantics.

The important distinction is:

```text
core_mask
    |
    +--> tells TRex which worker CPU bits are allowed
```

while:

```text
-p
    |
    +--> selects ports/profiles
```

and:

```text
--pin
    |
    +--> controls CPU/interface pinning
```

Always verify the actual worker-to-port assignment using TRex statistics/CPU information rather than assuming the mapping from the mask alone.

---

# 19. `--pin`

TRex also supports:

```text
--pin
```

The purpose is to pin/divide workers between interfaces.

Conceptually:

```text
Configured worker pool
          |
          v
       --pin
          |
     +----+----+
     |         |
   Port 0    Port 1
```

Important:

> `--pin` does NOT mean "use exactly two cores."

Instead, it tells TRex to perform interface/worker pinning using its configured worker resources.

Therefore:

```text
--core_mask
    |
    +--> Explicit CPU selection

--pin
    |
    +--> Automatic worker/interface pinning
```

These solve different problems.

---

# 20. Configured Cores vs Actually Busy Cores

A major lesson from the testing was:

> Seeing only one busy CPU in `htop` does not automatically mean TRex is configured to use only one CPU.

There is a difference between:

```text
Configured / available workers
```

and:

```text
Workers actually doing significant work
```

For example:

```text
Worker pool:
    CPU 11
    CPU 12
```

does not guarantee:

```text
CPU 11 -> 50% workload
CPU 12 -> 50% workload
```

The actual workload depends on:

- Number of ports
- Number of streams
- Traffic rate
- Packet size
- Worker allocation
- Profile structure
- TRex configuration

---

# 21. One Stream vs Multiple Cores

This is especially important.

Suppose:

```text
--core_mask
```

allows two worker CPUs.

That does not automatically mean:

```text
one stream
    |
    +--> split equally between two CPUs
```

A better mental model is:

```text
Available workers
       |
       v
TRex worker allocation
       |
       v
Traffic workload
       |
       +--> may require/use multiple workers
```

If the workload can be handled by one worker, another worker may remain mostly idle.

---

# 22. Two Ports and Two Workers

A situation like this can naturally result in two active workers:

```text
Port 0
   |
   v
Worker CPU A
   |
   v
Traffic


Port 1
   |
   v
Worker CPU B
   |
   v
Traffic
```

But if only one port is generating traffic:

```text
Port 0 -> traffic
Port 1 -> no traffic
```

then seeing:

```text
Worker A -> busy
Worker B -> mostly idle
```

can be completely normal.

---

# 23. Checking Traffic

During a test, use:

```text
trex> stats -c
```

Look at:

- Port 0 TX
- Port 1 TX
- RX
- PPS
- Bandwidth
- Drops
- CPU utilization

Do not rely only on `htop`.

A better troubleshooting process is:

```text
1. Is Port 0 transmitting?
2. Is Port 1 transmitting?
3. What is the actual PPS?
4. What is the actual bandwidth?
5. What is TRex CPU utilization?
6. Which worker cores are active?
```

---

# 24. Check CPU Topology

We have not yet established whether CPUs:

```text
10-19
30-39
```

are physical cores or SMT/hyperthread siblings.

Use:

```bash
lscpu -e=CPU,CORE,SOCKET,NODE
```

Example:

```text
CPU   CORE   SOCKET   NODE
10    10     1        1
11    11     1        1
...
30    10     1        1
31    11     1        1
...
```

If:

```text
CPU 11 -> CORE 11
CPU 31 -> CORE 11
```

then:

```text
11 and 31
```

are two logical CPUs belonging to the same physical core.

That means they are SMT siblings.

For performance testing, two separate physical cores generally provide more execution resources than two SMT siblings.

---

# 25. Useful Commands

## Check DPDK/NIC binding

```bash
./dpdk_setup_ports.py -s
```

Look for:

```text
drv=vfio-pci
```

---

## Check NIC NUMA node

```bash
cat /sys/bus/pci/devices/0000:d8:00.0/numa_node
cat /sys/bus/pci/devices/0000:d8:00.1/numa_node
```

Expected from our setup:

```text
1
1
```

---

## Check CPU topology

```bash
lscpu -e=CPU,CORE,SOCKET,NODE
```

---

## Check TRex platform configuration

```bash
grep -A 20 platform /etc/trex_cfg.yaml
```

---

## Get `start` help

```text
trex> help start
```

---

## Get STL profile arguments

```text
trex> start -f stl/1pkt_cmd.py -m 1gbps -t --help
```

---

## Check traffic statistics

```text
trex> stats -c
```

---

# 26. Basic Test Workflow

A useful way to think about a TRex test is:

```text
                1. Configure NIC
                         |
                         v
                2. Configure TRex
                         |
                         v
                3. Create STL profile
                         |
                         v
                4. Define packet
                         |
                         v
                5. Define STLStream
                         |
                         v
                6. Define stream rate
                         |
                         v
                7. Start traffic
                         |
                         v
                8. Observe TX/RX
                         |
                         v
                9. Observe CPU
                         |
                         v
                10. Analyze DUT
```

---

# 27. Example STL Profile Concept

A simple profile can look like:

```python
from trex.stl.api import *


class Prof1:

    def create_stream(self):

        pkt = Ether() / IP(
            src="10.0.0.1",
            dst="10.0.0.2"
        ) / UDP(
            sport=1234,
            dport=5678
        )

        return STLStream(
            packet=STLPktBuilder(pkt=pkt),
            mode=STLTXCont(pps=10000)
        )

    def get_streams(self, tunables, **kwargs):

        return [self.create_stream()]


def register():
    return Prof1()
```

The important chain is:

```text
get_streams()
      |
      v
create_stream()
      |
      v
STLStream()
      |
      +--> packet
      |
      +--> STLTXCont()
                 |
                 +--> rate
```

---

# 28. Main Mental Model

Keep these layers separate:

```text
                         TRex
                          |
          +---------------+---------------+
          |                               |
     Traffic definition              CPU allocation
          |                               |
      STLStream                     core_mask / pin
          |
      STLTXCont
          |
      Rate definition
          |
      +---+---+---+
      |       |   |
     PPS   bps_L2 bps_L1
```

And:

```text
STLStream
    |
    +--> Packet definition
    |
    +--> Transmission mode
    |
    +--> Rate
    |
    +--> Stream behavior
```

While:

```text
start -m
    |
    +--> Runtime traffic rate/multiplier
```

And:

```text
core_mask
    |
    +--> Worker CPU selection
```

And:

```text
--pin
    |
    +--> Worker/interface pinning
```

---

# 29. Key Lessons

1. **TRex is a high-performance network traffic generator.**

2. **The main purpose of TRex is to generate controlled and repeatable traffic for testing network devices.**

3. **STL is packet/stream oriented.**

4. **ASTF is state/connection oriented.**

5. **`STLStream` describes a traffic stream.**

6. **`STLTXCont(...)` defines the continuous transmission mode and rate of that stream.**

7. **`pps` means packets per second.**

8. **`bps_L2` means Layer-2 bandwidth.**

9. **`bps_L1` represents physical wire rate and includes physical transmission overhead.**

10. **PPS and bandwidth are related through packet size.**

11. **`-t` passes custom arguments from the TRex `start` command to the STL profile.**

12. **Profile arguments must come after `-t`.**

13. **Do not use `--` to separate STL profile arguments in this command.**

14. **Do not wrap the entire `-t` argument list in quotes in the TRex console.**

15. **`-m 5gbps` and `STLTXCont(...)` are different levels of rate control.**

16. **The system has two Intel X540-AT2 interfaces:**
    ```text
    d8:00.0
    d8:00.1
    ```

17. **Both NICs are using `vfio-pci`.**

18. **Both NICs are on NUMA node 1.**

19. **The configured worker CPU pool on NUMA node 1 is:**
    ```text
    10-19
    30-39
    ```

20. **The TRex master thread is configured on CPU 0.**

21. **The TRex latency thread is configured on CPU 1.**

22. **Configuring many worker CPUs does not mean all of them will be busy during every test.**

23. **`--core_mask` is used to restrict/select worker CPU cores.**

24. **`--pin` is used for worker/interface pinning and does not simply mean "use two cores."**

25. **Two available worker CPUs do not automatically mean one stream will be split across both CPUs.**

26. **`htop` alone is not enough to determine whether TRex is correctly using its workers.**

27. **Check TRex traffic statistics and CPU utilization together.**

28. **Use `lscpu -e=CPU,CORE,SOCKET,NODE` to understand physical cores, SMT siblings, sockets, and NUMA nodes.**

---
## Commands:
 * sudo ./t-rex-64 -i --no-scapy
    - starting trex server in interactive mode

 * on other terminal run 
    -  ./trex-console

* without CLI args : start -f profile.py -m 1gbps

* with CLI args : start -f profile.py -m 1gbps -t --mode 0 --fsize 1400