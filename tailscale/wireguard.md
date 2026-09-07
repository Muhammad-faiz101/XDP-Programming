# WireGuard VPN + SSH

## General Step-by-Step Setup Guide

This guide explains how to configure a **WireGuard VPN between a server and a client**, and then use **SSH through the VPN** to securely access the server.

The guide is written for someone who is setting up WireGuard for the first time.

---

## 1. What Are We Building?

We have two machines:

```text
+----------------+                         +----------------+
|                |                         |                |
|     CLIENT     |                         |     SERVER     |
|                |                         |                |
| Physical NIC   |                         | Physical NIC   |
|                |                         |                |
| WireGuard wg0  |=========================| WireGuard wg0  |
| 10.50.0.2      |   Encrypted VPN Tunnel  | 10.50.0.1      |
|                |                         |                |
+----------------+                         +----------------+
                                                    |
                                                    |
                                                SSH TCP/22
```

The client connects to the server using WireGuard.

After the VPN is established, SSH is used through the WireGuard IP.

For example:

```text
Client
   |
   | WireGuard VPN
   |
   v
Server VPN IP
   |
   | SSH
   |
   v
Server shell
```

---

## 2. What Are WireGuard and SSH?

## WireGuard

WireGuard is a VPN protocol.

It creates a virtual network interface, usually called:

```text
wg0
```

This interface behaves somewhat like a normal network interface, but traffic sent through it is encrypted.

WireGuard provides:

* Encryption
* Peer authentication
* Secure tunneling
* Virtual IP addresses
* Routing between VPN peers

---

## SSH

SSH stands for:

```text
Secure Shell
```

SSH allows a user to remotely log in to another machine.

Example:

```bash
ssh username@SERVER_IP
```

SSH provides:

* Remote shell access
* User authentication
* Encrypted communication
* Remote administration

---

## 3. Why Use WireGuard + SSH?

SSH by itself protects the connection, but the SSH service may still be reachable from a large network.

With WireGuard:

```text
Client
   |
   | VPN authentication + encryption
   |
   v
WireGuard network
   |
   | SSH authentication
   |
   v
Server
```

This gives us two separate layers:

```text
Layer 1:
WireGuard
    |
    +-- Is this an authorized VPN peer?

Layer 2:
SSH
    |
    +-- Is this an authorized user?
```

WireGuard authenticates the **device/peer**.

SSH authenticates the **user**.

---

## 4. Important Terms

Before starting, understand these terms.

| Term        | Meaning                                               |
| ----------- | ----------------------------------------------------- |
| Server      | Machine providing the VPN/SSH service                 |
| Client      | Machine connecting to the server                      |
| Peer        | A WireGuard participant                               |
| wg0         | WireGuard virtual network interface                   |
| Private Key | Secret cryptographic key                              |
| Public Key  | Key shared with the other peer                        |
| Endpoint    | Physical address where WireGuard packets are sent     |
| AllowedIPs  | IP addresses associated with a peer/routed through it |
| ListenPort  | UDP port on which WireGuard listens                   |
| VPN IP      | IP address assigned to the WireGuard interface        |

---

## 5. Network Architecture

The machines normally have two types of addresses.

## Physical Network

For example:

```text
Client physical IP:
192.168.1.20

Server physical IP:
192.168.1.10
```

These are just examples.

They may be completely different in your environment.

---

## WireGuard Network

We create a separate private network.

Example:

```text
10.50.0.0/24
```

The server could use:

```text
10.50.0.1
```

and the client:

```text
10.50.0.2
```

The important thing is that both machines belong to the same VPN subnet.

---

## 6. Configuration Variables

Before starting, determine the values for your environment.

Use a table like this:

| Variable           | Example        | Your Value   |
| ------------------ | -------------- | ------------ |
| Server physical IP | `192.168.1.10` | `YOUR_VALUE` |
| Server VPN IP      | `10.50.0.1`    | `YOUR_VALUE` |
| Client VPN IP      | `10.50.0.2`    | `YOUR_VALUE` |
| VPN subnet         | `10.50.0.0/24` | `YOUR_VALUE` |
| WireGuard port     | `51820`        | `YOUR_VALUE` |
| SSH username       | `user`         | `YOUR_VALUE` |

For the rest of this guide, examples will use:

```text
Server physical IP = 192.168.1.10
Server VPN IP      = 10.50.0.1
Client VPN IP      = 10.50.0.2
VPN subnet         = 10.50.0.0/24
WireGuard port     = 51820
SSH username       = user
```

**These are examples. Replace them with your actual values.**

---

## 7. Check the Physical Network First

Before configuring WireGuard, make sure the client can reach the server normally.

On the SERVER:

```bash
ip -br addr
```

Find the server's physical IP.

For example:

```text
eth0    UP    192.168.1.10/24
```

On the CLIENT:

```bash
ip -br addr
```

For example:

```text
wlan0   UP    192.168.1.20/24
```

Now test connectivity from the client:

```bash
ping -c 4 192.168.1.10
```

Replace the IP with the server's actual physical IP.

If this does not work, fix the normal network connection before configuring WireGuard.

---

## 8. Install WireGuard

WireGuard must be installed on both machines.

## 8.1 Server

Run:

```bash
sudo apt update
sudo apt install wireguard
```

Verify:

```bash
wg --version
```

---

## 8.2 Client

Run:

```bash
sudo apt update
sudo apt install wireguard
```

Verify:

```bash
wg --version
```

---

## 9. Generate Server Keys

WireGuard uses public/private key pairs.

Each peer has:

```text
Private Key
Public Key
```

The private key must remain secret.

The public key is shared with the other peer.

---

## 9.1 Create WireGuard Directory

On the SERVER:

```bash
sudo mkdir -p /etc/wireguard
sudo chmod 700 /etc/wireguard
```

---

## 9.2 Generate Private Key

```bash
sudo sh -c 'umask 077; wg genkey > /etc/wireguard/server_private.key'
```

---

## 9.3 Generate Public Key

```bash
sudo sh -c 'wg pubkey < /etc/wireguard/server_private.key > /etc/wireguard/server_public.key'
```

---

## 9.4 View Server Public Key

```bash
sudo cat /etc/wireguard/server_public.key
```

Save this value somewhere temporarily.

It will be needed by the client.

Do **not** share the server private key.

---

## 10. Generate Client Keys

On the CLIENT:

```bash
mkdir -p ~/wireguard
chmod 700 ~/wireguard
```

Generate the keys:

```bash
umask 077
wg genkey | tee ~/wireguard/client_private.key | wg pubkey > ~/wireguard/client_public.key
```

View the public key:

```bash
cat ~/wireguard/client_public.key
```

The client public key will be needed on the server.

---

## 11. Understand the Keys

At this point we have:

```text
SERVER
    |
    +-- server_private.key
    +-- server_public.key


CLIENT
    |
    +-- client_private.key
    +-- client_public.key
```

The sharing relationship is:

```text
Server private key
    |
    +-- NEVER share

Server public key
    |
    +-- Give to client


Client private key
    |
    +-- NEVER share

Client public key
    |
    +-- Give to server
```

Never put private keys into:

* GitHub
* Public documentation
* Screenshots
* Chat messages
* README files

---

## 12. Configure the WireGuard Server

Create the configuration file:

```bash
sudo nano /etc/wireguard/wg0.conf
```

Add:

```ini
[Interface]

## Server's WireGuard/VPN IP address
Address = 10.50.0.1/24

## WireGuard UDP listening port
ListenPort = 51820

## Server's private key
PrivateKey = SERVER_PRIVATE_KEY


[Peer]

## Client's public key
PublicKey = CLIENT_PUBLIC_KEY

## VPN IP assigned to this client
AllowedIPs = 10.50.0.2/32
```

Replace:

```text
10.50.0.1
```

with your server's VPN IP.

Replace:

```text
51820
```

if you are using another WireGuard port.

Replace:

```text
SERVER_PRIVATE_KEY
```

with the server's private key.

Replace:

```text
CLIENT_PUBLIC_KEY
```

with the client's public key.

Replace:

```text
10.50.0.2/32
```

with the client's VPN IP.

---

## 13. Why Does the Server Use /32 for the Client?

The server configuration says:

```ini
AllowedIPs = 10.50.0.2/32
```

`/32` means:

> This peer owns exactly this one IP address.

So the server knows:

```text
10.50.0.2
     |
     +---- Client
```

If another client is added:

```text
10.50.0.3
```

it would normally have its own peer entry:

```ini
[Peer]
PublicKey = ANOTHER_CLIENT_PUBLIC_KEY
AllowedIPs = 10.50.0.3/32
```

---

## 14. Protect the Server Configuration

Run:

```bash
sudo chmod 600 /etc/wireguard/wg0.conf
```

This prevents other normal users from reading the private key contained in the configuration.

---

## 15. Configure the WireGuard Client

On the CLIENT:

```bash
sudo nano /etc/wireguard/wg0.conf
```

Add:

```ini
[Interface]

## Client's WireGuard/VPN IP
Address = 10.50.0.2/24

## Client's private key
PrivateKey = CLIENT_PRIVATE_KEY


[Peer]

## Server's public key
PublicKey = SERVER_PUBLIC_KEY

## Server's physical IP and WireGuard port
Endpoint = 192.168.1.10:51820

## Traffic for the VPN network should use WireGuard
AllowedIPs = 10.50.0.0/24

## Helps maintain connectivity behind NAT
PersistentKeepalive = 25
```

Replace:

```text
10.50.0.2
```

with the client's VPN IP.

Replace:

```text
CLIENT_PRIVATE_KEY
```

with the client's private key.

Replace:

```text
SERVER_PUBLIC_KEY
```

with the server's public key.

Replace:

```text
192.168.1.10
```

with the server's physical/reachable IP.

---

## 16. Understand the Client's Endpoint

The client has:

```ini
Endpoint = 192.168.1.10:51820
```

This means:

> Send the encrypted WireGuard packets to the server's physical address on UDP port 51820.

The endpoint is **not normally the VPN IP**.

For example:

```text
Physical network:

Client -------------------- Server
                           192.168.1.10
                                ^
                                |
                             Endpoint


VPN network:

Client -------------------- Server
10.50.0.2                  10.50.0.1
```

The physical address transports the encrypted tunnel.

The VPN addresses are used for traffic inside the tunnel.

---

## 17. Understand AllowedIPs

This is one of the most important WireGuard concepts.

On the CLIENT:

```ini
AllowedIPs = 10.50.0.0/24
```

means:

> Traffic destined for the 10.50.0.0/24 VPN network should go through this WireGuard peer.

Therefore:

```text
10.50.0.1
10.50.0.2
10.50.0.3
...
```

can be reached through WireGuard.

---

## 18. /24 vs /32

These two prefixes have different meanings.

### /24

```text
10.50.0.0/24
```

represents the VPN network.

It covers the subnet:

```text
10.50.0.0 - 10.50.0.255
```

### /32

```text
10.50.0.2/32
```

represents one exact host.

So:

```text
/24 = network/subnet
/32 = one specific IP
```

---

## 19. Start WireGuard on the Server

On the SERVER:

```bash
sudo wg-quick up wg0
```

Check:

```bash
ip -br addr show wg0
```

Expected:

```text
wg0    UNKNOWN    10.50.0.1/24
```

Your actual address may be different.

---

## 20. Start WireGuard on the Client

On the CLIENT:

```bash
sudo wg-quick up wg0
```

Check:

```bash
ip -br addr show wg0
```

Expected:

```text
wg0    UNKNOWN    10.50.0.2/24
```

---

## 21. Check WireGuard Status

On either machine:

```bash
sudo wg show
```

You should see something similar to:

```text
interface: wg0
  public key: SERVER_PUBLIC_KEY
  private key: (hidden)
  listening port: 51820

peer: CLIENT_PUBLIC_KEY
  allowed ips: 10.50.0.2/32
```

On the client you should see the server as the peer.

---

## 22. Check the Handshake

Run:

```bash
sudo wg show
```

Look for:

```text
latest handshake: ...
```

For example:

```text
latest handshake: 30 seconds ago
```

This means the two WireGuard peers have successfully communicated and authenticated.

You may also see:

```text
transfer: 10 KiB received, 15 KiB sent
```

These counters show traffic passing through the WireGuard connection.

---

## 23. Test the VPN with Ping

From the CLIENT:

```bash
ping -c 4 10.50.0.1
```

Replace `10.50.0.1` with the server's VPN IP.

Expected:

```text
64 bytes from 10.50.0.1
64 bytes from 10.50.0.1
64 bytes from 10.50.0.1
64 bytes from 10.50.0.1
```

If this works, the basic VPN connection is working.

---

## 24. Verify the Route

On the CLIENT:

```bash
ip route
```

Look for a route similar to:

```text
10.50.0.0/24 dev wg0
```

Then run:

```bash
ip route get 10.50.0.1
```

Expected:

```text
10.50.0.1 dev wg0 src 10.50.0.2
```

The exact output may vary.

The important part is:

```text
dev wg0
```

This means traffic to the server's VPN address is being sent through WireGuard.

---

## 25. Understand What Happens to a Packet

Suppose the client runs:

```bash
ping 10.50.0.1
```

The original packet is:

```text
Source:
10.50.0.2

Destination:
10.50.0.1
```

WireGuard encrypts this packet.

The encrypted packet is transported using the physical network:

```text
Client physical IP
       |
       | encrypted UDP
       |
       v
Server physical IP
```

The server receives it.

WireGuard decrypts it.

The original packet becomes available on:

```text
wg0
```

So there are effectively two layers:

```text
OUTER PACKET
Physical network
+
Encrypted WireGuard data


INNER PACKET
10.50.0.2
     |
     v
10.50.0.1
```

---

## 26. Test SSH

Once ping works, install SSH on the server if necessary.

On SERVER:

```bash
sudo apt update
sudo apt install openssh-server
```

Check:

```bash
sudo systemctl status ssh
```

If necessary:

```bash
sudo systemctl enable --now ssh
```

---

## 27. Connect Using the VPN IP

From the CLIENT:

```bash
ssh username@10.50.0.1
```

Replace:

```text
username
```

with the actual server username.

Replace:

```text
10.50.0.1
```

with the server's WireGuard IP.

For example:

```bash
ssh user@10.50.0.1
```

This is the important part:

```text
Do NOT use the server's physical IP for this test.

Use the WireGuard/VPN IP.
```

---

## 28. Final Connection Flow

The final connection looks like:

```text
             Physical Network
                   |
                   |
                   v
          +----------------+
          |     SERVER     |
          |                |
          | Physical IP    |
          | 192.168.1.10   |
          |                |
          | wg0            |
          | 10.50.0.1      |
          +-------+--------+
                  |
                  |
             WireGuard
              Tunnel
                  |
                  |
          +-------+--------+
          |     CLIENT     |
          |                |
          | wg0            |
          | 10.50.0.2      |
          +----------------+
                  |
                  |
                 SSH
                  |
                  v
             Server Shell
```

---

## 29. Enable WireGuard at Boot

Once everything has been tested successfully, enable WireGuard to start automatically.

On the SERVER:

```bash
sudo systemctl enable wg-quick@wg0
```

On the CLIENT:

```bash
sudo systemctl enable wg-quick@wg0
```

Check:

```bash
systemctl status wg-quick@wg0
```

---

## 30. Restarting WireGuard

If you change the configuration, restart WireGuard.

```bash
sudo wg-quick down wg0
sudo wg-quick up wg0
```

Or:

```bash
sudo systemctl restart wg-quick@wg0
```

Then verify:

```bash
sudo wg show
```

---

## 31. Basic Troubleshooting

If the VPN doesn't work, do not randomly change settings.

Follow this order.

---

## Problem 1: wg0 Doesn't Exist

Check:

```bash
ip link show wg0
```

If it doesn't exist:

```bash
sudo wg-quick up wg0
```

If there is an error, read the error carefully.

---

## 32. Problem 2: wg0 Exists but Has No IP

Check:

```bash
ip -br addr show wg0
```

If the expected VPN IP is missing, restart:

```bash
sudo wg-quick down wg0
sudo wg-quick up wg0
```

Then check again:

```bash
ip -br addr show wg0
```

---

## 33. Problem 3: No Handshake

Run:

```bash
sudo wg show
```

If there is no:

```text
latest handshake
```

check:

### 1. Is the server running?

```bash
sudo wg show
```

### 2. Is the endpoint correct?

Client configuration:

```ini
Endpoint = SERVER_PHYSICAL_IP:51820
```

### 3. Are the public keys correct?

The client must have the server's public key.

The server must have the client's public key.

### 4. Is UDP port 51820 reachable?

Check the server:

```bash
sudo ss -lunp | grep 51820
```

---

## 34. Problem 4: Handshake Works but Ping Fails

If:

```text
latest handshake
```

exists but ping fails, check:

```bash
ip route get SERVER_VPN_IP
```

Example:

```bash
ip route get 10.50.0.1
```

It should use:

```text
dev wg0
```

Also check:

```bash
sudo wg show
```

and:

```bash
sudo tcpdump -ni wg0
```

---

## 35. Problem 5: Traffic Goes Through the Wrong Interface

Run:

```bash
ip route get SERVER_VPN_IP
```

If you see something like:

```text
via GATEWAY dev wlan0
```

instead of:

```text
dev wg0
```

then the routing configuration is incorrect.

Check the client's:

```ini
AllowedIPs =
```

For a simple VPN-to-server setup, it could be:

```ini
AllowedIPs = 10.50.0.0/24
```

or, if only the server itself should be reachable:

```ini
AllowedIPs = 10.50.0.1/32
```

---

## 36. Problem 6: SSH Doesn't Work

First verify WireGuard:

```bash
sudo wg show
```

Then:

```bash
ping -c 4 SERVER_VPN_IP
```

If ping works, check SSH:

```bash
sudo systemctl status ssh
```

Check whether SSH is listening:

```bash
sudo ss -lntp | grep ':22'
```

Then try:

```bash
ssh username@SERVER_VPN_IP
```

---

## 37. Use tcpdump for Troubleshooting

`tcpdump` is useful for seeing whether packets are actually moving.

## On the Physical Interface

First find the interface:

```bash
ip -br link
```

Then:

```bash
sudo tcpdump -ni PHYSICAL_INTERFACE udp port 51820
```

For example:

```bash
sudo tcpdump -ni eth0 udp port 51820
```

This shows WireGuard's encrypted UDP traffic.

---

## On wg0

Run:

```bash
sudo tcpdump -ni wg0
```

This lets you inspect traffic inside the VPN.

For example, you may see:

```text
10.50.0.2 > 10.50.0.1
```

---

## 38. Physical Interface vs wg0

This distinction is important.

### Physical interface

Example:

```text
eth0
```

Carries the encrypted WireGuard transport:

```text
UDP
Port 51820
```

### WireGuard interface

```text
wg0
```

Carries the decrypted VPN traffic:

```text
ICMP
TCP
SSH
etc.
```

Conceptually:

```text
Physical Interface
        |
        | Encrypted
        v
   WireGuard
        |
        | Decrypted
        v
       wg0
```

---

## 39. Optional: Restrict SSH to WireGuard

After everything works, you may want SSH to be accessible only through the VPN.

The desired architecture is:

```text
Physical network
       |
       X
    SSH blocked
       |
       v
    Server

WireGuard
    |
    |
    v
   wg0
    |
    v
   SSH
```

This creates a stronger security boundary.

However:

**Do not configure this until VPN SSH has been tested successfully.**

Otherwise, you could accidentally lock yourself out of the server.

Firewall rules depend on your Linux distribution and existing firewall configuration, so inspect the current firewall first:

```bash
sudo ufw status
```

and:

```bash
sudo iptables -L -n -v
```

Do not blindly apply firewall rules from another system.

---

## 40. Adding More Clients

WireGuard can have multiple clients.

For example:

```text
                    SERVER
                  10.50.0.1
                      |
          +-----------+-----------+
          |           |           |
          |           |           |
       Client 1    Client 2    Client 3
      10.50.0.2   10.50.0.3   10.50.0.4
```

Each client gets:

* Its own private key
* Its own public key
* Its own VPN IP
* Its own `[Peer]` entry on the server

Example:

```ini
[Peer]
PublicKey = CLIENT1_PUBLIC_KEY
AllowedIPs = 10.50.0.2/32

[Peer]
PublicKey = CLIENT2_PUBLIC_KEY
AllowedIPs = 10.50.0.3/32

[Peer]
PublicKey = CLIENT3_PUBLIC_KEY
AllowedIPs = 10.50.0.4/32
```

Do not assign the same VPN IP to two clients.

---

## 41. Understanding WireGuard "Groups"

Basic WireGuard does not have traditional user groups such as:

```text
Administrators
Developers
Employees
Guests
```

WireGuard mainly works with:

```text
Peers
Keys
AllowedIPs
Routing
Encryption
```

If an organization needs groups and access policies, additional systems can be used, such as:

* Firewalls
* Identity management
* Access-control systems
* VPN gateways
* Policy engines

For a small lab, different peers can be assigned different VPN IPs and firewall rules can then control what those peers can access.

---

## 42. Security Best Practices

## Protect Private Keys

Never expose:

```text
PrivateKey
```

Private keys should stay on their respective machines.

---

## Protect Configuration Files

Use:

```bash
sudo chmod 600 /etc/wireguard/wg0.conf
```

---

## Use Unique Keys

Every peer should have its own key pair.

Do not copy one client's private key to another client.

---

## Use Unique VPN IPs

For example:

```text
10.50.0.2
10.50.0.3
10.50.0.4
```

Each should belong to a different peer.

---

## Don't Disable Security Just to Make It Work

Avoid permanently disabling:

* Firewall
* SSH authentication
* Key verification
* Network security

Instead, identify which layer is failing.

---

## 43. Useful Verification Commands

## Show interfaces

```bash
ip -br addr
```

---

## Show routes

```bash
ip route
```

---

## Check a specific route

```bash
ip route get SERVER_VPN_IP
```

---

## Show WireGuard status

```bash
sudo wg show
```

---

## Start WireGuard

```bash
sudo wg-quick up wg0
```

---

## Stop WireGuard

```bash
sudo wg-quick down wg0
```

---

## Restart WireGuard

```bash
sudo wg-quick down wg0
sudo wg-quick up wg0
```

---

## Test VPN connectivity

```bash
ping -c 4 SERVER_VPN_IP
```

---

## Test SSH

```bash
ssh USERNAME@SERVER_VPN_IP
```

---

## Check SSH service

```bash
sudo systemctl status ssh
```

---

## Check WireGuard UDP port

```bash
sudo ss -lunp | grep 51820
```

---

## Capture WireGuard traffic

```bash
sudo tcpdump -ni PHYSICAL_INTERFACE udp port 51820
```

---

## Capture VPN traffic

```bash
sudo tcpdump -ni wg0
```

---

## 44. Complete Setup Checklist

Use this checklist when setting up from scratch.

## Server

```text
[ ] WireGuard installed
[ ] Server private key generated
[ ] Server public key generated
[ ] /etc/wireguard/wg0.conf created
[ ] Server VPN IP configured
[ ] Client public key added
[ ] Client AllowedIPs configured
[ ] wg0 started
```

## Client

```text
[ ] WireGuard installed
[ ] Client private key generated
[ ] Client public key generated
[ ] /etc/wireguard/wg0.conf created
[ ] Client VPN IP configured
[ ] Server public key added
[ ] Server endpoint configured
[ ] AllowedIPs configured
[ ] wg0 started
```

## Verification

```text
[ ] wg0 exists on server
[ ] wg0 exists on client
[ ] Correct VPN IP on server
[ ] Correct VPN IP on client
[ ] WireGuard handshake exists
[ ] Transfer counters increase
[ ] Route uses wg0
[ ] Ping works
[ ] SSH service is running
[ ] SSH through VPN IP works
```

---

## 45. Final Testing Procedure

After configuration, perform the following tests in order.

### Test 1 — Interface

Client:

```bash
ip -br addr show wg0
```

Server:

```bash
ip -br addr show wg0
```

---

### Test 2 — WireGuard

```bash
sudo wg show
```

Look for:

```text
latest handshake
```

---

### Test 3 — Routing

Client:

```bash
ip route get SERVER_VPN_IP
```

Make sure it uses:

```text
dev wg0
```

---

### Test 4 — VPN Connectivity

Client:

```bash
ping -c 4 SERVER_VPN_IP
```

---

### Test 5 — SSH

Client:

```bash
ssh USERNAME@SERVER_VPN_IP
```

---

### Test 6 — Packet Capture

On the server:

```bash
sudo tcpdump -ni PHYSICAL_INTERFACE udp port 51820
```

In another terminal:

```bash
sudo tcpdump -ni wg0
```

Then generate traffic from the client:

```bash
ping -c 4 SERVER_VPN_IP
```

This allows you to observe the difference between:

```text
Encrypted WireGuard transport
```

and:

```text
Inner VPN traffic
```

---

## 46. Troubleshooting Flowchart

When something fails, follow this order:

```text
                 START
                   |
                   v
             Is wg0 running?
              /          \
            NO            YES
            |              |
            v              v
      Start wg0       Does it have
                       correct IP?
                       /       \
                     NO         YES
                     |           |
                     v           v
                 Fix config   Is there
                              handshake?
                              /       \
                            NO         YES
                            |           |
                            v           v
                       Check keys,   Check route
                       endpoint,     and firewall
                       UDP port          |
                                         v
                                    Does ping work?
                                    /          \
                                  NO            YES
                                  |              |
                                  v              v
                              Check routing   Test SSH
                              and firewall       |
                                                v
                                             SUCCESS
```

---

## 47. What You Should Understand After Completing This

After completing this lab, you should understand:

### 1. Physical Network

How the machines communicate normally.

```text
Physical IP
     |
     v
Physical interface
```

### 2. WireGuard

How an encrypted virtual network is created.

```text
wg0
 |
 +-- VPN IP
 +-- Encryption
 +-- Peer authentication
```

### 3. Public/Private Keys

How WireGuard identifies peers.

```text
Private key = secret
Public key  = shared
```

### 4. AllowedIPs

How WireGuard associates IP addresses with peers and influences routing.

```text
AllowedIPs = Which addresses belong to/use this peer
```

### 5. Endpoint

Where encrypted WireGuard packets are sent.

```text
Endpoint = physical/reachable IP + UDP port
```

### 6. Routing

How Linux decides which interface should carry a packet.

```text
Destination
     |
     v
Routing table
     |
     v
Interface
```

### 7. SSH

How a user logs into the server after reaching it through the VPN.

---

## 48. The Most Important Concepts

Remember these three configuration options:

```text
Address
AllowedIPs
Endpoint
```

They answer three different questions.

### Address

```ini
Address = 10.50.0.2/24
```

Means:

> What is my VPN IP?

---

### AllowedIPs

```ini
AllowedIPs = 10.50.0.0/24
```

Means:

> Which destination IPs should use this WireGuard peer?

On the server, it also identifies which IP belongs to that peer.

---

### Endpoint

```ini
Endpoint = 192.168.1.10:51820
```

Means:

> Where do I send the encrypted WireGuard packets?

---

## 49. One-Line Summary

The entire setup can be remembered as:

```text
Physical Network
      |
      | carries encrypted WireGuard packets
      |
      v
WireGuard VPN
      |
      | provides private VPN IPs
      |
      v
SSH
      |
      | authenticates the user
      |
      v
Server
```

Or even more simply:

```text
WireGuard = secure network path

SSH = secure remote login
```

Together:

```text
VPN + SSH
=
Encrypted network + authenticated user access
```

---

```bash
## Start VPN
sudo wg-quick up wg0

## Verify
sudo wg show

## Test VPN
ping -c 4 SERVER_VPN_IP

## Test SSH
ssh USERNAME@SERVER_VPN_IP
```

If all four stages work:

```text
WireGuard interface
        ↓
Handshake
        ↓
VPN connectivity
        ↓
SSH
```

the basic WireGuard + SSH setup is complete.
