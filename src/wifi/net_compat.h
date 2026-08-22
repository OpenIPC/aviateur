#pragma once

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #include <windows.h>

    // 确保没有全局 min/max 宏干扰
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif

    #include <io.h>
    #include <malloc.h>
    #include <errno.h>
    #include <time.h>
    #include <basetsd.h>

    #ifndef __attribute__
        #define __attribute__(x)
    #endif

    #include "cross/endian.h"

    #ifndef _SSIZE_T_DEFINED
    #define _SSIZE_T_DEFINED
    typedef SSIZE_T ssize_t;
    #endif

    #define MSG_DONTWAIT 0

    struct iovec {
        void *iov_base;
        size_t iov_len;
    };

    struct msghdr {
        void *msg_name;
        socklen_t msg_namelen;
        struct iovec *msg_iov;
        size_t msg_iovlen;
        void *msg_control;
        size_t msg_controllen;
        int msg_flags;
    };

    // --- 使用 wfb_ 前缀的内联函数，彻底避免命名冲突 ---

    inline int wfb_close(int fd) {
        return closesocket((SOCKET)fd);
    }

    #ifndef POLLIN
        #define POLLIN      0x0100
        #define POLLOUT     0x0010
        #define POLLERR     0x0001
        #define POLLHUP     0x0002
        #define POLLNVAL    0x0004
        struct pollfd {
            SOCKET fd;
            SHORT  events;
            SHORT  revents;
        };
    #endif

    inline int wfb_poll(struct pollfd* fds, unsigned long nfds, int timeout) {
        return WSAPoll((PWSAPOLLFD)fds, (ULONG)nfds, (INT)timeout);
    }

    inline int wfb_setsockopt(SOCKET s, int level, int optname, const void* optval, int optlen) {
        return ::setsockopt(s, level, optname, (const char*)optval, optlen);
    }

    inline ssize_t wfb_sendto(SOCKET s, const void* buf, size_t len, int flags, const struct sockaddr* to, int tolen) {
        return (ssize_t)::sendto(s, (const char*)buf, (int)len, flags, to, tolen);
    }

    static inline void wfb_aligned_free(void *p) {
        _aligned_free(p);
    }

    // --- POSIX 模拟 ---

    #ifndef CMSG_FIRSTHDR
        #define CMSG_FIRSTHDR(mhdr) (NULL)
    #endif
    #ifndef CMSG_NXTHDR
        #define CMSG_NXTHDR(mhdr, cmsg) (NULL)
    #endif
    #ifdef CMSG_DATA
        #undef CMSG_DATA
    #endif
    #define CMSG_DATA(cmsg) (NULL)
    #ifndef CMSG_SPACE
        #define CMSG_SPACE(length) (sizeof(struct cmsghdr) + (length))
    #endif
    #ifndef CMSG_LEN
        #define CMSG_LEN(length) (sizeof(struct cmsghdr) + (length))
    #endif

    inline ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
        DWORD bytesSent = 0;
        WSAMSG wsaMsg;
        wsaMsg.name = (LPSOCKADDR)msg->msg_name;
        wsaMsg.namelen = msg->msg_namelen;
        WSABUF *lpBuffers = (WSABUF *)alloca(msg->msg_iovlen * sizeof(WSABUF));
        for (size_t i = 0; i < msg->msg_iovlen; ++i) {
            lpBuffers[i].buf = (char *)msg->msg_iov[i].iov_base;
            lpBuffers[i].len = (ULONG)msg->msg_iov[i].iov_len;
        }
        wsaMsg.lpBuffers = lpBuffers;
        wsaMsg.dwBufferCount = (DWORD)msg->msg_iovlen;
        wsaMsg.Control.buf = (char *)msg->msg_control;
        wsaMsg.Control.len = (ULONG)msg->msg_controllen;
        wsaMsg.dwFlags = (DWORD)flags;
        GUID GuidSendMsg = WSAID_WSASENDMSG;
        LPFN_WSASENDMSG pfWSASendMsg = NULL;
        DWORD dwBytes = 0;
        if (WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidSendMsg, sizeof(GuidSendMsg),
                     &pfWSASendMsg, sizeof(pfWSASendMsg), &dwBytes, NULL, NULL) != 0) {
            return -1;
        }
        if (pfWSASendMsg(fd, &wsaMsg, wsaMsg.dwFlags, &bytesSent, NULL, NULL) != 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) errno = EAGAIN;
            else errno = err;
            return -1;
        }
        return (ssize_t)bytesSent;
    }

    inline ssize_t recvmsg(int fd, struct msghdr *msg, int flags) {
        DWORD bytesReceived = 0;
        WSAMSG wsaMsg;
        wsaMsg.name = (LPSOCKADDR)msg->msg_name;
        wsaMsg.namelen = msg->msg_namelen;
        WSABUF *lpBuffers = (WSABUF *)alloca(msg->msg_iovlen * sizeof(WSABUF));
        for (size_t i = 0; i < msg->msg_iovlen; ++i) {
            lpBuffers[i].buf = (char *)msg->msg_iov[i].iov_base;
            lpBuffers[i].len = (ULONG)msg->msg_iov[i].iov_len;
        }
        wsaMsg.lpBuffers = lpBuffers;
        wsaMsg.dwBufferCount = (DWORD)msg->msg_iovlen;
        wsaMsg.Control.buf = (char *)msg->msg_control;
        wsaMsg.Control.len = (ULONG)msg->msg_controllen;
        wsaMsg.dwFlags = (DWORD)flags;
        GUID GuidRecvMsg = WSAID_WSARECVMSG;
        LPFN_WSARECVMSG pfWSARecvMsg = NULL;
        DWORD dwBytes = 0;
        if (WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidRecvMsg, sizeof(GuidRecvMsg),
                     &pfWSARecvMsg, sizeof(pfWSARecvMsg), &dwBytes, NULL, NULL) != 0) {
            return -1;
        }
        if (pfWSARecvMsg(fd, &wsaMsg, &bytesReceived, NULL, NULL) != 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) errno = EAGAIN;
            else errno = err;
            return -1;
        }
        msg->msg_namelen = wsaMsg.namelen;
        msg->msg_controllen = wsaMsg.Control.len;
        msg->msg_flags = wsaMsg.dwFlags;
        return (ssize_t)bytesReceived;
    }

    typedef int sa_family_t;

    inline int posix_memalign(void **memptr, size_t alignment, size_t size) {
        void *mem = _aligned_malloc(size, alignment);
        if (mem) {
            *memptr = mem;
            return 0;
        }
        return ENOMEM;
    }

    #ifndef CLOCK_MONOTONIC
        #define CLOCK_MONOTONIC 1
    #endif

    #ifdef __cplusplus
    extern "C" {
    #endif
    inline int clock_gettime(int clk_id, struct timespec *tp) {
        static LARGE_INTEGER freq;
        static BOOL freq_init = FALSE;
        if (!freq_init) {
            QueryPerformanceFrequency(&freq);
            freq_init = TRUE;
        }
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        tp->tv_sec = count.QuadPart / freq.QuadPart;
        tp->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
        return 0;
    }
    #ifdef __cplusplus
    }
    #endif

    // PCAP stubs
    #ifndef lib_pcap_pcap_h
    #define lib_pcap_pcap_h
    #define lib_pcap_bpf_h

    #ifdef __cplusplus
    extern "C" {
    #endif

    #define PCAP_ERRBUF_SIZE 256
    #define DLT_IEEE802_11_RADIO 127
    typedef void pcap_t;
    struct pcap_pkthdr { struct timeval ts; unsigned int caplen; unsigned int len; };
    struct bpf_program { unsigned int bf_len; void *bf_insns; };
    inline pcap_t* pcap_create(const char *, char *) { return NULL; }
    inline int pcap_set_buffer_size(pcap_t *, int) { return -1; }
    inline int pcap_set_snaplen(pcap_t *, int) { return -1; }
    inline int pcap_set_promisc(pcap_t *, int) { return -1; }
    inline int pcap_set_timeout(pcap_t *, int) { return -1; }
    inline int pcap_set_immediate_mode(pcap_t *, int) { return -1; }
    inline int pcap_activate(pcap_t *) { return -1; }
    inline int pcap_setnonblock(pcap_t *, int, char *) { return -1; }
    inline int pcap_datalink(pcap_t *) { return -1; }
    inline int pcap_compile(pcap_t *, struct bpf_program *, const char *, int, unsigned int) { return -1; }
    inline int pcap_setfilter(pcap_t *, struct bpf_program *) { return -1; }
    inline void pcap_freecode(struct bpf_program *) {}
    inline int pcap_get_selectable_fd(pcap_t *) { return -1; }
    inline const unsigned char* pcap_next(pcap_t *, struct pcap_pkthdr *) { return NULL; }
    inline void pcap_close(pcap_t *) {}
    inline char* pcap_geterr(pcap_t *) { return (char*)"pcap not supported on Windows"; }

    #ifdef __cplusplus
    }
    #endif
    #endif

    #ifndef UNIX_PATH_MAX
        #define UNIX_PATH_MAX 108
        struct sockaddr_un {
            short sun_family;
            char sun_path[UNIX_PATH_MAX];
        };
    #endif

#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    #include <stdlib.h>

    static inline void wfb_aligned_free(void *p) {
        free(p);
    }

    static inline int wfb_close(int fd) {
        return close(fd);
    }

    static inline int wfb_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
        return poll(fds, nfds, timeout);
    }

    static inline int wfb_setsockopt(int s, int level, int optname, const void* optval, socklen_t optlen) {
        return setsockopt(s, level, optname, optval, optlen);
    }

    static inline ssize_t wfb_sendto(int s, const void* buf, size_t len, int flags, const struct sockaddr* to, socklen_t tolen) {
        return sendto(s, buf, len, flags, to, tolen);
    }
#endif
