#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <net/if.h>
#include <signal.h>
#include <time.h>

struct flow_key {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
};

static volatile int keep_running = 1;
void sig_handler(int sig) { keep_running = 0; }

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    int ifindex = if_nametoindex(argv[1]);
    signal(SIGINT, sig_handler);

    struct bpf_object *obj = bpf_object__open_file("xdp_flow_tracker.o", NULL);
    if (!obj || bpf_object__load(obj)) {
        printf("[-] Failed to load kernel object.\n");
        return 1;
    }

    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "xdp_packet_parser");
    struct bpf_link *link = bpf_program__attach_xdp(prog, ifindex);
    if (!link) {
        printf("[-] Failed to attach firewall.\n");
        return 1;
    }

    int map_fd = bpf_map__fd(bpf_object__find_map_by_name(obj, "flow_counter"));

    // Generate Dynamic Filename
    char filename[128];
    time_t start_time = time(NULL);
    struct tm *tm_info = localtime(&start_time);
    strftime(filename, sizeof(filename), "network_flows_%Y-%m-%d_%H-%M-%S.txt", tm_info);

    printf("[*] Attached to %s.\n", argv[1]);
    printf("[*] Appending 30-second batches to: %s\n", filename);
    printf("[*] Press Ctrl+C to stop safely.\n");

    while (keep_running) {
        sleep(30);
        if (!keep_running) break;

        FILE *file = fopen(filename, "a");
        if (!file) continue;

        time_t current_time = time(NULL);
        fprintf(file, "\n=== eBPF Flow Log | Timestamp: %ld ===\n", current_time);
        fprintf(file, ">>> 30-SECOND BATCH: FINAL TALLY BEFORE RESET <<<\n");
        fprintf(file, "SOURCE IP:PORT           DESTINATION IP:PORT      PACKETS\n");
        fprintf(file, "---------------------------------------------------------\n");

        struct flow_key key, next_key;
        int err = bpf_map_get_next_key(map_fd, NULL, &next_key);
        __u64 value;
        
        // Summary Trackers
        int active_flows = 0;
        __u64 total_packets = 0;
        
        // Top Talker Trackers
        __u64 top_packets = 0;
        char top_src_str[32] = "N/A";
        char top_dst_str[32] = "N/A";

        while (err == 0) {
            key = next_key;
            err = bpf_map_get_next_key(map_fd, &key, &next_key);

            bpf_map_lookup_elem(map_fd, &key, &value);
            
            char src[16], dst[16];
            inet_ntop(AF_INET, &key.saddr, src, sizeof(src));
            inet_ntop(AF_INET, &key.daddr, dst, sizeof(dst));

            // Print the clean, formatted row
            fprintf(file, "%-15s:%-8d %-15s:%-8d %llu\n", 
                    src, ntohs(key.sport), dst, ntohs(key.dport), value);

            // Check if this is the highest volume flow in the batch
            if (value > top_packets) {
                top_packets = value;
                snprintf(top_src_str, sizeof(top_src_str), "%s:%d", src, ntohs(key.sport));
                snprintf(top_dst_str, sizeof(top_dst_str), "%s:%d", dst, ntohs(key.dport));
            }

            active_flows++;
            total_packets += value;

            bpf_map_delete_elem(map_fd, &key);
        }

        fprintf(file, "---------------------------------------------------------\n");
        fprintf(file, "BATCH SUMMARY: %d Unique Flows | %llu Total Packets\n", active_flows, total_packets);
        
        // Only print Top Talker if there was actually traffic
        if (active_flows > 0) {
            fprintf(file, "TOP TALKER:    %s -> %s (%llu Packets)\n", top_src_str, top_dst_str, top_packets);
        }
        
        fclose(file);
        
        struct tm *log_time = localtime(&current_time);
        printf("[%02d:%02d:%02d] Logged %d flows. Top Talker sent %llu packets. Map reset.\n", 
               log_time->tm_hour, log_time->tm_min, log_time->tm_sec, active_flows, top_packets);
    }

    bpf_link__destroy(link);
    bpf_object__close(obj);
    printf("\n[*] Map deleted and firewall detached successfully.\n");
    return 0;
}