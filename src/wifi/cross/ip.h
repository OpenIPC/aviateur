#pragma once

#include <cstdint>

struct iphdr {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t ihl : 4; // uint8_t, not unsigned int: MSVC would otherwise give the
    uint8_t version : 4; // bitfields their own 4-byte unit and shift every field
#elif __BYTE_ORDER == __BIG_ENDIAN
    uint8_t version : 4;
    uint8_t ihl : 4;
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

static_assert(sizeof(iphdr) == 20, "struct iphdr must match the 20-byte IPv4 header on every compiler");
