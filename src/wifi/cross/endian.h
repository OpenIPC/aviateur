#pragma once

#include <cstdint>
#include <cstdio>

#ifdef __APPLE__
    #include <libkern/OSByteOrder.h>
#endif

/// Convert host int32 to big endian.
inline uint32_t htobe32(uint32_t host_32bits) {
    uint16_t test = 0x1;
    bool is_little_endian = *((uint8_t *)&test) == 0x1;

    // macOS: OSSwapHostToBigInt32(x)
    if (is_little_endian) {
        // Little to big endian
        return ((host_32bits & 0x000000FF) << 24) | ((host_32bits & 0x0000FF00) << 8) |
               ((host_32bits & 0x00FF0000) >> 8) | ((host_32bits & 0xFF000000) >> 24);
    }
    return host_32bits;
}

/// Convert big endian to host int64.
inline uint64_t be64toh(uint64_t big_endian_64bits) {
#if defined(_WIN32)
    return _byteswap_uint64(big_endian_64bits);
#elif defined(__APPLE__)
    return OSSwapBigToHostInt64(big_endian_64bits);
#else
    printf(
        "No implementation for big endian to little endian conversion, so no conversion is applied. Make sure this is "
        "what you want");
    return big_endian_64bits;
#endif
}

/// Convert big to host int32.
inline uint32_t be32toh(uint32_t big_endian_32bits) {
#if defined(_WIN32)
    return _byteswap_ulong(big_endian_32bits);
#elif defined(__APPLE__)
    return OSSwapBigToHostInt32(big_endian_32bits);
#else
    printf(
        "No implementation for big endian to little endian conversion, so no conversion is applied. Make sure this is "
        "what you want");
    return big_endian_32bits;
#endif
}

/// Convert big to host int16.
inline uint16_t be16toh(uint16_t big_endian_16bits) {
#if defined(_WIN32)
    return _byteswap_ushort(big_endian_16bits);
#elif defined(__APPLE__)
    return OSSwapBigToHostInt16(big_endian_16bits);
#else
    printf(
        "No implementation for big endian to little endian conversion, so no conversion is applied. Make sure this is "
        "what you want");
    return big_endian_16bits;
#endif
}
