#pragma once

#if defined(_WIN32) || defined(__APPLE__)
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

/// Receive packets from a Wi-Fi adapter.
class WfbngLink {
public:
    WfbngLink();
    ~WfbngLink();

    static WfbngLink &Instance() {
        static WfbngLink link;
        return link;
    }

    static std::vector<DeviceId> get_device_list();

    bool start(const DeviceId &deviceId, uint8_t channel, int channelWidth, const std::string &keyPath);

    void stop() const;

    bool get_alink_enabled() const;

    void enable_alink(bool enable);

    int get_alink_tx_power() const;

    void set_alink_tx_power(int tx_power);

    /// Process a 802.11 frame.
    void handle_80211_frame(const Packet &packet);

#if defined(_WIN32)
    /// Send a RTP payload via socket.
    void handle_rtp(uint8_t *payload, uint16_t packet_size);
#endif

protected:
    libusb_context *ctx{};
    libusb_device_handle *devHandle{};

    std::shared_ptr<std::thread> usbThread;
    std::unique_ptr<Rtl8812aDevice> rtlDevice;

    std::string keyPath;

#ifndef _WIN32
    // --------------- Adaptive link
    std::unique_ptr<std::thread> usb_event_thread;
    std::unique_ptr<std::thread> usb_tx_thread;
    uint32_t link_id{7669206};
    std::recursive_mutex thread_mutex;
    std::shared_ptr<TxFrame> tx_frame;
    bool alink_enabled = true;
    bool alink_should_stop = false;
    int alink_tx_power = 30;
    std::unique_ptr<std::thread> link_quality_thread;
    FecController fec_controller;

    void start_link_quality_thread();

    void stop_adaptive_link();
// --------------- Adaptive link
#endif

    void init_thread(std::unique_ptr<std::thread> &thread,
                     const std::function<std::unique_ptr<std::thread>()> &init_func);

    void destroy_thread(std::unique_ptr<std::thread> &thread);

    // Use TUN instead of manually crafted IP packets.
    bool tun_enabled = false;
#ifdef __linux__
    std::unique_ptr<Tun> tun_;
#endif
};
