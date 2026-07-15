#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

struct flow_key {
    __u32 src_ip;
    __u32 dst_ip;
    __u16 src_port;
    __u16 dst_port;
    __u8  protocol;
};

struct flow_value {
    __u64 packets;
};

static volatile bool keep_running = true;

static void sig_handler(int signo) {
    keep_running = false;
}

void dump_flows_to_file(int map_fd, const char *filename) {
    struct flow_key key = {}, next_key;
    struct flow_value value;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open output file");
        return;
    }

    // Write a clean table header inside the text file
    fprintf(f, "%-15s:%-5s -> %-15s:%-5s | Packets\n", "Src IP", "Port", "Dst IP", "Port");
    fprintf(f, "---------------------------------------------------------\n");

    // Loop through all keys in the hash map
    // Open the output file, then iterate over all entries in the BPF flow map using
    // bpf_map_get_next_key(). For each key, use bpf_map_lookup_elem() to retrieve the  
    // corresponding packet count, convert the IP addresses and ports to human-readable
    // format, and write the current flow statistics to the output file.
    while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {// ==0 means success & negative value for error
        if (bpf_map_lookup_elem(map_fd, &next_key, &value) == 0) {
            char src_ip_str[INET_ADDRSTRLEN];
            char dst_ip_str[INET_ADDRSTRLEN];

            struct in_addr src_addr = { .s_addr = next_key.src_ip };
            struct in_addr dst_addr = { .s_addr = next_key.dst_ip };
            
            // Translate Endianness & Convert to strings
            inet_ntop(AF_INET, &src_addr, src_ip_str, sizeof(src_ip_str));
            inet_ntop(AF_INET, &dst_addr, dst_ip_str, sizeof(dst_ip_str));

            fprintf(f, "%-15s:%-5d -> %-15s:%-5d | %llu\n",
                    src_ip_str, ntohs(next_key.src_port),
                    dst_ip_str, ntohs(next_key.dst_port),
                    value.packets);
        }
        key = next_key;
    }

    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface_name>\n", argv[0]);
        return 1;
    }

    const char *ifname = argv[1];
    unsigned int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        perror("Invalid interface name");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 1. Open and load the compiled BPF binary object
    struct bpf_object *obj = bpf_object__open_file("xdp_program.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "ERROR: Opening BPF object file failed\n");
        return 1;
    }

    if (bpf_object__load(obj)) {
        fprintf(stderr, "ERROR: Loading BPF object into kernel failed\n");
        bpf_object__close(obj);
        return 1;
    }

    // 2. Find our XDP program block
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "xdp_flow_counter");
    if (!prog) {
        fprintf(stderr, "ERROR: Could not find XDP program section\n");
        bpf_object__close(obj);
        return 1;
    }

    // 3. Link/Attach program onto the chosen Network Card interface
    int prog_fd = bpf_program__fd(prog);
    if (bpf_xdp_attach(ifindex, prog_fd, 0, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to attach XDP program to %s\n", ifname);
        bpf_object__close(obj);
        return 1;
    }

    // 4. Extract the active descriptor handling our Hash map
    int map_fd = bpf_object__find_map_fd_by_name(obj, "flow_counter");
    if (map_fd < 0) {
        fprintf(stderr, "ERROR: Map 'flow_counter' not found\n");
        goto cleanup;
    }

    printf("XDP program successfully attached to %s. Tracking live TCP traffic...\n", ifname);
    printf("Writing updates to 'flows.txt' every second. Press Ctrl+C to terminate cleanly.\n");

    // 5. Monitor Loop
    while (keep_running) {
        dump_flows_to_file(map_fd, "flows.txt");
        sleep(1);
    }

    printf("\nStopping gracefully... Detaching XDP hook.\n");

cleanup:
    // 6. Complete clean teardown
    bpf_xdp_detach(ifindex, 0, NULL);
    bpf_object__close(obj);
    return 0;
}