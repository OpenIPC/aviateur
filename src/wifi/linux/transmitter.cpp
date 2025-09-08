
#include "transmitter.h"

#include <cinttypes>
#include <cstring>

#include "../cross/endian.h"

//-------------------------------------------------------------
// Transmitter
//-------------------------------------------------------------

Transmitter::Transmitter(const int k, const int n, const std::string &keypair, uint64_t epoch, uint32_t channelId)
    : fecPtr_(nullptr, FecDeleter{}), fecK_(k), fecN_(n), blockIndex_(0), fragmentIndex_(0),
      block_(static_cast<size_t>(n)), maxPacketSize_(0), epoch_(epoch), channelId_(channelId) {
    // Create a new fec object
    fec_t *rawFec;
    fec_new(fecK_, fecN_, &rawFec);
    if (!rawFec) {
        throw std::runtime_error("fec_new() failed");
    }
    fecPtr_.reset(rawFec);

    // Allocate block buffers
    for (int i = 0; i < fecN_; ++i) {
        block_[i] = std::unique_ptr<uint8_t[]>(new uint8_t[MAX_FEC_PAYLOAD]);
        std::memset(block_[i].get(), 0, MAX_FEC_PAYLOAD);
    }

    // Read keypair from a file
    FILE *fp = std::fopen(keypair.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error(string_format("Unable to open %s: %s", keypair.c_str(), std::strerror(errno)));
    }

    if (std::fread(txSecretKey_, crypto_box_SECRETKEYBYTES, 1, fp) != 1) {
        std::fclose(fp);
        throw std::runtime_error(string_format("Unable to read tx secret key: %s", std::strerror(errno)));
    }
    if (std::fread(rxPublicKey_, crypto_box_PUBLICKEYBYTES, 1, fp) != 1) {
        std::fclose(fp);
        throw std::runtime_error(string_format("Unable to read rx public key: %s", std::strerror(errno)));
    }
    std::fclose(fp);

    // Generate a fresh session key
    makeSessionKey();
}

Transmitter::~Transmitter() {
    // block_, fecPtr_ automatically cleaned up via unique_ptr
}

bool Transmitter::sendPacket(const uint8_t *buf, const size_t size, const uint8_t flags) {
    // If we are asked to finalize FEC block with no data while the block is empty, ignore
    if (fragmentIndex_ == 0 && (flags & WFB_PACKET_FEC_ONLY)) {
        return false;
    }

    // printf("Transmitter sends a packet, size %lu\n", size);

    // Ensure size is within user payload limit
    if (size > MAX_PAYLOAD_SIZE) {
        throw std::runtime_error("sendPacket: packet size exceeds MAX_PAYLOAD_SIZE");
    }

    // Write header
    auto *packetHdr = reinterpret_cast<wpacket_hdr_t *>(block_[fragmentIndex_].get());
    packetHdr->flags = flags;
    packetHdr->packet_size = htons(static_cast<uint16_t>(size));

    size_t wpacketHdrSize = sizeof(wpacket_hdr_t);

    // Copy payload
    std::memcpy(block_[fragmentIndex_].get() + wpacketHdrSize, buf, size);

    const size_t fecPayloadSize = wpacketHdrSize + size;

    // Zero out the remainder
    if (fecPayloadSize < MAX_FEC_PAYLOAD) {
        std::memset(block_[fragmentIndex_].get() + fecPayloadSize, 0, MAX_FEC_PAYLOAD - fecPayloadSize);
    }

    // Send this fragment
    sendBlockFragment(fecPayloadSize);

    // Track the largest data size in block
    maxPacketSize_ = std::max(maxPacketSize_, fecPayloadSize);
    fragmentIndex_++;

    // If not enough fragments for FEC, we are done
    if (fragmentIndex_ < static_cast<uint8_t>(fecK_)) {
        return true;
    }

    // If we have k fragments, encode the parity
    fec_encode_simd(fecPtr_.get(),
                    reinterpret_cast<uint8_t **>(block_.data()),
                    reinterpret_cast<uint8_t **>(block_.data()) + fecK_,
                    maxPacketSize_);

    // Send all FEC fragments
    while (fragmentIndex_ < static_cast<uint8_t>(fecN_)) {
        sendBlockFragment(maxPacketSize_);
        fragmentIndex_++;
    }

    // Move to the next block
    blockIndex_++;
    fragmentIndex_ = 0;
    maxPacketSize_ = 0;

    // Generate a new session key after we have looped over MAX_BLOCK_IDX blocks
    if (blockIndex_ > MAX_BLOCK_IDX) {
        makeSessionKey();
        sendSessionKey();
        blockIndex_ = 0;
    }
    return true;
}

void Transmitter::sendSessionKey() {
    injectPacket(sessionKeyPacket_, sizeof(sessionKeyPacket_));
}

void Transmitter::sendBlockFragment(const size_t packetSize) {
    // Prepare local buffer for encryption
    uint8_t cipherBuf[MAX_FORWARDER_PACKET_SIZE] = {};

    auto *blockHdr = reinterpret_cast<wblock_hdr_t *>(cipherBuf);
    blockHdr->packet_type = WFB_PACKET_DATA;
    blockHdr->data_nonce = htobe64(((blockIndex_ & BLOCK_IDX_MASK) << 8) + fragmentIndex_);

    unsigned long long cipherLen = 0;

    // AEAD encrypt
    const int rc = crypto_aead_chacha20poly1305_encrypt(cipherBuf + sizeof(wblock_hdr_t),
                                                        &cipherLen,
                                                        block_[fragmentIndex_].get(),
                                                        packetSize,
                                                        reinterpret_cast<const uint8_t *>(blockHdr),
                                                        sizeof(wblock_hdr_t),
                                                        nullptr,
                                                        reinterpret_cast<const uint8_t *>(&blockHdr->data_nonce),
                                                        sessionKey_);
    if (rc != 0) {
        throw std::runtime_error("Unable to encrypt packet!");
    }

    const size_t finalSize = sizeof(wblock_hdr_t) + cipherLen;
    injectPacket(cipherBuf, finalSize);
}

void Transmitter::makeSessionKey() {
    // Random session key
    randombytes_buf(sessionKey_, sizeof(sessionKey_));

    auto *hdr = reinterpret_cast<wsession_hdr_t *>(sessionKeyPacket_);
    hdr->packet_type = WFB_PACKET_SESSION;
    randombytes_buf(hdr->session_nonce, sizeof(hdr->session_nonce));

    wsession_data_t sessionData = {};
    sessionData.epoch = htobe64(epoch_);
    sessionData.channel_id = htonl(channelId_);
    sessionData.fec_type = WFB_FEC_VDM_RS;
    sessionData.k = static_cast<uint8_t>(fecK_);
    sessionData.n = static_cast<uint8_t>(fecN_);
    std::memcpy(sessionData.session_key, sessionKey_, sizeof(sessionKey_));

    // Box it
    if (crypto_box_easy(sessionKeyPacket_ + sizeof(wsession_hdr_t),
                        reinterpret_cast<const uint8_t *>(&sessionData),
                        sizeof(sessionData),
                        hdr->session_nonce,
                        rxPublicKey_,
                        txSecretKey_) != 0) {
        throw std::runtime_error("Unable to create session key packet!");
    }
}

#ifdef __linux__

    #include <linux/if_packet.h>
    #include <net/if.h>
    #include <sys/ioctl.h>

//-------------------------------------------------------------
// RawSocketTransmitter
//-------------------------------------------------------------

RawSocketTransmitter::RawSocketTransmitter(int k,
                                           int n,
                                           const std::string &keypair,
                                           uint64_t epoch,
                                           uint32_t channelId,
                                           const std::vector<std::string> &wlans,
                                           std::shared_ptr<uint8_t[]> radiotapHeader,
                                           size_t radiotapHeaderLen,
                                           uint8_t frameType)
    : Transmitter(k, n, keypair, epoch, channelId), channelId_(channelId), currentOutput_(0), ieee80211Sequence_(0),
      radiotapHeader_(std::move(radiotapHeader)), radiotapHeaderLen_(radiotapHeaderLen), frameType_(frameType) {
    // Create raw sockets and bind to specified interfaces
    for (const auto &iface : wlans) {
        int fd = socket(PF_PACKET, SOCK_RAW, 0);
        if (fd < 0) {
            throw std::runtime_error(
                string_format("Unable to open PF_PACKET socket on %s: %s", iface.c_str(), std::strerror(errno)));
        }

        const int optval = 1;
        if (setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &optval, sizeof(optval)) != 0) {
            close(fd);
            throw std::runtime_error(
                string_format("Unable to set PACKET_QDISC_BYPASS on %s: %s", iface.c_str(), std::strerror(errno)));
        }

        ifreq ifr = {};
        std::strncpy(ifr.ifr_name, iface.c_str(), sizeof(ifr.ifr_name) - 1);

        if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            ::close(fd);
            throw std::runtime_error(
                string_format("Unable to get interface index for %s: %s", iface.c_str(), std::strerror(errno)));
        }

        sockaddr_ll sll = {};
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifr.ifr_ifindex;
        sll.sll_protocol = 0;

        if (::bind(fd, reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) < 0) {
            ::close(fd);
            throw std::runtime_error(string_format("Unable to bind to %s: %s", iface.c_str(), std::strerror(errno)));
        }

        sockFds_.push_back(fd);
    }
}

RawSocketTransmitter::~RawSocketTransmitter() {
    for (int fd : sockFds_) {
        close(fd);
    }
}

void RawSocketTransmitter::injectPacket(const uint8_t *buf, size_t size) {
    if (size > MAX_FORWARDER_PACKET_SIZE) {
        throw std::runtime_error("RawSocketTransmitter::injectPacket - packet too large");
    }

    // Build 802.11 header
    uint8_t ieeeHdr[sizeof(ieee80211_header)];
    std::memcpy(ieeeHdr, ieee80211_header, sizeof(ieee80211_header));

    // Patch the Frame Control field, channel ID, and seq number
    ieeeHdr[0] = frameType_;
    const uint32_t channelIdBE = htonl(channelId_);
    std::memcpy(ieeeHdr + SRC_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));
    std::memcpy(ieeeHdr + DST_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));

    ieeeHdr[FRAME_SEQ_LB] = static_cast<uint8_t>(ieee80211Sequence_ & 0xff);
    ieeeHdr[FRAME_SEQ_HB] = static_cast<uint8_t>((ieee80211Sequence_ >> 8) & 0xff);
    ieee80211Sequence_ += 16;

    // iovec for sendmsg
    iovec iov[3] = {};

    iov[0].iov_base = radiotapHeader_.get();
    iov[0].iov_len = radiotapHeaderLen_;
    iov[1].iov_base = const_cast<uint8_t *>(ieeeHdr);
    iov[1].iov_len = sizeof(ieeeHdr);
    iov[2].iov_base = const_cast<uint8_t *>(buf);
    iov[2].iov_len = size;

    msghdr msg = {};
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    if (currentOutput_ >= 0) {
        // Single-interface mode
        const uint64_t startUs = get_time_us();
        const ssize_t rc = sendmsg(sockFds_[currentOutput_], &msg, 0);
        const bool success = rc >= 0 || errno == ENOBUFS;

        if (rc < 0 && errno != ENOBUFS) {
            throw std::runtime_error(string_format("Unable to inject packet: %s", std::strerror(errno)));
        }

        const uint64_t key = (static_cast<uint64_t>(currentOutput_) << 8) | 0xff;
        antennaStat_[key].logLatency(get_time_us() - startUs, success, static_cast<uint32_t>(size));
    } else {
        // Mirror mode: send on all interfaces
        for (size_t i = 0; i < sockFds_.size(); i++) {
            const uint64_t startUs = get_time_us();
            const ssize_t rc = sendmsg(sockFds_[i], &msg, 0);
            const bool success = rc >= 0 || errno == ENOBUFS;

            if (rc < 0 && errno != ENOBUFS) {
                throw std::runtime_error(string_format("Unable to inject packet: %s", std::strerror(errno)));
            }
            uint64_t key = (static_cast<uint64_t>(i) << 8) | 0xff;
            antennaStat_[key].logLatency(get_time_us() - startUs, success, static_cast<uint32_t>(size));
        }
    }
}

void RawSocketTransmitter::dumpStats(FILE *fp,
                                     uint64_t ts,
                                     uint32_t &injectedPackets,
                                     uint32_t &droppedPackets,
                                     uint32_t &injectedBytes) {
    for (auto &kv : antennaStat_) {
        const auto &stats = kv.second;
        uint64_t countAll = stats.countPacketsInjected + stats.countPacketsDropped;
        uint64_t avgLatency = (countAll == 0) ? 0 : (stats.latencySum / countAll);

        // fprintf(fp,
        //         "%" PRIu64 "\tTX_ANT\t%" PRIx64 "\t%u:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
        //         ts,
        //         kv.first,
        //         stats.countPacketsInjected,
        //         stats.countPacketsDropped,
        //         stats.latencyMin,
        //         avgLatency,
        //         stats.latencyMax);

        injectedPackets += stats.countPacketsInjected;
        droppedPackets += stats.countPacketsDropped;
        injectedBytes += stats.countBytesInjected;
    }

    antennaStat_.clear();
}
#endif

//-------------------------------------------------------------
// UdpTransmitter
//-------------------------------------------------------------

UdpTransmitter::UdpTransmitter(int k,
                               int n,
                               const std::string &keypair,
                               const std::string &clientAddr,
                               int basePort,
                               uint64_t epoch,
                               uint32_t channelId)
    : Transmitter(k, n, keypair, epoch, channelId), sockFd_(-1), basePort_(basePort) {
    sockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd_ < 0) {
        throw std::runtime_error(string_format("Error opening UDP socket: %s", std::strerror(errno)));
    }

    std::memset(&saddr_, 0, sizeof(saddr_));
    saddr_.sin_family = AF_INET;
    saddr_.sin_addr.s_addr = ::inet_addr(clientAddr.c_str());
    saddr_.sin_port = htons(static_cast<unsigned short>(basePort_));
}

UdpTransmitter::~UdpTransmitter() {
    close(sockFd_);
}

void UdpTransmitter::selectOutput(int idx) {
    saddr_.sin_port = htons(static_cast<unsigned short>(basePort_ + idx));
}

void UdpTransmitter::injectPacket(const uint8_t *buf, size_t size) {
    // Create a random wrxfwd_t header
    wrxfwd_t fwdHeader = {};
    fwdHeader.wlan_idx = static_cast<uint8_t>(std::rand() % 2);

    std::memset(fwdHeader.antenna, 0xff, sizeof(fwdHeader.antenna));
    std::memset(fwdHeader.rssi, SCHAR_MIN, sizeof(fwdHeader.rssi));

    fwdHeader.antenna[0] = static_cast<uint8_t>(std::rand() % 2);
    fwdHeader.rssi[0] = static_cast<int8_t>(std::rand() & 0xff);

    // Two-element iovec
    iovec iov[2];
    iov[0].iov_base = reinterpret_cast<void *>(&fwdHeader);
    iov[0].iov_len = sizeof(fwdHeader);
    iov[1].iov_base = const_cast<uint8_t *>(buf);
    iov[1].iov_len = size;

    msghdr msg = {};
    msg.msg_name = &saddr_;
    msg.msg_namelen = sizeof(saddr_);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    sendmsg(sockFd_, &msg, 0);
}

//-------------------------------------------------------------
// UsbTransmitter
//-------------------------------------------------------------

UsbTransmitter::UsbTransmitter(int k,
                               int n,
                               const std::string &keypair,
                               uint64_t epoch,
                               uint32_t channelId,
                               uint8_t *radiotapHeader,
                               size_t radiotapHeaderLen,
                               uint8_t frameType,
                               Rtl8812aDevice *device)
    : Transmitter(k, n, keypair, epoch, channelId), channelId_(channelId), currentOutput_(0), ieee80211Sequence_(0),
      radiotapHeader_(radiotapHeader), radiotapHeaderLen_(radiotapHeaderLen), frameType_(frameType),
      rtlDevice_(device) {}

void UsbTransmitter::selectOutput(int idx) {
    currentOutput_ = idx;
}

void UsbTransmitter::dumpStats(FILE *fp,
                               uint64_t ts,
                               uint32_t &injectedPackets,
                               uint32_t &droppedPackets,
                               uint32_t &injectedBytes) {
    for (auto &kv : antennaStat_) {
        const auto &stats = kv.second;
        uint64_t countAll = stats.countPacketsInjected + stats.countPacketsDropped;
        uint64_t avgLatency = (countAll == 0) ? 0 : (stats.latencySum / countAll);

        // fprintf(fp,
        //         "%" PRIu64 "\tTX_ANT\t%" PRIx64 "\t%u:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
        //         ts,
        //         kv.first,
        //         stats.countPacketsInjected,
        //         stats.countPacketsDropped,
        //         stats.latencyMin,
        //         avgLatency,
        //         stats.latencyMax);

        injectedPackets += stats.countPacketsInjected;
        droppedPackets += stats.countPacketsDropped;
        injectedBytes += stats.countBytesInjected;
    }
    antennaStat_.clear();
}

void UsbTransmitter::injectPacket(const uint8_t *buf, const size_t size) {
    if (!rtlDevice_ || rtlDevice_->should_stop) {
        throw std::runtime_error("UsbTransmitter: main thread exit, should stop");
    }

    if (size > MAX_FORWARDER_PACKET_SIZE) {
        throw std::runtime_error("UsbTransmitter:: packet too large");
    }

    uint8_t ieeeHdr[sizeof(ieee80211_header)];
    std::memcpy(ieeeHdr, ieee80211_header, sizeof(ieee80211_header));

    // Patch frame type
    ieeeHdr[0] = frameType_;
    const uint32_t channelIdBE = htonl(channelId_);
    std::memcpy(ieeeHdr + SRC_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));
    std::memcpy(ieeeHdr + DST_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));

    ieeeHdr[FRAME_SEQ_LB] = static_cast<uint8_t>(ieee80211Sequence_ & 0xff);
    ieeeHdr[FRAME_SEQ_HB] = static_cast<uint8_t>((ieee80211Sequence_ >> 8) & 0xff);
    ieee80211Sequence_ += 16;

    const uint64_t startUs = get_time_us();

    // Merge into one contiguous buffer
    const size_t totalSize = radiotapHeaderLen_ + sizeof(ieeeHdr) + size;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[totalSize]);

    std::memcpy(buffer.get(), radiotapHeader_, radiotapHeaderLen_);
    std::memcpy(buffer.get() + radiotapHeaderLen_, ieeeHdr, sizeof(ieeeHdr));
    std::memcpy(buffer.get() + radiotapHeaderLen_ + sizeof(ieeeHdr), buf, size);

    const bool result = rtlDevice_->send_packet(buffer.get(), totalSize);
    if (!result) {
        printf("Rtl8812aDevice::send_packet failed!\n");
    }

    const uint64_t key = (static_cast<uint64_t>(currentOutput_) << 8) | 0xff;
    antennaStat_[key].logLatency(get_time_us() - startUs, result, static_cast<uint32_t>(size));
}
