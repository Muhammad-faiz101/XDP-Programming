1. Disable IPv6

    Linux periodically sends IPv6 Neighbor Discovery packets.

    Temporarily disable it:

        sudo sysctl -w net.ipv6.conf.all.disable_ipv6=1
        sudo sysctl -w net.ipv6.conf.default.disable_ipv6=1
        sudo sysctl -w net.ipv6.conf.eth0.disable_ipv6=1

Replace eth0 with your interface.


2. Disable multicast
    sudo ip link set dev eth0 multicast off