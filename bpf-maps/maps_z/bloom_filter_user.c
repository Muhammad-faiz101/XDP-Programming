// bloom_filter_user.c

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <bpf/bpf.h>

int main(int argc, char *argv[])
{
    int map_fd;
    char ip_str[32];
    __u32 ip;

    if (argc != 2) {
        printf("Usage: %s <map_id>\n", argv[0]);
        return 1;
    }

    map_fd = bpf_map_get_fd_by_id(atoi(argv[1]));
    if (map_fd < 0) {
        perror("bpf_map_get_fd_by_id");
        return 1;
    }

    while (1) {

        int choice;

        printf("\n");
        printf("1. Insert IP\n");
        printf("2. Check IP\n");
        printf("3. Exit\n");
        printf("Choice: ");

        scanf("%d", &choice);

        if (choice == 3)
            break;

        printf("Enter IPv4 Address: ");
        scanf("%31s", ip_str);

        if (inet_pton(AF_INET, ip_str, &ip) != 1) {
            printf("Invalid IP Address\n");
            continue;
        }

        if (choice == 1) {

            if (bpf_map_update_elem(map_fd, NULL, &ip, BPF_ANY) == 0)
                printf("Inserted into Bloom Filter\n");
            else
                perror("Insert Failed");

        }
        else if (choice == 2) {

            if (bpf_map_lookup_elem(map_fd, NULL, &ip) == 0)
                printf("Possibly Present\n");
            else
                printf("Definitely Not Present\n");
        }
    }

    return 0;
}