### Reciever pkt/sec count:
* sar -n DEV 1 | grep --line-buffered_ <interface_name>
    - gives per second packet count from rx of recieving interface
    - sar works after driver before xdp program

### Interface ring buffer/queue (rx,tx) stats:
    ip -s link show <interface_name>

### To check no. of rx and tx:
    ls -d /sys/class/net/interface_name/queues/rx-*| wc -l

### Detail analysis of tx :
    sudo ethtool -S interface_name | grep q_tx_pkt_n

### Live and visual stats of tx and rx:
    bmon -b interface_name

### For checking hardware detail of nic card:
    lspci -vvv | grep Ethernet

### Checking processes:
    ps -all