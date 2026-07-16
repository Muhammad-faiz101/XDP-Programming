#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <linux/if_link.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>


static volatile int running = 1;


static void sig_handler(int sig)
{
    running = 0;
}


int main(int argc, char **argv)
{
    struct bpf_object *obj;
    struct bpf_program *prog;

    struct bpf_map *outer_map;
    struct bpf_map *inner_template;


    int prog_fd;
    int ifindex;

    int outer_map_fd;
    int inner_map_fd;

    __u32 index = 0;


    if (argc != 3) {

        printf("Usage: %s <bpf_object> <interface>\n", argv[0]);
        return 1;
    }


    signal(SIGINT, sig_handler);



    /*
     * Open kernel object
     */
    obj = bpf_object__open_file(argv[1], NULL);

    if (!obj) {

        printf("Failed to open BPF object\n");
        return 1;
    }



    /*
     * Load BPF program and maps
     */
    if (bpf_object__load(obj)) {

        printf("Failed to load BPF object\n");
        return 1;
    }



    /*
     * Find XDP program
     */
    prog =
    bpf_object__find_program_by_name(obj,
                                     "map_in_map_demo");


    if (!prog) {

        printf("Cannot find XDP program\n");
        return 1;
    }



    prog_fd = bpf_program__fd(prog);



    /*
     * Interface index
     */
    ifindex = if_nametoindex(argv[2]);

    if (!ifindex) {

        printf("Invalid interface\n");
        return 1;
    }



    /*
     * Attach XDP program
     * Generic/SKB mode
     */
    if (bpf_xdp_attach(ifindex,
                       prog_fd,
                       XDP_FLAGS_SKB_MODE,
                       NULL)) {


        printf("Failed to attach XDP\n");
        return 1;
    }



    printf("XDP attached on %s\n", argv[2]);



    /*
     * Get map objects
     */
    outer_map =
    bpf_object__find_map_by_name(obj,
                                 "outer_map");


    inner_template =
    bpf_object__find_map_by_name(obj,
                                 "inner_map");



    if (!outer_map || !inner_template) {

        printf("Cannot find maps\n");
        return 1;
    }



    outer_map_fd =
    bpf_map__fd(outer_map);



    /*
     * Create real inner map
     *
     * Using the kernel template
     */
    inner_map_fd =
    bpf_map_create(
        bpf_map__type(inner_template),
        "inner_runtime_map",
        bpf_map__key_size(inner_template),
        bpf_map__value_size(inner_template),
        bpf_map__max_entries(inner_template),
        NULL);



    if (inner_map_fd < 0) {

        printf("Failed creating inner map\n");
        return 1;
    }



    /*
     * Put inner map inside outer map
     *
     * outer_map[0] = inner_map
     */
    if (bpf_map_update_elem(
            outer_map_fd,
            &index,
            &inner_map_fd,
            BPF_ANY)) {


        printf("Failed inserting inner map\n");
        return 1;
    }



    printf("Inner map inserted into outer map\n");



    /*
     * Read counters
     */
    while (running) {


        sleep(5);


        printf("\n========== Counters ==========\n");


        __u32 key;
        __u32 next_key;


        /*
         * First key
         */
        if (bpf_map_get_next_key(
                inner_map_fd,
                NULL,
                &next_key)) {


            printf("No packets yet\n");
            continue;
        }



        while (1) {


            __u64 value;


            if (!bpf_map_lookup_elem(
                    inner_map_fd,
                    &next_key,
                    &value)) {


                struct in_addr addr;

                addr.s_addr = next_key;


                printf("%s -> %llu packets\n",
                       inet_ntoa(addr),
                       (unsigned long long)value);
            }



            key = next_key;



            if (bpf_map_get_next_key(
                    inner_map_fd,
                    &key,
                    &next_key)) {

                break;
            }

        }


        printf("==============================\n");

    }



    /*
     * Cleanup
     */
    bpf_xdp_detach(ifindex,
                   XDP_FLAGS_SKB_MODE,
                   NULL);


    close(inner_map_fd);

    bpf_object__close(obj);


    printf("Detached\n");


    return 0;
}