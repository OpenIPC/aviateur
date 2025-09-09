#pragma once

#if defined(__linux__) || defined(__CYGWIN__)

    #include <endian.h>

#elif defined(__APPLE__)

    #include <libkern/OSByteOrder.h>

    #define htobe16(x) OSSwapHostToBigInt16(x)
    #define htole16(x) OSSwapHostToLittleInt16(x)
    #define be16toh(x) OSSwapBigToHostInt16(x)
    #define le16toh(x) OSSwapLittleToHostInt16(x)

    #define htobe32(x) OSSwapHostToBigInt32(x)
    #define htole32(x) OSSwapHostToLittleInt32(x)
    #define be32toh(x) OSSwapBigToHostInt32(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)

    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define htole64(x) OSSwapHostToLittleInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
    #define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined(_WIN32)

    #include <winsock2.h>

    #define htobe16(x) htons(x)
    #define htole16(x) (x)
    #define be16toh(x) ntohs(x)
    #define le16toh(x) (x)

    #define htobe32(x) htonl(x)
    #define htole32(x) (x)
    #define be32toh(x) ntohl(x)
    #define le32toh(x) (x)

    #define htobe64(x) htonll(x)
    #define htole64(x) (x)
    #define be64toh(x) ntohll(x)
    #define le64toh(x) (x)

#else

    #error platform not supported

#endif
