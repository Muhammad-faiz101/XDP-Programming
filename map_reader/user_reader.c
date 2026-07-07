#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <net/if.h>
#include <signal.h>

// 1. MUST MATCH THE KERNEL EXACTLY
struct flow_key {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
};

// Global flag to handle Ctrl+C cleanly
static volatile int keep_running = 1;
void sig_handler(int sig) { keep_running = 0; }

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    const char *ifname = argv[1];
    int ifindex = if_nametoindex(ifname);
    if (!ifindex) {
        printf("[-] Error: Invalid interface '%s'\n", ifname);
        return 1;
    }
    
    signal(SIGINT, sig_handler);

    // 2. Load and Attach 
    struct bpf_object *obj = bpf_object__open_file("xdp_counter.o", NULL);
    if (bpf_object__load(obj)) {
        printf("[-] Failed to load object file.\n");
        return 1;
    }
    
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "xdp_packet_parser");
    struct bpf_link *link = bpf_program__attach_xdp(prog, ifindex);
    if (libbpf_get_error(link)) {
        printf("[-] Failed to attach XDP program.\n");
        return 1;
    }
    
    printf("[*] Attached to %s. Gathering full flow data...\n", ifname);

    // 3. Get Map File Descriptor (Matches your new map name)
    struct bpf_map *map = bpf_object__find_map_by_name(obj, "flow_counter");
    int map_fd = bpf_map__fd(map);

    printf("[*] Writing kernel memory to 'network_flows.txt' every 2 seconds.\n");
    printf("[*] Press Ctrl+C to stop.\n");

    // 4. The Extraction Loop
    while (keep_running) {
        FILE *file = fopen("network_flows.txt", "w");
        if (!file) {
            printf("[-] Failed to open file.\n");
            break;
        }

        fprintf(file, "=== XDP Live Full Flow Counter ===\n");
        fprintf(file, "%-21s | %-21s | %s\n", "SOURCE", "DESTINATION", "PACKETS");
        fprintf(file, "----------------------+-----------------------+---------\n");

        struct flow_key key = {}, next_key;
        __u64 value;

        int err = bpf_map_get_next_key(map_fd, NULL, &next_key);
        
        while (err == 0) {
            bpf_map_lookup_elem(map_fd, &next_key, &value);

            // Create separate buffers for source and dest IP strings
            char src_ip_str[INET_ADDRSTRLEN];
            char dst_ip_str[INET_ADDRSTRLEN];

            // Safely convert network byte IPs to strings
            inet_ntop(AF_INET, &next_key.saddr, src_ip_str, sizeof(src_ip_str));
            inet_ntop(AF_INET, &next_key.daddr, dst_ip_str, sizeof(dst_ip_str));

            // Format the final output: IP:PORT
            fprintf(file, "%-15s:%-5d | %-15s:%-5d | %llu\n", 
                    src_ip_str, ntohs(next_key.sport), 
                    dst_ip_str, ntohs(next_key.dport), 
                    value);

            key = next_key;
            err = bpf_map_get_next_key(map_fd, &key, &next_key);
        }

        fclose(file);
        sleep(2);
    }

    // 5. Cleanup
    printf("\n[*] Detaching XDP program and exiting...\n");
    bpf_link__destroy(link);
    bpf_object__close(obj);
    
    return 0;
}