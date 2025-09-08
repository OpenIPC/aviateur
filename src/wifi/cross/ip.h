#pragma once

#include <cstdint>

struct iphdr {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int ihl : 4;
    unsigned int version : 4;
#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned int version : 4;
    unsigned int ihl : 4;
#else
    #error "Please fix <bits/endian.h>"
#endif
    int8_t tos;
    int16_t tot_len;
    int16_t id;
    int16_t frag_off;
    int8_t ttl;
    int8_t protocol;
    int16_t check;
    int32_t saddr;
    int32_t daddr;
};
