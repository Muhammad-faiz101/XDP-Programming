# Network Stack Buffers & Packet Loss (Sender Side)

```
[Application Layer] 
       │ 
       ▼ 
 (Socket Send Buffer)
       │ 
       ▼ 
 [Transport Layer] (UDP)
       │ 
       ▼ 
  [Network Layer] 
       │ 
       ▼ 
      (qdisc)
       │ 
       ▼ 
 [Data Link Layer] (Driver & NIC)
```

---

## Buffer Congestion Mechanics

### 1. qdisc (Queuing Discipline) Buffer
Modern hardware supports multiple Tx queues, and Linux can assign multi-queue qdiscs to manage them efficiently.

**Causes of qdisc Overflows:**
* **Bandwidth Mismatch:** Sending rate exceeds hardware capacity (e.g., OS pushes data at 2 Gbps, but the NIC/link speed is limited to 1 Gbps).
* **CPU Starvation:** The CPU core assigned to process NIC transmit (`tx`) operations hits 100% utilization due to another process. The NIC stalls, causing the qdisc queue to back up.

### 2. Socket Send Buffer
**The Domino Effect:**
1. `qdisc` fills up due to hardware or CPU bottlenecks.
2. The **IP (Network) layer** stops pulling data from the **Transport layer**.
3. The **Transport layer** stops pulling data from the **Socket Send Buffer**.
4. The application continues calling the `sendto()` system call.
5. The **Socket Send Buffer** overflows, triggering send buffer errors.

> **Key Term:**  
> **Tx (Transmit):** The hardware capability of sending packets out of the network interface.

---

# Monitoring Buffer Statistics

## Analyzing UDP Send-Q (`ss -numa`)

Run `ss -numa` while your high-volume UDP application is pushing traffic to evaluate socket queue levels.

---

### Diagnostic Scenarios

* **Case A: Send-Q is 0 (or consistently low)**
  * **Meaning:** The kernel is keeping up with the application. The bottleneck is not at the socket layer.
  * **Next Step:** Check lower stack layers. Traffic is likely dropping at Traffic Control (`tc -s qdisc`) or the Driver Ring Buffer (`ip -s link`).

* **Case B: Send-Q climbs and remains high**
  * **Meaning:** The kernel's UDP layer or CPU is bottlenecked. The application is writing data into the socket buffer faster than the kernel can process it into IP packets.
  * **Risk:** When Send-Q hits the maximum `SO_SNDBUF` limit, the kernel drops incoming packets, registering as `send buffer errors` in `netstat -s -u`.
---
### Socket Send Buffer

* **Using `netstat`:**
  ```bash
  netstat -s -u
  ```
  *Shows total UDP packets sent (Application → IP) and send buffer errors (packets dropped because the outbound buffer was full).*

* **Using `nstat`:**
  ```bash
  nstat -az | grep -E "Udp(OutDatagram|Snd-bufErrors)"
  ```
  *Displays `UdpOutDatagrams` and `UdpSndbufErrors` counters.*

---

### qdisc Buffer

* **Check current queue status:**
  ```bash
  tc -s qdisc show dev <interface_name>
  ```
  *Look for the `limit` value (maximum number of queued packets) under the root qdisc.*

---

## Socket Send Buffer Configuration

### View Current Buffer Limits
```bash
# Check default send buffer size
sudo sysctl net.core.wmem_default

# Check maximum send buffer size
sudo sysctl net.core.wmem_max
```

### Increase Buffer Limits

* **Temporary Adjustment (until reboot):**
  ```bash
  sudo sysctl -w net.core.wmem_max=16777216
  ```

* **Permanent Adjustment:**  
  Edit `/etc/sysctl.conf`:
  ```bash
  sudo nano /etc/sysctl.conf
  ```
  Add or modify the following lines:
  ```ini
  net.core.wmem_default = 16777216
  net.core.wmem_max = 16777216
  ```
  Apply changes immediately:
  ```bash
  sudo sysctl -p
  ```

---

## qdisc Queue Length Configuration

### View & Modify (Temporary)

* **Check status:**
  ```bash
  tc -s qdisc show dev <interface_name>
  ```

* **Temporarily change qdisc limit:**
  ```bash
  sudo tc qdisc change dev <interface_name> root fq_codel limit 2000
  ```

---

### Permanent Configuration (Netplan)

Edit your Netplan configuration file (e.g., `/etc/netplan/01-netcfg.yaml`):

```yaml
network:
  version: 2
  renderer: networkd
  ethernets:
    <interface_name>:
      dhcp4: true
      transmit-queue-length: 2000
```

Apply the new Netplan configuration:
```bash
sudo netplan apply
```