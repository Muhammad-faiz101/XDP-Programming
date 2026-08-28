````markdown
# Remote Access to an Ubuntu Server Using Tailscale and SSH

This guide explains how to securely access an Ubuntu server remotely when the server is located behind a university, office, home, or other private network.

The setup uses:

- **Tailscale** — creates a private network between authorized devices.
- **SSH** — provides terminal access to the server.

No public IP, port forwarding, or router configuration is required.

---

## 1. Architecture

```text
                    Internet
                       |
          +------------+------------+
          |                         |
          v                         v
    Remote Laptop              Ubuntu Server
          |                         |
          |       Tailscale         |
          +-------------------------+
                    |
                SSH Access
````

After Tailscale is configured, the server receives a private Tailscale IP such as:

```text
100.x.x.x
```

This IP is used for remote SSH connections.

---

## 2. Prerequisites

Before starting, make sure:

* The server is running Ubuntu/Linux.
* The server has Internet access.
* The remote laptop has Internet access.
* You have access to a Tailscale account.
* You know the Linux username on the server.
* SSH is installed/enabled on the server.

---

# 3. Server Setup

## Step 1: Check Internet Connectivity

On the server, run:

```bash
ping -c 3 8.8.8.8
```

If replies are received, Internet connectivity is working.

Also test DNS:

```bash
ping -c 3 google.com
```

If the first command works but the second fails, see the
[Troubleshooting](#troubleshooting) section.

---

## Step 2: Install Tailscale

Run:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
```

Start and authenticate Tailscale:

```bash
sudo tailscale up
```

A login URL will be displayed.

Open the URL in a browser and sign in using your Tailscale account.

---

## Step 3: Find the Tailscale IP

Run:

```bash
tailscale status
```

Then:

```bash
tailscale ip
```

Example:

```text
100.102.108.17
```

This is the server's **Tailscale IP**.

Use this IP for remote access.

---

## Step 4: Verify Tailscale

Check the Tailscale service:

```bash
systemctl status tailscaled
```

It should show:

```text
Active: active (running)
```

Tailscale normally starts automatically when the server boots.

---

# 4. Remote Laptop Setup

## Linux

Install Tailscale:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
```

Start Tailscale:

```bash
sudo tailscale up
```

Sign in using the **same Tailscale account/tailnet** used for the server.

## Windows / macOS

Download and install Tailscale from:

[https://tailscale.com/download](https://tailscale.com/download)

Then sign in using the same Tailscale account.

---

# 5. Test the Connection

On the remote laptop, check the devices visible to Tailscale:

```bash
tailscale status
```

The server should appear in the list.

Test the connection:

```bash
tailscale ping <SERVER_TAILSCALE_IP>
```

Example:

```bash
tailscale ping 100.102.108.17
```

A successful response should look similar to:

```text
pong from xdpserver (100.102.108.17) ...
```

> **Note:** Normal `ping <SERVER_TAILSCALE_IP>` may fail even when Tailscale is working. Use `tailscale ping` as the primary Tailscale connectivity test.

---

# 6. Connect Using SSH

From the remote laptop:

```bash
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

Example:

```bash
ssh xupsys@100.102.108.17
```

The first connection may display a host-key confirmation:

```text
Are you sure you want to continue connecting?
```

Verify that the server is correct and enter:

```text
yes
```

Then enter the server user's password.

You are now connected to the server remotely.

---

# 7. Accessing the Server After Reboot

You normally **do not need to repeat the setup** after rebooting.

### On the server

Tailscale should start automatically.

Check:

```bash
tailscale status
```

If necessary:

```bash
sudo tailscale up
```

### On the remote laptop

Make sure Tailscale is running:

```bash
tailscale status
```

Then connect:

```bash
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

---

# 8. Changing Wi-Fi or Location

Changing the laptop's network does **not** require reconfiguring Tailscale.

For example, the laptop can switch between:

* Home Wi-Fi
* University Wi-Fi
* Another Wi-Fi network
* Mobile hotspot
* A network in another city or country

The server's Tailscale IP remains the same.

Example:

```bash
ssh xupsys@100.102.108.17
```

The SSH fingerprint also normally does **not** need to be accepted again when changing networks because the connection is still to the same server.

---

# 9. Using `tmux` for Long-Running Tasks

For long-running experiments or programs, `tmux` is recommended.

Create a session:

```bash
tmux new -s work
```

Run your experiment or program inside the session.

Detach from the session:

```text
Ctrl+B
then D
```

The program continues running on the server even if the SSH connection is lost.

Reconnect later:

```bash
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

Then:

```bash
tmux attach -t work
```

This is particularly useful for long-running TRex, DPDK, XDP, or other network experiments.

---

# 10. Troubleshooting

## `curl: Could not resolve host`

Example:

```text
curl: (6) Could not resolve host
```

Check Internet connectivity:

```bash
ping -c 3 8.8.8.8
```

If this works but:

```bash
ping -c 3 google.com
```

fails, DNS resolution is the problem.

Check:

```bash
resolvectl status
```

Do not manually modify `/etc/resolv.conf` unless you understand how your system's DNS configuration is managed.

---

## Tailscale Is Not Running

Check:

```bash
systemctl status tailscaled
```

Restart it if necessary:

```bash
sudo systemctl restart tailscaled
```

Then check:

```bash
tailscale status
```

---

## Server Does Not Appear in `tailscale status`

Make sure both devices are signed into the same Tailscale account/tailnet.

On the server:

```bash
tailscale status
```

On the remote laptop:

```bash
tailscale status
```

If necessary, authenticate again:

```bash
sudo tailscale up
```

---

## `tailscale ping` Fails

Run:

```bash
tailscale netcheck
```

Check that both devices have Internet access and Tailscale is running.

---

## Tailscale Works but SSH Fails

Check SSH on the server:

```bash
sudo systemctl status ssh
```

If SSH is not installed:

```bash
sudo apt install openssh-server
```

Enable and start it:

```bash
sudo systemctl enable --now ssh
```

Then try:

```bash
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

---

## `Permission denied` During SSH

Make sure the username is correct.

On the server:

```bash
whoami
```

Then use:

```bash
ssh <CORRECT_USERNAME>@<SERVER_TAILSCALE_IP>
```

---

## `REMOTE HOST IDENTIFICATION HAS CHANGED`

If SSH displays:

```text
WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!
```

**Do not blindly accept the new fingerprint.**

The server may have been reinstalled or its SSH host key may have changed. Verify the server before modifying the SSH `known_hosts` entry.

---

# 11. Recommended Security Practices

For a team environment:

* Use Tailscale instead of exposing SSH directly to the public Internet.
* Do not share personal passwords between team members.
* Prefer individual Linux accounts for each team member.
* Use SSH keys instead of shared passwords where possible.
* Never share private SSH keys.
* Use `tmux` for long-running experiments.
* Keep the server powered on and connected to the Internet.

---

# 12. Quick Reference

### Check Tailscale

```bash
tailscale status
```

### Get Tailscale IP

```bash
tailscale ip
```

### Test Tailscale

```bash
tailscale ping <SERVER_TAILSCALE_IP>
```

### Connect to Server

```bash
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

### Check Tailscale Service

```bash
systemctl status tailscaled
```

### Create a Persistent Session

```bash
tmux new -s work
```

### Reconnect to a Session

```bash
tmux attach -t work
```

---

# 13. Normal Workflow

Once the setup is complete, remote access is simply:

```bash
# 1. Check Tailscale
tailscale status

# 2. Test the server
tailscale ping <SERVER_TAILSCALE_IP>

# 3. Connect
ssh <USERNAME>@<SERVER_TAILSCALE_IP>
```

For example:

```bash
ssh xupsys@100.102.108.17
```

As long as:

1. The server is powered on.
2. The server has Internet access.
3. Tailscale is running on the server.
4. Tailscale is running on the remote laptop.

the server can be accessed remotely from different networks and locations.

---

## Official Documentation

* Tailscale: [https://tailscale.com/](https://tailscale.com/)
* Tailscale Downloads: [https://tailscale.com/download](https://tailscale.com/download)
* OpenSSH: [https://www.openssh.com/](https://www.openssh.com/)
* tmux: [https://github.com/tmux/tmux](https://github.com/tmux/tmux)

```
```