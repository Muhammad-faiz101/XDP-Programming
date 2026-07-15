// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include <net/if.h>

static volatile bool running = true;

static void sigint_handler(int sig)
{
    running = false;
}

static int insert_ip(int map_fd, const char *ip_str)
{
    __u32 ip;

    if (inet_pton(AF_INET, ip_str, &ip) != 1) {
        fprintf(stderr, "Invalid IP: %s\n", ip_str);
        return -1;
    }

    if (bpf_map_update_elem(map_fd, NULL, &ip, BPF_ANY)) {
        fprintf(stderr, "Failed to insert %s : %s\n",
                ip_str, strerror(errno));
        return -1;
    }

    printf("Inserted %s\n", ip_str);
    return 0;
}

int main(int argc, char **argv)
{
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_map *map;

    int prog_fd;
    int map_fd;
    int ifindex;

    if (argc != 2) {
        printf("Usage: %s <ifname>\n", argv[0]);
        return 1;
    }

    ifindex = if_nametoindex(argv[1]);
    if (!ifindex) {
        perror("if_nametoindex");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    obj = bpf_object__open_file("bloom_filter_xdp.o", NULL);
    if (!obj) {
        fprintf(stderr, "Failed to open BPF object\n");
        return 1;
    }

    if (bpf_object__load(obj)) {
        fprintf(stderr, "Failed to load BPF object\n");
        return 1;
    }

    prog = bpf_object__find_program_by_name(obj, "xdp_bloom_demo");
    if (!prog) {
        fprintf(stderr, "Program not found\n");
        return 1;
    }

   

    map = bpf_object__find_map_by_name(obj, "bloom_filter");
    if (!map) {
        fprintf(stderr, "Bloom filter map not found\n");
        return 1;
    }

    map_fd = bpf_map__fd(map);

    // insert_ip(map_fd, "57.144.149.32");
    // insert_ip(map_fd, "172.64.148.235");
    insert_ip(map_fd, "151.101.194.152");
    insert_ip(map_fd, "151.101.66.152");
    insert_ip(map_fd, "151.101.2.152");
    insert_ip(map_fd, "151.101.130.152");
    insert_ip(map_fd, "8.8.8.8");
    // insert_ip(map_fd, "157.240.227.60");
    // insert_ip(map_fd, "35.190.80.1:");
    
    struct bpf_link *link;
    link = bpf_program__attach_xdp(prog, ifindex);

    if (!link) {
    fprintf(stderr, "Failed to attach XDP program\n");
    bpf_object__close(obj);
    return 1;
    }

    printf("XDP attached.\n");
    printf("Press Ctrl+C to exit.\n");

    while (running)
        sleep(1);

    bpf_link__destroy(link);
    bpf_object__close(obj);

    return 0;
}