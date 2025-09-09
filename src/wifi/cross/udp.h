#pragma once

#include <cstdint>

#ifdef __FAVOR_BSD

struct udphdr {
    int16_t uh_sport; /* source port */
    int16_t uh_dport; /* destination port */
    int16_t uh_ulen;  /* udp length */
    int16_t uh_sum;   /* udp checksum */
};

#else

struct udphdr {
    int16_t source;
    int16_t dest;
    int16_t len;
    int16_t check;
};
#endif
