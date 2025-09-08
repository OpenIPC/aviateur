#pragma once

// -- External C Libraries --
extern "C" {
#include "../wfb-ng/zfex.h"
}

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../wfb-ng/wifibroadcast.hpp"
#include "Rtl8812aDevice.h"

/// A custom deleter for FEC pointer usage in unique_ptr
struct FecDeleter {
    void operator()(fec_t *fecPtr) const {
        if (fecPtr) {
            fec_free(fecPtr);
        }
    }
};

/**
 * @class Transmitter
 * @brief Base class providing FEC encoding, encryption, and session key management.
 *
 * Derived classes implement the actual injection (sending) of packets.
 */
class Transmitter {
public:
    /**
     * @brief Constructs a Transmitter with specified FEC parameters, keypair file, epoch, and channel ID.
     * @param k Number of primary FEC fragments.
     * @param n Total FEC fragments.
     * @param keypair File path to keypair file.
     * @param epoch Unique epoch for the session.
     * @param channelId Channel identifier (e.g., linkId << 8 | radioPort).
     */
    Transmitter(int k, int n, const std::string &keypair, uint64_t epoch, uint32_t channelId);

    /**
     * @brief Virtual destructor to ensure proper cleanup in derived classes.
     */
    virtual ~Transmitter();

    /**
     * @brief Sends a single packet (optionally triggers FEC block finalization).
     * @param buf Pointer to payload data.
     * @param size Size of the payload data in bytes.
     * @param flags Additional flags (e.g., WFB_PACKET_FEC_ONLY).
     * @return True if the packet is enqueued or encoded, false if ignored (e.g., FEC-only while block is empty).
     */
    bool sendPacket(const uint8_t *buf, size_t size, uint8_t flags);

    /**
     * @brief Injects the current session key packet.
     */
    void sendSessionKey();

    /**
     * @brief Choose which output interface (antenna / socket / etc.) to use.
     * @param idx The interface index, or -1 for "mirror" mode.
     */
    virtual void selectOutput(int idx) = 0;

    /**
     * @brief Dumps statistics (injected vs. dropped packets, latencies, etc.) for derived transmitters.
     * @param fp File pointer to write stats.
     * @param ts Current timestamp (ms).
     * @param injectedPackets [out] cumulative count of injected packets.
     * @param droppedPackets [out] cumulative count of dropped packets.
     * @param injectedBytes [out] cumulative count of injected bytes.
     */
    virtual void dumpStats(FILE *fp,
                           uint64_t ts,
                           uint32_t &injectedPackets,
                           uint32_t &droppedPackets,
                           uint32_t &injectedBytes) = 0;

protected:
    /**
     * @brief Actually injects (sends) the final buffer. Implemented by derived classes.
     * @param buf Pointer to data to send.
     * @param size Byte length of data to send.
     */
    virtual void injectPacket(const uint8_t *buf, size_t size) = 0;

private:
    void sendBlockFragment(size_t packetSize);
    void makeSessionKey();

private:
    // FEC encoding
    std::unique_ptr<fec_t, FecDeleter> fecPtr_;
    const unsigned short int fecK_;
    const unsigned short int fecN_;

    // Per-block counters
    uint64_t blockIndex_;
    uint8_t fragmentIndex_;
    std::vector<std::unique_ptr<uint8_t[]>> block_;
    size_t maxPacketSize_;

    // Session properties
    const uint64_t epoch_;
    const uint32_t channelId_;

    // Crypto keys
    uint8_t txSecretKey_[crypto_box_SECRETKEYBYTES];
    uint8_t rxPublicKey_[crypto_box_PUBLICKEYBYTES];
    uint8_t sessionKey_[crypto_aead_chacha20poly1305_KEYBYTES];

    // Session key packet buffer: header + data + Mac
    uint8_t sessionKeyPacket_[sizeof(wsession_hdr_t) + sizeof(wsession_data_t) + crypto_box_MACBYTES];
};

/**
 * @class TxAntennaItem
 * @brief Tracks statistics for a single output interface/antenna.
 */
class TxAntennaItem {
public:
    TxAntennaItem()
        : countPacketsInjected(0), countBytesInjected(0), countPacketsDropped(0), latencySum(0), latencyMin(0),
          latencyMax(0) {}

    /**
     * @brief Logs packet latency and updates injection/dropping stats.
     * @param latency Microseconds elapsed.
     * @param succeeded True if packet was sent successfully, false if dropped.
     * @param packetSize Number of bytes in the packet.
     */
    void logLatency(const uint64_t latency, const bool succeeded, const uint32_t packetSize) {
        if ((countPacketsInjected + countPacketsDropped) == 0) {
            latencyMin = latency;
            latencyMax = latency;
        } else {
            latencyMin = std::min(latency, latencyMin);
            latencyMax = std::max(latency, latencyMax);
        }
        latencySum += latency;

        if (succeeded) {
            ++countPacketsInjected;
            countBytesInjected += packetSize;
        } else {
            ++countPacketsDropped;
        }
    }

    // Stats
    uint32_t countPacketsInjected;
    uint32_t countBytesInjected;
    uint32_t countPacketsDropped;

    uint64_t latencySum;
    uint64_t latencyMin;
    uint64_t latencyMax;
};

/// Map: key = (antennaIndex << 8) | 0xff, value = TxAntennaItem
typedef std::unordered_map<uint64_t, TxAntennaItem> TxAntennaStat;

#ifdef __linux__
/**
 * @class RawSocketTransmitter
 * @brief Sends packets over raw AF_PACKET sockets.
 *
 * This transmitter can operate in single-output or "mirror" mode. In mirror mode,
 * it sends each packet through all raw sockets. In single-output mode, only one is selected.
 */
class RawSocketTransmitter final : public Transmitter {
public:
    RawSocketTransmitter(int k,
                         int n,
                         const std::string &keypair,
                         uint64_t epoch,
                         uint32_t channelId,
                         const std::vector<std::string> &wlans,
                         std::shared_ptr<uint8_t[]> radiotapHeader,
                         size_t radiotapHeaderLen,
                         uint8_t frameType);

    ~RawSocketTransmitter() override;

    void selectOutput(const int idx) override {
        currentOutput_ = idx;
    }

    void dumpStats(FILE *fp,
                   uint64_t ts,
                   uint32_t &injectedPackets,
                   uint32_t &droppedPackets,
                   uint32_t &injectedBytes) override;

private:
    void injectPacket(const uint8_t *buf, size_t size) override;

private:
    const uint32_t channelId_;
    int currentOutput_;
    uint16_t ieee80211Sequence_;
    std::vector<int> sockFds_;
    TxAntennaStat antennaStat_;
    std::shared_ptr<uint8_t[]> radiotapHeader_;
    size_t radiotapHeaderLen_;
    uint8_t frameType_;
};
#endif

/**
 * @class UdpTransmitter
 * @brief Sends packets over a simple UDP socket.
 */
class UdpTransmitter : public Transmitter {
public:
    UdpTransmitter(int k,
                   int n,
                   const std::string &keypair,
                   const std::string &clientAddr,
                   int basePort,
                   uint64_t epoch,
                   uint32_t channelId);

    ~UdpTransmitter() override;

    void dumpStats(FILE *fp,
                   uint64_t ts,
                   uint32_t &injectedPackets,
                   uint32_t &droppedPackets,
                   uint32_t &injectedBytes) override {
        // No stats for UDP
    }

    void selectOutput(int idx) override;

private:
    void injectPacket(const uint8_t *buf, size_t size) override;

private:
    int sockFd_;
    int basePort_;
    struct sockaddr_in saddr_;
};

/**
 * @class UsbTransmitter
 * @brief Sends packets via an attached USB Wi-Fi device.
 */
class UsbTransmitter : public Transmitter {
public:
    UsbTransmitter(int k,
                   int n,
                   const std::string &keypair,
                   uint64_t epoch,
                   uint32_t channelId,
                   uint8_t *radiotapHeader,
                   size_t radiotapHeaderLen,
                   uint8_t frameType,
                   Rtl8812aDevice *device);

    ~UsbTransmitter() override = default;

    void selectOutput(int idx) override;

    void dumpStats(FILE *fp,
                   uint64_t ts,
                   uint32_t &injectedPackets,
                   uint32_t &droppedPackets,
                   uint32_t &injectedBytes) override;

private:
    void injectPacket(const uint8_t *buf, size_t size) override;

private:
    const uint32_t channelId_;
    int currentOutput_;
    uint16_t ieee80211Sequence_;
    TxAntennaStat antennaStat_;
    uint8_t *radiotapHeader_;
    size_t radiotapHeaderLen_;
    uint8_t frameType_;
    Rtl8812aDevice *rtlDevice_;
};
