#include "tx_frame.h"

#include <sys/poll.h>

#ifdef __linux__
    #include <linux/ip.h>
    #include <linux/random.h>
    #include <linux/udp.h>
    #include <sys/ioctl.h>
#else
    #include "../cross/ip.h"
    #include "../cross/udp.h"
#endif

#include <cinttypes>
#include <cstring>

TxFrame::TxFrame(const bool tun_enabled) {
    tun_enabled_ = tun_enabled;
}

TxFrame::~TxFrame() = default;

void TxFrame::stop() {
    shouldStop_ = true;
}

uint32_t TxFrame::extractRxqOverflow(struct msghdr *msg) {
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(msg, cmsg)) {

        #ifdef __linux__
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_RXQ_OVFL) {
            uint32_t val = 0;
            std::memcpy(&val, CMSG_DATA(cmsg), sizeof(val));
            return val;
        }
        #else
        if (cmsg->cmsg_level == SOL_SOCKET) {
            uint32_t val = 0;
            std::memcpy(&val, CMSG_DATA(cmsg), sizeof(val));
            return val;
        }
        #endif
    }
    return 0;
}

int TxFrame::open_udp_socket_for_rx(int port, int buf_size) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::runtime_error(string_format("Unable to open socket: %s", std::strerror(errno)));
    }

    // Allow resuing address
    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Set receive timeout to 500ms
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        close(fd);
        throw std::runtime_error(string_format("Unable to set socket timeout: %s", std::strerror(errno)));
    }

    if (buf_size) {
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
            close(fd);
            throw std::runtime_error(string_format("Unable to set requested buffer size: %s", std::strerror(errno)));
        }

        int actual_buf_size = 0;
        socklen_t optlen = sizeof(actual_buf_size);
        getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actual_buf_size, &optlen);
        if (actual_buf_size < buf_size * 2) {
            // Linux doubles the value we set
            fprintf(stderr, "Warning: requested rx buffer size %d but got %d\n", buf_size, actual_buf_size / 2);
        }
    }

    struct sockaddr_in saddr = {};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<struct sockaddr *>(&saddr), sizeof(saddr)) < 0) {
        close(fd);
        throw std::runtime_error(string_format("Unable to bind to port %d: %s", port, std::strerror(errno)));
    }

    return fd;
}

uint16_t inet_csum(const void *buf, size_t hdr_len) {
    unsigned long sum = 0;

    const auto *ip1 = (const uint16_t *)buf;
    while (hdr_len > 1) {
        sum += *ip1++;
        if (sum & 0x80000000) sum = (sum & 0xFFFF) + (sum >> 16);
        hdr_len -= 2;
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    return ~sum;
}

void TxFrame::dataSource(std::shared_ptr<Transmitter> &transmitter,
                         std::vector<int> &rxFds,
                         int fecTimeout,
                         bool mirror,
                         int logInterval) {
    int nfds = static_cast<int>(rxFds.size());
    if (nfds <= 0) {
        throw std::runtime_error("dataSource: no valid rx sockets");
    }

    // Set timeout on all sockets
    for (int fd : rxFds) {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error(string_format("Unable to set socket timeout: %s", std::strerror(errno)));
        }
    }

    std::vector<pollfd> fds(nfds);
    for (int i = 0; i < nfds; ++i) {
        fds[i].fd = rxFds[i];
        fds[i].events = POLLIN;
    }

    uint64_t sessionKeyAnnounceTs = 0;
    uint32_t rxqOverflowCount = 0;
    uint64_t logSendTs = 0;
    uint64_t fecCloseTs = (fecTimeout > 0) ? get_time_ms() + fecTimeout : 0;

    // Stats counters
    uint32_t countPFecTimeouts = 0;
    uint32_t countPIncoming = 0;
    uint32_t countBIncoming = 0;
    uint32_t countPInjected = 0;
    uint32_t countBInjected = 0;
    uint32_t countPDropped = 0;
    uint32_t countPTruncated = 0;

    int startFdIndex = 0;

    while (true) {
        if (shouldStop_) {
            printf("TxFrame: stopping main loop");
            break;
        }

        uint64_t curTs = get_time_ms();
        int pollTimeout = 0;
        if (curTs < logSendTs) {
            pollTimeout = static_cast<int>(logSendTs - curTs);
        }

        if (fecTimeout > 0) {
            int ft = static_cast<int>(fecCloseTs > curTs ? fecCloseTs - curTs : 0);
            if (pollTimeout == 0 || ft < pollTimeout) {
                pollTimeout = ft;
            }
        }

        int rc = poll(fds.data(), nfds, pollTimeout);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            throw std::runtime_error(string_format("poll error: %s", std::strerror(errno)));
        }

        // Logging at intervals
        curTs = get_time_ms();
        if (curTs >= logSendTs) {
            transmitter->dumpStats(stdout, curTs, countPInjected, countPDropped, countBInjected);

            // std::fprintf(stdout,
            //              "%" PRIu64 "\tPKT\t%u:%u:%u:%u:%u:%u:%u\n",
            //              curTs,
            //              countPFecTimeouts,
            //              countPIncoming,
            //              countBIncoming,
            //              countPInjected,
            //              countBInjected,
            //              countPDropped,
            //              countPTruncated);
            // std::fflush(stdout);

            if (countPDropped) {
                std::fprintf(stderr, "%u packets dropped\n", countPDropped);
            }
            if (countPTruncated) {
                std::fprintf(stderr, "%u packets truncated\n", countPTruncated);
            }

            // Reset counters
            countPFecTimeouts = 0;
            countPIncoming = 0;
            countBIncoming = 0;
            countPInjected = 0;
            countBInjected = 0;
            countPDropped = 0;
            countPTruncated = 0;
            logSendTs = curTs + logInterval;
        }

        if (rc == 0) {
            // Timed out
            if (fecTimeout > 0 && (curTs >= fecCloseTs)) {
                // Send a FEC-only to close block if block is open
                if (!transmitter->sendPacket(nullptr, 0, WFB_PACKET_FEC_ONLY)) {
                    ++countPFecTimeouts;
                }
                fecCloseTs = get_time_ms() + fecTimeout;
            }
            continue;
        }

        // We have events
        int i = startFdIndex;
        for (startFdIndex = 0; rc > 0; i++) {
            pollfd &pfd = fds[static_cast<size_t>(i % nfds)];
            if (pfd.revents & (POLLERR | POLLNVAL)) {
                throw std::runtime_error(string_format("socket error: %s", std::strerror(errno)));
            }

            if (pfd.revents & POLLIN) {
                --rc;

                // Mirror or single output selection
                transmitter->selectOutput(mirror ? -1 : (i % nfds));

                while (true) {
                    if (shouldStop_) {
                        printf("TxFrame: stopping polling loop");
                        break;
                    }

                    uint8_t buf[MAX_PAYLOAD_SIZE + 1] = {};

                    uint8_t cmsgbuf[CMSG_SPACE(sizeof(uint32_t))] = {};

                    iovec iov = {};
                    iov.iov_base = buf;
                    iov.iov_len = sizeof(buf);

                    msghdr msg = {};
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1;
                    msg.msg_control = cmsgbuf;
                    msg.msg_controllen = sizeof(cmsgbuf);

                    ssize_t rsize = recvmsg(pfd.fd, &msg, 0);
                    if (rsize < 0) {
                        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != ETIMEDOUT) {
                            continue;
                            throw std::runtime_error(string_format("Error receiving packet: %s", std::strerror(errno)));
                        }
                        break;
                    }

                    // Incoming stats
                    ++countPIncoming;
                    countBIncoming += static_cast<uint32_t>(rsize);

                    if (rsize > static_cast<ssize_t>(MAX_PAYLOAD_SIZE)) {
                        rsize = MAX_PAYLOAD_SIZE;
                        ++countPTruncated;
                    }

                    uint32_t curOverflow = extractRxqOverflow(&msg);
                    if (curOverflow != rxqOverflowCount) {
                        uint32_t diff = (curOverflow - rxqOverflowCount);
                        countPDropped += diff;
                        countPIncoming += diff; // All these overflows are potential incoming
                        rxqOverflowCount = curOverflow;
                    }

                    // Possibly re-announce session key
                    uint64_t nowTs = get_time_ms();
                    if (nowTs >= sessionKeyAnnounceTs) {
                        transmitter->sendSessionKey();
                        sessionKeyAnnounceTs = nowTs + SESSION_KEY_ANNOUNCE_MSEC;
                    }

                    // fixme: should move before size check

                    // Craft IP packets manually.
                    if (!tun_enabled_) {
                        int iphdr_len = sizeof(struct iphdr);
                        int udphdr_len = sizeof(struct udphdr);

                        // Packet size
                        size_t packet_size = 2 + iphdr_len + udphdr_len + rsize;

                        // Packet
                        auto packet = std::vector<unsigned char>(packet_size, 0);

                        uint16_t net_packet_size = htons(packet_size - 2);
                        memcpy(packet.data(), &net_packet_size, 2);

                        static int packet_id = 0;

                        // IP header
                        auto *ip = (struct iphdr *)(packet.data() + 2);
                        ip->saddr = inet_addr("10.5.0.1");
                        ip->daddr = inet_addr("10.5.0.10");
                        ip->ihl = 5;
                        ip->version = 4;
                        ip->tos = 0;
                        ip->tot_len = htons(packet_size - 2);
                        ip->id = htons(packet_id++);
                        ip->frag_off = 0;
                        ip->ttl = 64;
                        ip->protocol = IPPROTO_UDP;
                        ip->check = 0; // Will be calculated later

                        // UDP header
                        auto *udp = (struct udphdr *)(ip + 1);
                        udp->source = htons(54321); // Doesn't matter
                        udp->dest = htons(9999);
                        udp->len = htons(udphdr_len + rsize);
                        udp->check = 0;

                        ip->check = inet_csum((unsigned short *)ip, iphdr_len);

                        // Payload
                        uint8_t *payload_buf = (uint8_t *)(udp + 1);
                        memcpy(payload_buf, buf, rsize);

                        // Forward packet
                        transmitter->sendPacket(packet.data(), packet_size, 0);
                    } else {
                        // Forward packet
                        transmitter->sendPacket(buf, static_cast<size_t>(rsize), 0);
                    }

                    // If we've hit a log boundary inside the same poll, break to flush stats
                    if (nowTs >= logSendTs) {
                        startFdIndex = i % nfds;
                        break;
                    }
                }
            }
        }

        // Reset FEC timer if data arrived
        if (fecTimeout > 0) {
            fecCloseTs = get_time_ms() + fecTimeout;
        }
    }
}

void TxFrame::run(Rtl8812aDevice *rtlDevice, TxArgs *arg) {
    // Decide if using VHT
    if (arg->bandwidth >= 80) {
        arg->vht_mode = true;
    }

    // Radiotap header preparation
    std::unique_ptr<uint8_t[]> rtHeader;
    size_t rtHeaderLen = 0;
    uint8_t frameType = FRAME_TYPE_RTS;

    // Construct the appropriate radiotap header (HT vs. VHT)
    if (!arg->vht_mode) {
        // HT mode
        uint8_t flags = 0;
        switch (arg->bandwidth) {
            case 10:
            case 20:
                flags |= IEEE80211_RADIOTAP_MCS_BW_20;
                break;
            case 40:
                flags |= IEEE80211_RADIOTAP_MCS_BW_40;
                break;
            default:
                throw std::runtime_error(string_format("Unsupported bandwidth: %d", arg->bandwidth));
        }

        if (arg->short_gi) {
            flags |= IEEE80211_RADIOTAP_MCS_SGI;
        }

        switch (arg->stbc) {
            case 0:
                break;
            case 1:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            case 2:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_2 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            case 3:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_3 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            default:
                throw std::runtime_error(string_format("Unsupported STBC type: %d", arg->stbc));
        }

        if (arg->ldpc) {
            flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
        }

        rtHeaderLen = sizeof(radiotap_header_ht);
        rtHeader.reset(new uint8_t[rtHeaderLen]);
        std::memcpy(rtHeader.get(), radiotap_header_ht, rtHeaderLen);

        rtHeader[MCS_FLAGS_OFF] = flags;
        rtHeader[MCS_IDX_OFF] = static_cast<uint8_t>(arg->mcs_index);
    } else {
        // VHT mode
        uint8_t flags = 0;
        rtHeaderLen = sizeof(radiotap_header_vht);
        rtHeader.reset(new uint8_t[rtHeaderLen]);
        std::memcpy(rtHeader.get(), radiotap_header_vht, rtHeaderLen);

        if (arg->short_gi) {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
        }
        if (arg->stbc) {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
        }

        switch (arg->bandwidth) {
            case 80:
                rtHeader[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_80M;
                break;
            case 160:
                rtHeader[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_160M;
                break;
            default:
                throw std::runtime_error(string_format("Unsupported VHT bandwidth: %d", arg->bandwidth));
        }

        if (arg->ldpc) {
            rtHeader[VHT_CODING_OFF] = IEEE80211_RADIOTAP_VHT_CODING_LDPC_USER0;
        }

        rtHeader[VHT_FLAGS_OFF] = flags;
        rtHeader[VHT_MCSNSS0_OFF] |= static_cast<uint8_t>((arg->mcs_index << IEEE80211_RADIOTAP_VHT_MCS_SHIFT) &
                                                          IEEE80211_RADIOTAP_VHT_MCS_MASK);
        rtHeader[VHT_MCSNSS0_OFF] |=
            static_cast<uint8_t>((arg->vht_nss << IEEE80211_RADIOTAP_VHT_NSS_SHIFT) & IEEE80211_RADIOTAP_VHT_NSS_MASK);
    }

#ifdef __linux__
    // Check system entropy
    {
        int fd = open("/dev/random", O_RDONLY);
        if (fd != -1) {
            int eCount = 0;
            if (ioctl(fd, RNDGETENTCNT, &eCount) == 0 && eCount < 160) {
                std::fprintf(stderr,
                             "Warning: Low entropy available. Consider installing rng-utils, "
                             "jitterentropy, or haveged to increase entropy.\n");
            }
            close(fd);
        }
    }
#endif

    // Attempt to create a UDP listening socket
    std::vector<int> rxFds;
    int bindPort = arg->udp_port;
    int udpFd = open_udp_socket_for_rx(bindPort, arg->rcv_buf);

    // Invalid port is provided
    if (arg->udp_port == 0) {
        // Ephemeral port
        struct sockaddr_in saddr;
        socklen_t saddrLen = sizeof(saddr);
        if (getsockname(udpFd, reinterpret_cast<struct sockaddr *>(&saddr), &saddrLen) != 0) {
            throw std::runtime_error(string_format("Unable to get ephemeral port: %s", std::strerror(errno)));
        }
        bindPort = ntohs(saddr.sin_port);
        std::printf("%" PRIu64 "\tLISTEN_UDP\t%d\n", get_time_ms(), bindPort);
    }

    std::printf("Transmitter is listening on UDP port: %d\n", bindPort);

    rxFds.push_back(udpFd);

    if (arg->udp_port == 0) {
        std::fprintf(stderr, "Invalid UDP port: %d\n", bindPort);
    }

    try {
        uint32_t channelId = (arg->link_id << 8) + arg->radio_port;
        std::shared_ptr<Transmitter> transmitter;

        if (arg->debug_port) {
            // Send data out via UDP to 127.0.0.1:debug_port
            transmitter = std::make_shared<UdpTransmitter>(arg->k,
                                                           arg->n,
                                                           arg->keypair,
                                                           arg->debug_ip,
                                                           arg->debug_port,
                                                           arg->epoch,
                                                           channelId);
        } else {
            // Use the USB-based transmitter
            transmitter = std::make_shared<UsbTransmitter>(arg->k,
                                                           arg->n,
                                                           arg->keypair,
                                                           arg->epoch,
                                                           channelId,
                                                           rtHeader.get(),
                                                           rtHeaderLen,
                                                           frameType,
                                                           rtlDevice);
        }

        // Start polling loop
        dataSource(transmitter, rxFds, arg->fec_timeout, arg->mirror, arg->log_interval);
    } catch (const std::runtime_error &ex) {
        std::fprintf(stderr, "Error in TxFrame::run: %s\n", ex.what());
    }
}
