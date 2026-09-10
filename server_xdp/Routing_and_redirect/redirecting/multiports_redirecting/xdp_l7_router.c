#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif


/* =========================================================
 * LOGICAL INTERFACE ROLES
 * Actual ifindexes are populated from userspace.
 * ========================================================= */

#define ROLE_CLIENT  0    /* Server ens8f0 */
#define ROLE_F       1    /* Server ens7f0 */
#define ROLE_Z       2    /* Server ens7f1 */


/* =========================================================
 * DEVMAP TRANSMIT SLOTS
 * ========================================================= */

#define TX_TO_F       0
#define TX_TO_Z       1
#define TX_TO_CLIENT  2


/* =========================================================
 * MAC ADDRESS PAIR
 * ========================================================= */

struct mac_pair {
    __u8 dst_mac[ETH_ALEN];
    __u8 src_mac[ETH_ALEN];
};


/* =========================================================
 * MAP 1:
 * Logical role -> actual Linux interface index
 *
 *   0 -> ens8f0
 *   1 -> ens7f0
 *   2 -> ens7f1
 * ========================================================= */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);

    __type(key, __u32);
    __type(value, __u32);

} ifindex_map SEC(".maps");


/* =========================================================
 * MAP 2:
 * TX slot -> Ethernet source/destination MAC pair
 *
 *   slot 0 -> toward Laptop F
 *   slot 1 -> toward Laptop Z
 *   slot 2 -> toward Laptop A
 * ========================================================= */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);

    __type(key, __u32);
    __type(value, struct mac_pair);

} mac_map SEC(".maps");


/* =========================================================
 * MAP 3:
 * TX slot -> destination Linux ifindex
 *
 *   slot 0 -> ens7f0
 *   slot 1 -> ens7f1
 *   slot 2 -> ens8f0
 * ========================================================= */

struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 3);

    __type(key, __u32);
    __type(value, __u32);

} tx_port SEC(".maps");


/* =========================================================
 * DEBUG SEPARATOR
 * ========================================================= */

static __always_inline void debug_separator(void)
{
    bpf_printk("-----------------------------------------");
}


/* =========================================================
 * GET ACTUAL IFINDEX FOR A LOGICAL ROLE
 * ========================================================= */

static __always_inline int get_role_ifindex(
    __u32 role,
    __u32 *ifindex
)
{
    __u32 *p =
        bpf_map_lookup_elem(
            &ifindex_map,
            &role
        );

    if (!p) {

        bpf_printk(
            "get_role_ifindex: lookup FAILED role=%u",
            role
        );

        return -1;
    }

    *ifindex = *p;

    bpf_printk(
        "get_role_ifindex: role=%u -> ifindex=%u",
        role,
        *ifindex
    );

    return 0;
}


/* =========================================================
 * REWRITE ETHERNET MAC ADDRESSES
 * ========================================================= */

static __always_inline int set_eth_macs(
    struct ethhdr *eth,
    __u32 tx_slot
)
{
    struct mac_pair *macs =
        bpf_map_lookup_elem(
            &mac_map,
            &tx_slot
        );

    if (!macs) {

        bpf_printk(
            "set_eth_macs: lookup FAILED slot=%u",
            tx_slot
        );

        return -1;
    }

    bpf_printk(
        "set_eth_macs: rewriting MACs slot=%u",
        tx_slot
    );

    __builtin_memcpy(
        eth->h_dest,
        macs->dst_mac,
        ETH_ALEN
    );

    __builtin_memcpy(
        eth->h_source,
        macs->src_mac,
        ETH_ALEN
    );

    bpf_printk(
        "set_eth_macs: MAC rewrite SUCCESS slot=%u",
        tx_slot
    );

    return 0;
}


/* =========================================================
 * REWRITE MAC + REDIRECT THROUGH DEVMAP
 * ========================================================= */

static __always_inline int redirect_slot(struct ethhdr *eth,__u32 tx_slot)
{
    bpf_printk(
        "redirect_slot: requested slot=%u",
        tx_slot
    );

    if (set_eth_macs(eth, tx_slot) < 0) {

        bpf_printk(
            "redirect_slot: MAC rewrite FAILED -> DROP"
        );

        return XDP_DROP;
    }

    bpf_printk(
        "redirect_slot: bpf_redirect_map slot=%u",
        tx_slot
    );

    return bpf_redirect_map(
        &tx_port,
        tx_slot,
        0
    );
}


/* =========================================================
 * MAIN XDP PROGRAM
 * ========================================================= */

SEC("xdp")
int xdp_l7_router(struct xdp_md *ctx)
{
    void *data =
        (void *)(long)ctx->data;

    void *data_end =
        (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;


    /* =====================================================
     * 1. VALIDATE ETHERNET HEADER
     * ===================================================== */

    if ((void *)(eth + 1) > data_end) {

        bpf_printk(
            "xdp_l7_router: INVALID Ethernet header -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 2. GET INGRESS IFINDEX
     * ===================================================== */

    __u32 ingress =
        ctx->ingress_ifindex;

    bpf_printk(
        "xdp_l7_router: packet received ingress=%u",
        ingress
    );


    /* =====================================================
     * 3. GET SERVER INTERFACE INDEXES
     * ===================================================== */

    __u32 client_if = 0;
    __u32 f_if      = 0;
    __u32 z_if      = 0;

    if (get_role_ifindex(
            ROLE_CLIENT,
            &client_if
        ) < 0 ||

        get_role_ifindex(
            ROLE_F,
            &f_if
        ) < 0 ||

        get_role_ifindex(
            ROLE_Z,
            &z_if
        ) < 0) {

        bpf_printk(
            "xdp_l7_router: interface map lookup FAILED"
        );

        debug_separator();

        return XDP_DROP;
    }

    bpf_printk(
        "xdp_l7_router: CLIENT=%u F=%u Z=%u",
        client_if,
        f_if,
        z_if
    );


    /* =====================================================
     * 4. ETHERNET PROTOCOL
     * ===================================================== */

    __u16 proto =
        bpf_ntohs(eth->h_proto);

    bpf_printk(
        "xdp_l7_router: EtherType=0x%x",
        proto
    );


    /* =====================================================
     * 5. ARP
     *
     * Client -> F
     * F      -> Client
     *
     * Your static ARP entries are doing the neighbor
     * resolution, so the ARP frame itself is simply
     * redirected without MAC rewriting.
     * ===================================================== */

    if (eth->h_proto == bpf_htons(ETH_P_ARP)) {

        bpf_printk(
            "xdp_l7_router: ARP packet"
        );

        if (ingress == client_if) {

            bpf_printk(
                "xdp_l7_router: ARP CLIENT -> F"
            );

            int action =
                bpf_redirect_map(
                    &tx_port,
                    TX_TO_F,
                    0
                );

            debug_separator();

            return action;
        }

        if (ingress == f_if) {

            bpf_printk(
                "xdp_l7_router: ARP F -> CLIENT"
            );

            int action =
                bpf_redirect_map(
                    &tx_port,
                    TX_TO_CLIENT,
                    0
                );

            debug_separator();

            return action;
        }

        bpf_printk(
            "xdp_l7_router: ARP from Z/unknown -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 6. ONLY IPv4
     * ===================================================== */

    if (eth->h_proto != bpf_htons(ETH_P_IP)) {

        bpf_printk(
            "xdp_l7_router: non-IPv4 -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }

    bpf_printk(
        "xdp_l7_router: IPv4 packet"
    );


    /* =====================================================
     * 7. VALIDATE IPv4 HEADER
     * ===================================================== */

    struct iphdr *iph =
        (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end) {

        bpf_printk(
            "xdp_l7_router: INVALID IPv4 header -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 8. F -> CLIENT RETURN PATH
     *
     * HTTPS responses
     * DNS responses
     * ICMP replies
     * etc.
     *
     * F has already performed NAT/routing.
     *
     * Server only needs to perform L2 forwarding:
     *
     *       F -> ens7f0 -> Server -> ens8f0 -> A
     * ===================================================== */

    if (ingress == f_if) {

        bpf_printk(
            "xdp_l7_router: packet received from F"
        );

        bpf_printk(
            "xdp_l7_router: F -> CLIENT"
        );

        int action =
            redirect_slot(
                eth,
                TX_TO_CLIENT
            );

        bpf_printk(
            "xdp_l7_router: F -> CLIENT forwarding complete"
        );

        debug_separator();

        return action;
    }


    /* =====================================================
     * 9. Z IS PASSIVE
     *
     * Nothing should normally come back from Z.
     * ===================================================== */

    if (ingress == z_if) {

        bpf_printk(
            "xdp_l7_router: packet received from Z"
        );

        bpf_printk(
            "xdp_l7_router: Z is monitor-only -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 10. ONLY CLIENT PACKETS ARE CLASSIFIED
     * ===================================================== */

    if (ingress != client_if) {

        bpf_printk(
            "xdp_l7_router: UNKNOWN ingress=%u -> DROP",
            ingress
        );

        debug_separator();

        return XDP_DROP;
    }

    bpf_printk(
        "xdp_l7_router: packet received from CLIENT"
    );


    /* =====================================================
     * 11. IPv4 HEADER LENGTH
     * ===================================================== */

    __u32 ip_header_len =
        (__u32)iph->ihl * 4;

    if (ip_header_len < sizeof(*iph)) {

        bpf_printk(
            "xdp_l7_router: invalid IP header length=%u -> DROP",
            ip_header_len
        );

        debug_separator();

        return XDP_DROP;
    }

    if ((void *)iph + ip_header_len > data_end) {

        bpf_printk(
            "xdp_l7_router: IP header exceeds data_end -> DROP"
        );

        debug_separator();

        return XDP_DROP;
    }

    bpf_printk(
        "xdp_l7_router: IP header length=%u",
        ip_header_len
    );


    /* =====================================================
     * 12. ICMP
     *
     * Ping from A must go to F so F can route/NAT it.
     *
     *       A -> Server -> F -> Internet
     * ===================================================== */

    if (iph->protocol == IPPROTO_ICMP) {

        bpf_printk(
            "xdp_l7_router: ICMP CLIENT -> F"
        );

        int action =
            redirect_slot(
                eth,
                TX_TO_F
            );

        bpf_printk(
            "xdp_l7_router: ICMP forwarding complete"
        );

        debug_separator();

        return action;
    }


    /* =====================================================
     * 13. UDP
     *
     * DNS is normally UDP/53.
     * Forward UDP/53 to F.
     * Other UDP traffic is dropped.
     * ===================================================== */

    if (iph->protocol == IPPROTO_UDP) {

        struct udphdr *udph =
            (void *)iph + ip_header_len;

        if ((void *)(udph + 1) > data_end) {

            bpf_printk(
                "xdp_l7_router: INVALID UDP header -> DROP"
            );

            debug_separator();

            return XDP_DROP;
        }

        __u16 src_port =
            bpf_ntohs(udph->source);

        __u16 dst_port =
            bpf_ntohs(udph->dest);

        bpf_printk(
            "xdp_l7_router: UDP src=%u dst=%u",
            src_port,
            dst_port
        );


        /* DNS query */
        if (dst_port == 53) {

            bpf_printk(
                "xdp_l7_router: DNS UDP CLIENT -> F"
            );

            int action =
                redirect_slot(
                    eth,
                    TX_TO_F
                );

            bpf_printk(
                "xdp_l7_router: DNS UDP forwarding complete"
            );

            debug_separator();

            return action;
        }


        bpf_printk(
            "xdp_l7_router: unsupported UDP dst=%u -> DROP",
            dst_port
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 14. TCP
     * ===================================================== */

    if (iph->protocol == IPPROTO_TCP) {

        struct tcphdr *tcph =
            (void *)iph + ip_header_len;

        if ((void *)(tcph + 1) > data_end) {

            bpf_printk(
                "xdp_l7_router: INVALID TCP header -> DROP"
            );

            debug_separator();

            return XDP_DROP;
        }

        __u16 src_port =
            bpf_ntohs(tcph->source);

        __u16 dst_port =
            bpf_ntohs(tcph->dest);

        bpf_printk(
            "xdp_l7_router: TCP src=%u dst=%u",
            src_port,
            dst_port
        );


        /* =================================================
         * HTTP -> Z
         *
         * Connection is intentionally allowed to fail.
         * ================================================= */

        if (dst_port == 80) {

            bpf_printk(
                "xdp_l7_router: HTTP detected dst=80"
            );

            bpf_printk(
                "xdp_l7_router: CLIENT -> Z"
            );

            int action =
                redirect_slot(
                    eth,
                    TX_TO_Z
                );

            bpf_printk(
                "xdp_l7_router: HTTP forwarding complete"
            );

            debug_separator();

            return action;
        }


        /* =================================================
         * HTTPS -> F
         *
         * F performs:
         *
         *   enp2s0 -> wlp1s0
         *   IP forwarding
         *   MASQUERADE/NAT
         * ================================================= */

        if (dst_port == 443) {

            bpf_printk(
                "xdp_l7_router: HTTPS detected dst=443"
            );

            bpf_printk(
                "xdp_l7_router: CLIENT -> F"
            );

            int action =
                redirect_slot(
                    eth,
                    TX_TO_F
                );

            bpf_printk(
                "xdp_l7_router: HTTPS forwarding complete"
            );

            debug_separator();

            return action;
        }


        /* =================================================
         * DNS over TCP -> F
         * ================================================= */

        if (dst_port == 53) {

            bpf_printk(
                "xdp_l7_router: DNS TCP CLIENT -> F"
            );

            int action =
                redirect_slot(
                    eth,
                    TX_TO_F
                );

            bpf_printk(
                "xdp_l7_router: DNS TCP forwarding complete"
            );

            debug_separator();

            return action;
        }


        /* =================================================
         * Everything else
         * ================================================= */

        bpf_printk(
            "xdp_l7_router: unsupported TCP dst=%u -> DROP",
            dst_port
        );

        debug_separator();

        return XDP_DROP;
    }


    /* =====================================================
     * 15. UNKNOWN IP PROTOCOL
     * ===================================================== */

    bpf_printk(
        "xdp_l7_router: unsupported IP protocol=%u -> DROP",
        iph->protocol
    );

    debug_separator();

    return XDP_DROP;
}


char LICENSE[] SEC("license") = "GPL";