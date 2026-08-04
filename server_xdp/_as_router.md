# Cross-Subnet Linux Router Setup
No xdp server involved 
This guide details the configuration required to turn a Linux machine (Laptop B) into a router that shares its Wi-Fi internet connection over an Ethernet cable to a client machine (Laptop A) operating on a completely different IP subnet.

This setup is fully compatible with a live USB boot environment, meaning it relies on immediate, session-based configurations (`netplan`, `sysctl`, `iptables`) rather than persistent configuration files.

## Architecture & Packet Flow

* **Laptop A (Client):** Connected via Ethernet (`192.168.50.2/24`).
* **Laptop B (Router):** Connected via Ethernet to A (`192.168.60.2/24`) and via Wi-Fi to the Internet.

**How it works:**
1. Laptop A sends internet-bound traffic out its Ethernet port to Laptop B.
2. Laptop B receives the traffic, recognizes it is bound for the external network, and forwards it internally to its Wi-Fi interface.
3. Laptop B applies NAT (Network Address Translation/Masquerading) to make the traffic look like it originated from its own Wi-Fi IP address.
4. When the response returns from the internet, Laptop B reverses the NAT and forwards the response back down the Ethernet cable to Laptop A.

---

## Phase 1: Address Configuration (Netplan)

Ubuntu live environments primarily use `NetworkManager`. We must explicitly define `renderer: NetworkManager` to prevent crashes when applying configurations.

### 1. Laptop B (Router) Configuration
Save this file at `/etc/netplan/01-netcfg.yaml` (or edit the existing `.yaml` file). 

*Note: We purposefully omit a `default` gateway here so Laptop B relies entirely on Wi-Fi for its internet connection. The `scope: link` rule forces Laptop B to route `.50.x` traffic directly over the physical cable rather than sending it to the Wi-Fi router.*

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      match:
        macaddress: "50:a1:32:76:e9:f9"
      set-name: enp2s0
      addresses: 
        - 192.168.60.2/24
      routes:
        - to: 192.168.50.0/24
          scope: link
```

### 2. Laptop A (Client) Configuration
Save this file at `/etc/netplan/01-netcfg.yaml`.

*Note: The `on-link: true` directive is mandatory for cross-subnet routing. It bypasses the kernel's default security checks and allows Laptop A to use a gateway (`192.168.60.2`) that is outside its own subnet (`192.168.50.x`).*

```yaml
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp2s0:
      match:
        macaddress: "50:a1:32:76:de:bb"
      set-name: enp2s0
      addresses: 
        - 192.168.50.2/24
      routes:
        - to: default
          via: 192.168.60.2
          on-link: true
      nameservers:
        addresses: [8.8.8.8, 1.1.1.1]
```

**Apply the network settings on both laptops:**
```bash
sudo netplan apply
```

---

## Phase 2: Enable IP Forwarding

By default, the Linux kernel drops packets not destined for its own IP. We must toggle a kernel parameter on **Laptop B** to allow it to route traffic between interfaces.

```bash
sudo sysctl -w net.ipv4.ip_forward=1
```

---

## Phase 3: NAT & Firewall Configuration (iptables)

The external internet does not know how to route traffic back to the private `192.168.50.x` network. **Laptop B** must translate the traffic using `iptables`.

Run the following commands on **Laptop B**:

**1. Masquerade Outbound Traffic:**
Intercepts packets leaving the Wi-Fi interface (`wlp1s0`) and replaces Laptop A's source IP with Laptop B's Wi-Fi IP.
```bash
sudo iptables -t nat -A POSTROUTING -o wlp1s0 -j MASQUERADE
```

**2. Allow Outbound Forwarding:**
Grants explicit permission for packets to traverse from the Ethernet interface (`enp2s0`) to the Wi-Fi interface (`wlp1s0`).
```bash
sudo iptables -A FORWARD -i enp2s0 -o wlp1s0 -j ACCEPT
```

**3. Allow Inbound Replies:**
Allows returning traffic (like ping replies from Google) to enter the Wi-Fi interface and flow back into the Ethernet interface, but only for connections that Laptop A previously initiated.
```bash
sudo iptables -A FORWARD -i wlp1s0 -o enp2s0 -m state --state RELATED,ESTABLISHED -j ACCEPT
```

---

## Phase 4: Verification

To confirm the routing chain is successful, run the following commands on **Laptop A**:

1. **Test local link:** `ping 192.168.60.2`
2. **Test external routing:** `ping 8.8.8.8`
3. **Test DNS resolution:** `ping google.com`

