# UDP Traffic Benchmarking Report: NVIDIA Jetson Orin ↔ Server

## Test Environment & Baseline Parameters
* **Traffic Flow:** Bidirectional performance testing using `iperf3` (UDP).
* **Optimization Flags:** `taskset` for CPU core affinity pinning and `-P` for multi-stream execution.
* **Baseline Performance (1500 MTU):** Capped at **5.5 Gbps** in both directions (Jetson → Server and Server → Jetson) using 8 parallel streams.
* **Baseline Command:** `taskset -c 0-7 iperf3 -c 192.168.60.3 -u -b 10G -l 1400 -P 8 -t 30`
* **Hardware MTU Capabilities:** Server NIC supports up to **9710** | Jetson Orin NIC supports up to **9000**

---

## Server → Jetson Traffic Results (Server MTU: 9710)

| Threads / Streams | Payload Size | Target Bandwidth | Achieved Rate | 
|---|---|---|---|
| 1 Thread | 9600 Bytes | 10 Gbps | **7.40 Gbps** | 
| 2 Threads | 9600 Bytes | 10 Gbps | **9.93 Gbps** | 
| 4 Threads | 9600 Bytes | 10 Gbps | **9.93 Gbps** | 
| 8 Threads | 9600 Bytes | 10 Gbps | **9.93 Gbps** |

---

## Jetson → Server Traffic Results (Jetson MTU: 9000)

| Threads / Streams | Payload Size | Target Bandwidth | Achieved Rate |
|---|---|---|---|
| 1 Thread | 1400 Bytes | 20 Gbps | **1.43 Gbps** | 
| 1 Thread | 8500 Bytes | 20 Gbps | **6.49 Gbps** | 
| 2 Threads | 8500 Bytes | 10 Gbps | **7.80 Gbps** | 
| 3 Threads | 8500 Bytes | 10 Gbps | **8.38 Gbps** | 
| 4 Threads | 8500 Bytes | 10 Gbps | **7.86 Gbps** | 
| 4 Threads | 8950 Bytes | 10 Gbps | **8.77 Gbps** | 
| 5 Threads | 8950 Bytes | 10 Gbps | **9.28 Gbps** | 
| 6 Threads | 8950 Bytes | 10 Gbps | **9.50 Gbps** |
| 8 Threads | 8950 Bytes | 10 Gbps | **9.74 Gbps** | 

---

## Resource Utilization & Key Findings
* **Jetson Core Allocation Constraint:** Although `taskset -c 0-7` requested 8 CPU cores on the Jetson Orin, `htop` observations confirmed that **only 2 cores were actively utilized** during traffic generation.
* **Server Core Utilization:** The server successfully distributed processing loads across all allocated cores as specified by `taskset`.
* **MTU & Multithreading Synergy:** Increasing payload sizes toward jumbo frame limits (~9000+ bytes) and scaling threads eliminated single-core packet processing bottlenecks, bringing both directions close to line rate (**9.93 Gbps** on Server, **9.74 Gbps** on Jetson).