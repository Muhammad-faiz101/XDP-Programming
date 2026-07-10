#include<bpf/libbpf.h> //maps
#include<bpf/bpf.h>
#include<errno.h> //error msgs
#include<unistd.h> // for close()

int create_hash_maps(void){
    int fd = bpf_map_create ( //wrapper for command BPF_MAP_CREATE
        BPF_MAP_TYPE_HASH,
        "b_hash", //name
        sizeof(int), //key size
        sizeof(long), //value size
        80, //max entr.
        NULL //map flags
    );
    if (fd < 0)
    {
        fprintf(stderr, "failed to create hash map: %s\n", strerror(errno));
    }
    return fd;
}

int main(){
    int fd = create_hash_maps();
    if (fd>=0)
    {
        printf("Hash map is created with fd: %d\n", fd);
        close(fd); //must when fd; prevent leaks + rsrc mngmnt
    }
    return 0;
}
