#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #include <windows.h>
    #include <io.h>
    #include <malloc.h>
    #include <errno.h>
    #include <time.h>

    #include "cross/endian.h"

    // Fix for missing types/constants on Windows
    typedef int ssize_t;

    #define MSG_DONTWAIT 0

    // iovec and msghdr for Windows
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

    // sendmsg implementation for Windows using WSASendMsg
    inline ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
        DWORD bytesSent = 0;
        WSAMSG wsaMsg;
        wsaMsg.name = (LPSOCKADDR)msg->msg_name;
        wsaMsg.namelen = msg->msg_namelen;

        // Convert iovec to WSABUF
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

        // Get function pointer for WSASendMsg
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

    // recvmsg implementation for Windows using WSARecvMsg
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

    #define poll WSAPoll
    #define close closesocket

    inline int posix_memalign(void **memptr, size_t alignment, size_t size) {
        void *mem = _aligned_malloc(size, alignment);
        if (mem) {
            *memptr = mem;
            return 0;
        }
        return ENOMEM;
    }

    // In rx.cpp, free() is used for fragments allocated by posix_memalign.
    // On Windows, we must use _aligned_free.
    #define free(p) _aligned_free(p)

    // clock_gettime for Windows
    #define CLOCK_MONOTONIC 1
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

    // Windows doesn't have sys/un.h
    struct sockaddr_un {
        short sun_family;
        char sun_path[108];
    };

    // pcap stubs for rx.cpp to compile on Windows without linking pcap
    #define PCAP_ERRBUF_SIZE 256
    #define DLT_IEEE802_11_RADIO 127
    typedef void pcap_t;
    struct pcap_pkthdr {
        struct timeval ts;
        unsigned int caplen;
        unsigned int len;
    };
    struct bpf_program {
        unsigned int bf_len;
        void *bf_insns;
    };
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

#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    #include <stdlib.h>
#endif
