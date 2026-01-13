#pragma once

#include <array>

#include "signal_quality.h"

#if defined(_WIN32) || defined(__APPLE__)
    #include "cross/wfbng_processor.h"

    #ifdef _WIN32
        #include <winsock2.h> // To solve winsock.h redefinition errors, include before libusb.h
        #define INVALID_SOCKET (-1)
    #endif
    #include <libusb.h>
#else
    #include <libusb-1.0/libusb.h>
#endif

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Rtl8812aDevice.h"
#include "fec_controller.h"

#ifdef __linux__
    #include "linux/tun.h"
#endif

#ifndef _WIN32
    #include "linux/tx_frame.h"
#endif

struct DeviceId {
    uint16_t vendor_id;
    uint16_t product_id;
    std::string display_name;
    uint8_t bus_num;
    uint8_t port_num;
};

class AggregatorX;

constexpr int ANTENNA_COUNT = 2;

/// Receive packets from a Wi-Fi adapter.
class WfbngLink {
public:
    WfbngLink();
    ~WfbngLink();

    static std::vector<DeviceId> get_device_list();

    /// Start Wi-Fi monitoring with a device.
    bool start(const DeviceId &deviceId, uint8_t channel, int channelWidth, const std::string &kPath);

    void stop();

#ifdef _WIN32
    /// Send a RTP payload via socket.
    void handle_rtp(uint8_t *payload, uint16_t packet_size);
#endif

    bool get_alink_enabled() const;

    void enable_alink(bool enable);

    int get_alink_tx_power() const;

    void set_alink_tx_power(int tx_power);

    /// Process a 802.11 frame.
    void handle_80211_frame(const Packet &packet);

    std::array<float, ANTENNA_COUNT> get_link_score() const;

    int get_packet_loss() const;

protected:
    libusb_context *ctx{};
    libusb_device_handle *devHandle{};

    std::shared_ptr<std::thread> usbThread;
    std::unique_ptr<Rtl8812aDevice> rtlDevice;

    // In case a link is stopped before initializing an RTL device.
    std::atomic<bool> exit_requested{false};

    std::string keyPath;

    int socketFd = INVALID_SOCKET;

    bool first_rtp_packet_received = false;

    /// Unique identifier for this link. Must match between transmitter and receiver.
    /// Use different values for separate links to avoid interference.
    uint32_t link_id = 7669206;

    std::mutex agg_mutex;

#ifndef _WIN32
    std::unique_ptr<AggregatorX> video_aggregator;
    std::unique_ptr<AggregatorX> udp_aggregator;
#else
    std::unique_ptr<Aggregator> video_aggregator;
#endif

    std::shared_ptr<SignalQualityCalculator> signal_quality_calculator;
    std::array<float, ANTENNA_COUNT> link_score_ = {}; // Percentage
    int packets_lost_ = 0;                              // Number over the last second

#ifndef _WIN32
    // --------------- Adaptive link
    std::unique_ptr<std::thread> usb_event_thread;
    std::unique_ptr<std::thread> usb_tx_thread;
    std::recursive_mutex thread_mutex;
    std::shared_ptr<TxFrame> tx_frame;
    bool alink_should_stop = false;
    std::unique_ptr<std::thread> link_quality_thread;
    FecController fec_controller;

    void start_link_quality_thread();

    void stop_adaptive_link();

    void init_thread(std::unique_ptr<std::thread> &thread,
                     const std::function<std::unique_ptr<std::thread>()> &init_func);

    void destroy_thread(std::unique_ptr<std::thread> &thread);
#endif
    bool alink_enabled = false;
    int alink_tx_power = 30;
    // --------------- Adaptive link

    // Use TUN instead of manually crafted IP packets.
    bool tun_enabled = false;
#ifdef __linux__
    std::unique_ptr<Tun> tun_;
#endif
};
