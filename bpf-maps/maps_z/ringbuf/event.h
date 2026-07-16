// event.h

#ifndef __EVENT_H__
#define __EVENT_H__

#include <linux/types.h>

struct event {

    __u32 src_ip;
    __u32 dst_ip;

    __u16 src_port;
    __u16 dst_port;

    __u8 protocol;

    __u32 pkt_len;

};

#endif