Pktgen-DPDK: Ubuntu 10GbE → Laptop 1GbE Setup
Ubuntu Server
    |
    | 10GbE NIC
    | Intel X540-AT2
    | DPDK / vfio-pci
    |
    | Ethernet cable
    |
    v
Laptop
    |
    | USB Ethernet adapter
    | enxf8e43bead16f
    | 1Gbps
    |
    IP: 192.168.60.3
1. Ubuntu Server — identify the DPDK port

Check DPDK device binding:

sudo dpdk-devbind.py --status

The Pktgen port used in this experiment:

0000:d8:00.1
Intel X540-AT2 10GbE
Driver: vfio-pci

Check PCI driver:

sudo lspci -nnk -s d8:00.1

Check PCIe link:

sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'

Expected:

LnkCap: Speed 5GT/s, Width x8
LnkSta: Speed 5GT/s, Width x8

Check that the physical link is 10Gbps:

sudo ethtool <kernel-interface>

For the DPDK-bound port, however, there is no normal Linux interface because the NIC is owned by vfio-pci.

2. Laptop — identify Ethernet interface

Find interfaces:

ip addr

In this setup the USB Ethernet interface is:

enxf8e43bead16f

Check link:

sudo ethtool enxf8e43bead16f

Important result:

Speed: 1000Mb/s
Link detected: yes

Therefore the laptop link is 1 Gbps, not 10 Gbps.

Check interface:

ip addr show enxf8e43bead16f
3. Configure laptop IP

Assign:

sudo ip addr add 192.168.60.3/24 dev enxf8e43bead16f

Verify:

ip addr show enxf8e43bead16f

Expected:

inet 192.168.60.3/24

The Ubuntu/Pktgen source IP is:

192.168.60.1

The laptop destination IP is:

192.168.60.3
4. Verify laptop MAC address
ip link show enxf8e43bead16f

This setup:

MAC: f8:e4:3b:ea:d1:6f

This MAC is used as the Pktgen destination MAC.

5. Start Pktgen

Because DPDK libraries are installed under /usr/local/lib/x86_64-linux-gnu, use:

sudo env LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu \
./build/app/pktgen \
-l 10-19 \
-n 4 \
-a 0000:d8:00.1 \
--socket-mem=0,1024 \
-- \
-m "[11:12].0"

Important:

0000:d8:00.1

is the X540-AT2 10GbE NIC being used by DPDK.

Check Pktgen process:

ps aux | grep '[p]ktgen'
6. Configure Pktgen packet

The important Pktgen configuration is:

Source IP      : 192.168.60.1
Destination IP : 192.168.60.3


Source MAC      : b4:96:91:12:9d:46
Destination MAC : f8:e4:3b:ea:d1:6f


Packet size     : 1400 bytes

Verify Pktgen port:

Link State          : <UP-10000-FD>

This means the Ubuntu server's physical NIC is linked at 10 Gbps.

7. Configure continuous transmission

Pktgen should show:

Tx Count/% Rate     : Forever /100%

This means Pktgen continuously transmits rather than sending only 10 packets.

For example:

Tx Pkts       : 2,333,384
Tx Max        : 88,264 packets/s
8. Monitor Pktgen

Pktgen port statistics:

Pkts Rx/Tx
Rate Rx/Tx
Total Rx Pkts
Tx Pkts
Pkts/s Rx Max
Tx Max
Errors Rx/Tx

For example:

Tx Pkts : 2,333,384
Tx Max  : 88,264 packets/s

Tx Pkts is the cumulative number of packets transmitted.

Tx Max is the maximum packet rate observed.

9. Capture packets on laptop

Run:

sudo tcpdump -i enxf8e43bead16f -nn -e -c 10

Expected packet:

b4:96:91:12:9d:46 > f8:e4:3b:ea:d1:6f
192.168.60.1.1234 > 192.168.60.3.5678

This proves that packets physically reached the laptop's NIC and were visible to Linux.

For continuous capture:

sudo tcpdump -i enxf8e43bead16f -nn -e

To capture only traffic from the Ubuntu server:

sudo tcpdump -i enxf8e43bead16f -nn -e \
'ether src b4:96:91:12:9d:46'

Or:

sudo tcpdump -i enxf8e43bead16f -nn \
'host 192.168.60.1'
10. Check laptop RX statistics

Before starting Pktgen:

ip -s link show enxf8e43bead16f

Start Pktgen and then run:

ip -s link show enxf8e43bead16f

Example:

RX:
    bytes     packets    errors    dropped
    41460     30         0         0

The important fields are:

RX packets
RX errors
RX dropped
RX missed

If RX packets increase while Pktgen TX packets increase, packets are reaching the laptop.

11. Check laptop NIC hardware statistics
sudo ethtool -S enxf8e43bead16f

Useful filtered version:

sudo ethtool -S enxf8e43bead16f | \
grep -Ei 'rx|drop|err|miss|fifo|buf'

This can reveal hardware-level receive errors/drops.

12. Check laptop link speed
sudo ethtool enxf8e43bead16f

The important field is:

Speed: 1000Mb/s

Therefore:

Ubuntu NIC       = 10 Gbps
Laptop USB NIC   = 1 Gbps

The end-to-end physical link is therefore limited to 1 Gbps.

The 10GbE Pktgen NIC can generate 10GbE traffic, but the laptop-side 1GbE interface cannot receive 10Gbps.

13. Calculate/interpret the Pktgen result

Pktgen example:

Tx Max : 88,264 packets/s
Packet size : 1400 bytes

Approximate payload rate:

88,264 × 1400 × 8
≈ 989 Mbps

So this is approximately 1 Gbps of packet payload traffic.

That makes sense because the receiving laptop has a 1Gbps Ethernet link.

Remember that Ethernet has additional headers/overhead, so the actual wire rate is somewhat higher than the IP payload rate.

14. Important distinction

This experiment has two different link capabilities:

Ubuntu X540-AT2
       |
       | 10 Gbps capable
       |
       v
Ethernet cable
       |
       | limited by receiver
       v
Laptop USB Ethernet
       |
       | 1 Gbps
       v
Laptop

Therefore:

Can Ubuntu generate 10Gbps?

Yes. The X540-AT2 is a 10GbE NIC and Pktgen-DPDK can generate traffic at 10GbE rates.

Can this particular laptop receive 10Gbps?

No. Its enxf8e43bead16f interface is currently linked at:

1000 Mbps = 1 Gbps

So this setup is useful for testing up to approximately 1Gbps at the receiver.

15. Useful troubleshooting commands
Ubuntu
sudo dpdk-devbind.py --status
sudo lspci -nnk -s d8:00.1
sudo lspci -vv -s d8:00.1 | grep -Ei 'LnkCap|LnkSta'
ps aux | grep '[p]ktgen'
sudo dpdk-proc-info --proc-type=secondary --file-prefix=pktgen -- --show-port

Note: dpdk-proc-info only works while the corresponding DPDK primary process is running and when the DPDK installation/environment matches Pktgen.

Laptop
ip addr
ip addr show enxf8e43bead16f
ip link show enxf8e43bead16f
sudo ethtool enxf8e43bead16f
ip -s link show enxf8e43bead16f
sudo ethtool -S enxf8e43bead16f
sudo tcpdump -i enxf8e43bead16f -nn -e
16. Clean experiment procedure
Step 1 — Laptop
sudo ip addr add 192.168.60.3/24 dev enxf8e43bead16f
Step 2 — Verify
sudo ethtool enxf8e43bead16f

Make sure:

Speed: 1000Mb/s
Link detected: yes
Step 3 — Start packet capture
sudo tcpdump -i enxf8e43bead16f -nn -e
Step 4 — Start Pktgen
sudo env LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu \
./build/app/pktgen \
-l 10-19 \
-n 4 \
-a 0000:d8:00.1 \
--socket-mem=0,1024 \
-- \
-m "[11:12].0"

Configure:

Source IP      = 192.168.60.1
Destination IP = 192.168.60.3
Source MAC     = b4:96:91:12:9d:46
Destination MAC= f8:e4:3b:ea:d1:6f
Packet size    = 1400
Tx Count       = Forever
Tx Rate        = 100%
Step 5 — Check Pktgen TX

Confirm:

Tx Pkts       increasing
Tx Max        increasing
Errors Tx     0
Step 6 — Check laptop RX
ip -s link show enxf8e43bead16f

Confirm:

RX packets increasing
RX errors = 0
RX dropped = 0
Step 7 — Confirm actual packets
sudo tcpdump -i enxf8e43bead16f -nn -e -c 10

You should see:

b4:96:91:12:9d:46 > f8:e4:3b:ea:d1:6f
192.168.60.1 > 192.168.60.3

This establishes the complete path:

Pktgen
  ↓
Ubuntu DPDK X540-AT2 TX
  ↓
10GbE physical link
  ↓
Laptop 1GbE NIC
  ↓
Laptop NIC RX
  ↓
Linux RX
  ↓
tcpdump

Most important conclusion from the current setup: the Ubuntu side is 10GbE-capable, but the laptop's enxf8e43bead16f link is 1Gbps, so you should not use this setup to evaluate 10Gbps reception. For a true 10Gbps experiment, the laptop must have a 10GbE NIC/adapter and a link that negotiates at 10,000 Mbps.
