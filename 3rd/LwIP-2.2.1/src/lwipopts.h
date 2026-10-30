#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#ifdef _WIN32
#include <basetsd.h>
#ifndef SSIZE_MAX
#define SSIZE_MAX _I64_MAX
#endif
#define LWIP_NO_UNISTD_H 1
#endif

#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0

#define LWIP_CALLBACK_API       1
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

#define MEM_ALIGNMENT           4
#define MEM_SIZE                (16 * 1024)
#define MEMP_NUM_PBUF           64
#define MEMP_NUM_TCP_PCB        16
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG        32

#define LWIP_ARP                0
#define LWIP_ETHERNET           0
#define LWIP_IPV4               1
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_ICMP               1

#define TCP_MSS                 1460
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)

#endif /* LWIPOPTS_H */
