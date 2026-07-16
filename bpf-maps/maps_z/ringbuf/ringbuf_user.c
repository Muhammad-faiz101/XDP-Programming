// ringbuf_user.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "event.h"

static volatile int running = 1;

/* Ctrl+C handler */
static void sig_handler(int signo)
{
    running = 0;
}

/*---------------------------------------------------------
 * Callback Function
 *
 * Called every time the kernel submits an event.
 *--------------------------------------------------------*/
static int handle_event(void *ctx, void *data, size_t data_sz)
{
    struct event *e = data;

    struct in_addr src, dst;
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];

    src.s_addr = e->src_ip;
    dst.s_addr = e->dst_ip;

    inet_ntop(AF_INET, &src, src_ip, sizeof(src_ip));
    inet_ntop(AF_INET, &dst, dst_ip, sizeof(dst_ip));

    printf("\n=========================================\n");

    printf("Source IP        : %s\n", src_ip);
    printf("Destination IP   : %s\n", dst_ip);

    printf("Source Port      : %u\n", e->src_port);
    printf("Destination Port : %u\n", e->dst_port);

    printf("Protocol         : ");

    switch (e->protocol) {

    case IPPROTO_TCP:
        printf("TCP\n");
        break;

    case IPPROTO_UDP:
        printf("UDP\n");
        break;

    case IPPROTO_ICMP:
        printf("ICMP\n");
        break;

    default:
        printf("%u\n", e->protocol);
    }

    printf("Packet Length    : %u bytes\n", e->pkt_len);

    printf("=========================================\n");

    return 0;
}

int main(int argc, char **argv)
{
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_map *map;
    struct ring_buffer *rb = NULL;

    int prog_fd;
    int map_fd;
    int ifindex;

    if (argc != 3) {

        printf("Usage:\n");

        printf("%s <bpf_object> <interface>\n", argv[0]);

        return 1;
    }

    ifindex = if_nametoindex(argv[2]);

    if (!ifindex) {

        perror("if_nametoindex");

        return 1;
    }

    /*--------------------------------------
     * Open BPF Object
     *-------------------------------------*/
    obj = bpf_object__open_file(argv[1], NULL);

    if (!obj) {

        printf("Failed to open BPF object\n");

        return 1;
    }

    /*--------------------------------------
     * Load BPF Object
     *-------------------------------------*/
    if (bpf_object__load(obj)) {

        printf("Failed to load BPF object\n");

        return 1;
    }

    /*--------------------------------------
     * Get XDP Program
     *-------------------------------------*/
    prog = bpf_object__next_program(obj, NULL);

    prog_fd = bpf_program__fd(prog);

    /*--------------------------------------
     * Attach XDP
     *-------------------------------------*/
    if (bpf_xdp_attach(ifindex,
                       prog_fd,
                       XDP_FLAGS_SKB_MODE,
                       NULL)) {

        printf("Failed to attach XDP program\n");

        return 1;
    }

    printf("XDP Program Attached\n");

    /*--------------------------------------
     * Find Ring Buffer Map
     *-------------------------------------*/
    map = bpf_object__find_map_by_name(obj, "events");

    if (!map) {

        printf("Ring Buffer map not found\n");

        return 1;
    }

    map_fd = bpf_map__fd(map);

    /*--------------------------------------
     * Create Ring Buffer Reader
     *-------------------------------------*/
    rb = ring_buffer__new(map_fd,
                          handle_event,
                          NULL,
                          NULL);

    if (!rb) {

        printf("Failed to create Ring Buffer\n");

        return 1;
    }

    signal(SIGINT, sig_handler);

    printf("\nListening for packets...\n");

    /*--------------------------------------
     * Poll Ring Buffer
     *-------------------------------------*/
    while (running) {

        ring_buffer__poll(rb, 100);
    }

    printf("\nDetaching XDP...\n");

    bpf_xdp_detach(ifindex,
                   XDP_FLAGS_SKB_MODE,
                   NULL);

    ring_buffer__free(rb);

    bpf_object__close(obj);

    printf("Done\n");

    return 0;
}