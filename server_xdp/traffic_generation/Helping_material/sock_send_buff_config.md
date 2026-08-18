# Tuning Linux Global Send Socket Buffer Limits (`wmem`)

When a high-volume UDP application experiences packet loss due to socket send buffer overflows, tuning kernel parameters provides the memory headroom required to absorb transient traffic bursts.

---

## 1. Calculating Required Buffer Limits

Buffer sizing should align with your network link capacity and Latency/Round-Trip Time (RTT) using the **Bandwidth-Delay Product (BDP)** formula:

$$\text{BDP (bytes)} = \frac{\text{Bandwidth (bits/sec)} \times \text{RTT (seconds)}}{8}$$

### Example Calculation
For a **10 Gbps link** with a **20 ms (0.02 s) RTT**:
$$\text{BDP} = \frac{10,000,000,000 \times 0.02}{8} = 25,000,000 \text{ bytes } (\approx 25 \text{ MB})$$

> **Kernel Overhead Rule:** Linux automatically doubles requested buffer sizes to account for internal `sk_buff` struct metadata overhead. To prevent silent capping, ensure `net.core.wmem_max` is set to **at least $2 \times \text{BDP}$**.

---

## 2. Global Kernel Parameter Configuration

Two primary kernel parameters control outgoing socket buffer limits:
* `net.core.wmem_max`: The absolute hard limit across all unprivileged processes.
* `net.core.wmem_default`: The default buffer size allocated to newly created sockets.

### A. Runtime Configuration (Temporary)
Apply changes dynamically without rebooting:

```bash
# Increase maximum send buffer limit (32 MB)
sudo sysctl -w net.core.wmem_max=33554432

# Increase default send buffer size (16 MB)
sudo sysctl -w net.core.wmem_default=16777216
```

### B. Persistent Configuration
To ensure settings persist across system reboots, append them to the sysctl configuration file:

1. Open `/etc/sysctl.conf` (or create `/etc/sysctl.d/99-network-buffers.conf`):
   ```bash
   sudo nano /etc/sysctl.conf
   ```

2. Add the target values:
   ```ini
   net.core.wmem_max = 33554432
   net.core.wmem_default = 16777216
   ```

3. Reload settings into the live kernel:
   ```bash
   sudo sysctl -p
   ```

---

## 3. Verification & Diagnostic Commands

### Confirm Active Kernel Parameters
```bash
sysctl net.core.wmem_max net.core.wmem_default
```

### Inspect Active Socket Buffer Allocations
```bash
ss -numa
```

### Monitor Drop Counters
Verify that socket-level drops stop incrementing under high workload:
```bash
nstat -az | grep -E "Udp(OutDatagram|Snd-bufErrors)"
```

---

## Key Operational Notes

* **Bufferbloat & Latency:** While expanding send buffers prevents immediate drops during bursts, sustained downstream congestion combined with large buffers introduces latency/jitter.
* **Bottleneck Shifting:** Eliminating socket drops (`UdpSndbufErrors`) may shift packet loss further down the network stack. Monitor queue discipline (`tc -s qdisc`) and driver ring buffers (`ip -s link`) if loss persists.