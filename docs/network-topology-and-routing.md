# Linux Networking Lab: Building a Routed Topology for XDP

## Objective

Build a simple routed network consisting of:

- **Laptop A**
- **Ubuntu Server**
- **Laptop B**

where all traffic between the two laptops passes through the Ubuntu server. This topology serves as the foundation for learning Linux networking, routing, packet forwarding, and later, XDP/eBPF.

---

# Network Topology

```
                   Ubuntu Server

          ens7f0                   ens7f1
      192.168.50.1             192.168.60.1
            │                        │
            │                        │
            │                        │
      Laptop A                  Laptop B
   192.168.50.2             192.168.60.2
```

---

# Learning Objectives

By completing this lab, you will learn:

- Linux network interfaces
- Static IP addressing
- Netplan configuration
- Routing tables
- Default gateways
- Route metrics
- IP forwarding
- ARP (Address Resolution Protocol)
- Packet capturing using tcpdump
- Basic network troubleshooting

---

# Prerequisites

- Ubuntu installed on all machines
- Ethernet cables connected
- sudo privileges
- Netplan installed (default on Ubuntu)
- tcpdump installed

Install tcpdump if needed:

```bash
sudo apt update
sudo apt install tcpdump
```

---

# Step 1 — Discover Available Network Interfaces

List all interfaces.

```bash
ip link
```

Purpose:

- Shows all network interfaces.
- Indicates whether interfaces are UP or DOWN.
- Verifies physical cable connectivity.

Example output:

```
ens7f0  UP LOWER_UP
ens7f1  UP LOWER_UP
```

Important states:

| State | Meaning |
|--------|---------|
| UP | Interface enabled |
| LOWER_UP | Physical cable connected |
| NO-CARRIER | Cable disconnected |
| DOWN | Interface disabled |

---

# Step 2 — View IP Addresses

```bash
ip addr
```

Purpose:

Displays

- interface names
- IPv4 addresses
- IPv6 addresses
- subnet masks

Example:

```
ens7f0
192.168.50.1/24

ens7f1
192.168.60.1/24
```

To inspect a single interface:

```bash
ip addr show ens7f0
```

---

# Step 3 — Configure Static IP Addresses

Ubuntu uses **Netplan**.

List Netplan configuration:

```bash
ls /etc/netplan
```

Edit configuration:

```bash
sudo nano /etc/netplan/<file>.yaml
```

Example server configuration:

```yaml
network:
  version: 2

  ethernets:

    ens7f0:
      dhcp4: false
      addresses:
        - 192.168.50.1/24

    ens7f1:
      dhcp4: false
      addresses:
        - 192.168.60.1/24
```

Test safely:

```bash
sudo netplan try
```

Apply permanently:

```bash
sudo netplan apply
```

Verify:

```bash
hostname -I
```

or

```bash
ip addr
```

---

# Step 4 — Verify Connectivity

From Laptop A

```bash
ping 192.168.50.1
```

Purpose:

Verify Laptop A can reach the server.

---

From Laptop B

```bash
ping 192.168.60.1
```

Purpose:

Verify Laptop B can reach the server.

---

From the server

```bash
ping 192.168.50.2

ping 192.168.60.2
```

Purpose:

Verify the server can reach both laptops.

---

# Step 5 — View the Routing Table

```bash
ip route
```

Purpose:

Displays Linux's routing table.

Example:

```
192.168.50.0/24 dev ens7f0

192.168.60.0/24 dev ens7f1

default via 10.1.78.1 dev wlp1s0
```

Concept:

The routing table tells Linux **where to send packets**.

---

# Step 6 — See Which Route Linux Chooses

```bash
ip route get <destination-IP>
```

Example:

```bash
ip route get 192.168.60.2
```

Purpose:

Displays

- outgoing interface
- gateway
- source IP

Example:

```
192.168.60.2 via 192.168.50.1 dev enp2s0
```

This command is one of the most useful routing debugging tools.

---

# Step 7 — Enable IP Forwarding

Linux does not forward packets by default.

Check status:

```bash
cat /proc/sys/net/ipv4/ip_forward
```

Output:

```
0
```

means disabled.

```
1
```

means enabled.

Enable temporarily:

```bash
sudo sysctl -w net.ipv4.ip_forward=1
```

Disable temporarily:

```bash
sudo sysctl -w net.ipv4.ip_forward=0
```

Permanent configuration:

```bash
sudo nano /etc/sysctl.conf
```

Set

```
net.ipv4.ip_forward=1
```

Reload:

```bash
sudo sysctl -p
```

Purpose:

Turns the Ubuntu server into a router.

---

# Step 8 — Inspect the ARP Table

```bash
ip neigh
```

Purpose:

Shows IP-to-MAC mappings.

Example:

```
192.168.50.2

↓

50:a1:32:76:de:bb
```

Useful for verifying Ethernet communication.

---

# Step 9 — Capture Network Traffic

Capture everything:

```bash
sudo tcpdump -i ens7f0 -n
```

or

```bash
sudo tcpdump -i ens7f1 -n
```

Capture only ICMP:

```bash
sudo tcpdump -i ens7f0 icmp
```

Purpose:

Observe packets entering and leaving interfaces.

Useful for

- ARP
- ICMP
- TCP
- UDP

---

# Step 10 — Troubleshooting the Lab

Initially,

Laptop A could not ping Laptop B.

We verified:

- IP forwarding
- Routing table
- Interfaces
- ARP

Everything looked correct.

However,

```bash
ip route get 192.168.50.2
```

returned

```
via 10.1.78.1 dev wlp1s0
```

instead of

```
via 192.168.60.1 dev enp2s0
```

This showed Linux was sending packets over **Wi-Fi** instead of **Ethernet**.

---

# Why?

The routing table contained two default routes.

Example:

```
default via Wi-Fi
metric 600

default via Ethernet
metric 20100
```

Linux chooses the route with the **lowest metric**.

Therefore,

Wi-Fi was preferred.

---

# Temporary Verification

Disconnect Wi-Fi.

Immediately,

communication between Laptop A and Laptop B started working.

This confirmed that routing was the issue.

---

# Better Solution

Keep Wi-Fi for Internet access.

Add a route for the opposite subnet.

Laptop A

```bash
sudo ip route add 192.168.60.0/24 via 192.168.50.1 dev enp2s0
```

Laptop B

```bash
sudo ip route add 192.168.50.0/24 via 192.168.60.1 dev enp2s0
```

Now

- Internet uses Wi-Fi.
- Lab traffic uses Ethernet.

---

# Useful Commands Summary

## Interfaces

```bash
ip link
```

---

## IP addresses

```bash
ip addr
```

---

## All assigned IPv4 addresses

```bash
hostname -I
```

---

## Routing table

```bash
ip route
```

---

## Route lookup

```bash
ip route get <IP>
```

---

## Connectivity

```bash
ping <IP>
```

---

## ARP table

```bash
ip neigh
```

---

## Enable forwarding

```bash
sudo sysctl -w net.ipv4.ip_forward=1
```

---

## Disable forwarding

```bash
sudo sysctl -w net.ipv4.ip_forward=0
```

---

## Packet capture

```bash
sudo tcpdump -i <interface> -n
```

---

## Temporary static route

```bash
sudo ip route add <network> via <gateway> dev <interface>
```

---

## Netplan

```bash
sudo netplan try
```

```bash
sudo netplan apply
```

---

# Final Network Configuration

| Device | Interface | IP Address |
|----------|-----------|------------|
| Server | ens7f0 | 192.168.50.1/24 |
| Server | ens7f1 | 192.168.60.1/24 |
| Laptop A | Ethernet | 192.168.50.2/24 |
| Laptop B | Ethernet | 192.168.60.2/24 |

---

# Packet Flow

```
Laptop A
192.168.50.2
        │
        ▼
Server
192.168.50.1
        │
Linux Routing
(IP Forwarding)
        │
        ▼
Server
192.168.60.1
        │
        ▼
Laptop B
192.168.60.2
```

The IP destination remains `192.168.60.2` throughout the journey. At each hop, only the Ethernet (Layer 2) header changes to target the next device's MAC address.

---

# Next Step

This networking lab provides the foundation for XDP.

In the next phase, an XDP program will be attached to `ens7f0` or `ens7f1` so packets can be inspected, modified, redirected, or dropped **before** they enter the normal Linux networking stack.