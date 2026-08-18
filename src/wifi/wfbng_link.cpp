#include "wfbng_link.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "../gui_interface.h"
#include "RxPacket.h"
#include "UsbOpen.h"
#include "WiFiDriver.h"
#include "cross/endian.h"
#include "logger.h"
#include "rtp.h"
#include "rx_frame.h"
#include "signal_quality.h"

#ifdef _WIN32
    #include "cross/wfbng_processor.h"

    #pragma comment(lib, "ws2_32.lib")
#else
    #include "linux/tun.h"
    #include "linux/tx_frame.h"
    #include "wfb-ng/rx.hpp"
#endif

#define GET_H264_NAL_UNIT_TYPE(buffer_ptr) (buffer_ptr[0] & 0x1F)

using u8 = uint8_t;

constexpr u8 WFB_TX_PORT = 160;
constexpr u8 WFB_RX_PORT = 32;

inline bool isH264(const uint8_t *data) {
    auto h264NalType = GET_H264_NAL_UNIT_TYPE(data);
    return h264NalType == 24 || h264NalType == 28;
}

#ifndef _WIN32
class AggregatorX : public AggregatorUDPv4 {
public:
    AggregatorX(const std::string &client_addr,
                int client_port,
                const std::string &keypair,
                uint64_t epoch,
                uint32_t channel_id,
                int snd_buf_size)
        : AggregatorUDPv4(client_addr, client_port, keypair, epoch, channel_id, snd_buf_size) {}

protected:
    void send_to_socket(const uint8_t *payload, const uint16_t packet_size) override {
        GuiInterface::Instance().rtpPktCount_++;
        GuiInterface::Instance().UpdateCount();

        if (packet_size < 12) {
            return;
        }

        auto *header = (RtpHeader *)payload;
        const uint16_t seq_num = htons(header->seq);

        GuiInterface::Instance().PutLog(LogLevel::Debug, "RTP sequence number: {}", seq_num);
        GuiInterface::Instance().PutLog(LogLevel::Debug, "RTP timestamp: {}", htonl(header->stamp));

        if (!prev_seq_num.has_value()) {
            // Check H264 or H265
            if (isH264(header->getPayloadData())) {
                GuiInterface::Instance().playerCodec = "H264";
            } else {
                GuiInterface::Instance().playerCodec = "H265";
            }

            GuiInterface::Instance().NotifyRtpStream(header->pt,
                                                     ntohl(header->ssrc),
                                                     GuiInterface::Instance().playerPort,
                                                     GuiInterface::Instance().playerCodec);
        }

        if (prev_seq_num.has_value() && seq_num - prev_seq_num.value() > 1) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "RTP packets lost: {}", seq_num - prev_seq_num.value() - 1);
        }
        prev_seq_num = seq_num;

        // Send payload via socket.
        sendto(sockfd, payload, packet_size, 0, (sockaddr *)&saddr, sizeof(saddr));
    }

private:
    AggregatorX(const AggregatorX &);
    AggregatorX &operator=(const AggregatorX &);

    std::optional<uint16_t> prev_seq_num;
};
#endif


namespace {

/// Adapters devourer can actually drive. They sort to the top of the device list and
/// get a meaningful label even when the OS will not hand us a product string.
const std::map<uint32_t, const char *> kKnownAdapters = {
    {0x0bda8812, "RTL8812AU"},
    {0x0bda881a, "RTL8812AU-VS"},
    {0x0bda8813, "RTL8814AU"},
    {0x0bdaa81a, "RTL8812EU"},
    {0x0bdac812, "RTL8812CU"},
    {0x0bda8821, "RTL8821AU"},
    {0x0b0517d2, "RTL8812AU (ASUS USB-AC56)"},
    {0x23570120, "RTL8821AU (TP-Link Archer T2U Plus)"},
    {0x35bc0108, "RTL8852BU (TP-Link Archer TX20U Nano)"},
};

/// Product strings are vendor-controlled and occasionally absurd. The button label is
/// sized by its text, so cap the human-readable part; the vid:pid[bus:port] suffix is
/// always kept because it is what disambiguates two identical adapters.
constexpr size_t kMaxProductNameChars = 32;

std::string elide(const std::string &text, size_t max_chars) {
    if (text.size() <= max_chars) {
        return text;
    }
    return text.substr(0, max_chars > 3 ? max_chars - 3 : 0) + "...";
}

uint32_t make_device_key(uint16_t vendor_id, uint16_t product_id) {
    return (static_cast<uint32_t>(vendor_id) << 16) | product_id;
}

std::string read_first_line(const std::string &path) {
    std::ifstream f(path);
    std::string line;
    if (f && std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        return line;
    }
    return {};
}

/// Best-effort product string for a device.
///
/// On Linux sysfs is preferred: it needs no permissions, so we can name every device
/// rather than only the ones we are allowed to open. Elsewhere (and as a fallback) we
/// ask the device itself, which requires opening it and therefore usually only works
/// for the adapter our udev rule covers.
std::string query_product_name(libusb_device *dev, const libusb_device_descriptor &desc) {
#ifdef __linux__
    std::array<uint8_t, 8> ports{};
    const int depth = libusb_get_port_numbers(dev, ports.data(), static_cast<int>(ports.size()));
    if (depth > 0) {
        std::string sysfs_name = std::to_string(static_cast<int>(libusb_get_bus_number(dev))) + "-";
        for (int i = 0; i < depth; ++i) {
            sysfs_name += std::to_string(static_cast<int>(ports[i]));
            if (i + 1 < depth) {
                sysfs_name += ".";
            }
        }
        const std::string base = "/sys/bus/usb/devices/" + sysfs_name + "/";
        std::string product = read_first_line(base + "product");
        if (!product.empty()) {
            const std::string manufacturer = read_first_line(base + "manufacturer");
            // Some devices repeat the vendor inside the product string; do not say it twice.
            if (!manufacturer.empty() && product.find(manufacturer) == std::string::npos) {
                product = manufacturer + " " + product;
            }
            return product;
        }
    }
#endif

    if (desc.iProduct == 0) {
        return {};
    }

    libusb_device_handle *handle = nullptr;
    if (libusb_open(dev, &handle) != LIBUSB_SUCCESS || handle == nullptr) {
        // Almost always a permissions issue; the caller falls back to the known table.
        return {};
    }

    unsigned char buf[256] = {};
    const int len = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf) - 1);
    libusb_close(handle);

    if (len > 0) {
        return std::string(reinterpret_cast<char *>(buf), len);
    }
    return {};
}

} // namespace

std::vector<DeviceId> WfbngLink::get_device_list() {
    std::vector<DeviceId> list;

    // Initialize libusb
    libusb_context *find_ctx;
    libusb_init(&find_ctx);

    // Get a list of USB devices
    libusb_device **devs;
    const ssize_t count = libusb_get_device_list(find_ctx, &devs);
    if (count < 0) {
        return list;
    }

    // Iterate over devices
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = devs[i];

        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            // Check if the device is using libusb driver
            if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
                uint8_t bus_num = libusb_get_bus_number(dev);
                uint8_t port_num = libusb_get_port_number(dev);

                std::stringstream ss;
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idVendor << ":";
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idProduct;
                ss << std::dec << " [" << (int)bus_num << ":" << (int)port_num << "]";
                const std::string id_key = ss.str();

                // Prefer the chip name we know over a vague product string like
                // "802.11n NIC", but fall back to whatever the OS reports.
                const auto known = kKnownAdapters.find(make_device_key(desc.idVendor, desc.idProduct));
                const bool is_known = known != kKnownAdapters.end();

                std::string product_name = query_product_name(dev, desc);
                std::string label = is_known ? known->second : product_name;
                if (label.empty()) {
                    label = "Unknown USB device";
                }

                DeviceId dev_id = {
                    .vendor_id = desc.idVendor,
                    .product_id = desc.idProduct,
                    .display_name = elide(label, kMaxProductNameChars) + "  (" + id_key + ")",
                    .bus_num = bus_num,
                    .port_num = port_num,
                    .id_key = id_key,
                    .product_name = product_name,
                    .known_adapter = is_known,
                };

                list.push_back(dev_id);
            }
        }
    }

    // Supported FPV adapters first, then alphabetically, so the one the user
    // actually wants is at the top instead of buried among mice and hubs.
    std::sort(list.begin(), list.end(), [](const DeviceId &a, const DeviceId &b) {
        if (a.known_adapter != b.known_adapter) {
            return a.known_adapter;
        }
        return a.display_name < b.display_name;
    });

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    // Deinitialize libusb
    libusb_exit(find_ctx);

    return list;
}

bool WfbngLink::start(const DeviceId &deviceId, uint8_t channel, int channelWidthMode, const std::string &kPath) {
    GuiInterface::Instance().wifiFrameCount_ = 0;
    GuiInterface::Instance().wfbngFrameCount_ = 0;
    GuiInterface::Instance().rtpPktCount_ = 0;
    GuiInterface::Instance().UpdateCount();

    keyPath = kPath;

    if (usbThread) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "USB thread already exists");
        return false;
    }

    auto logger = std::make_shared<Logger>();

    if (ctx) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "libusb context should be null");
        return false;
    }

    int rc = libusb_init(&ctx);
    if (rc < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to initialize libusb");
        return false;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_ERROR);

    // Get a list of USB devices
    libusb_device **devs;
    ssize_t count = libusb_get_device_list(ctx, &devs);
    if (count < 0) {
        libusb_exit(ctx);
        ctx = nullptr;
        return false;
    }

    libusb_device *target_dev{};

    // Iterate over devices
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = devs[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            // Check if the device is using libusb driver
            if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
                const int bus_num = libusb_get_bus_number(dev);
                const int port_num = libusb_get_port_number(dev);

                if (desc.idVendor == deviceId.vendor_id && desc.idProduct == deviceId.product_id &&
                    bus_num == deviceId.bus_num && port_num == deviceId.port_num) {
                    target_dev = dev;
                }
            }
        }
    }

    if (!target_dev) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Invalid device ID!");
        // Free the list of devices
        libusb_free_device_list(devs, 1);
        libusb_exit(ctx);
        ctx = nullptr;
        return false;
    }

    // This cannot handle multiple devices with the same vendor_id and product_id.
    // devHandle = libusb_open_device_with_vid_pid(ctx, wifiDeviceVid, wifiDevicePid);
    libusb_open(target_dev, &devHandle);

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    if (devHandle == nullptr) {
        libusb_exit(ctx);
        ctx = nullptr;

        GuiInterface::Instance().PutLog(LogLevel::Error,
                                        "Cannot open device {:04x}:{:04x} at [{:}:{:}]",
                                        deviceId.vendor_id,
                                        deviceId.product_id,
                                        deviceId.bus_num,
                                        deviceId.port_num);
        GuiInterface::Instance().ShowTip("invalid usb msg", true);

        return false;
    }

    // Find the Wi-Fi interface (handles composite devices like RTL8822BU)
    int iface = devourer::find_wifi_interface(devHandle);

    // Prepare the USB device: lock, detach kernel driver, set config, claim
    // (do_reset=false: libusb_reset_device can cause RTL8812AU to re-enumerate
    //  with a stale handle on Windows/WinUSB, breaking URB completion)
    std::shared_ptr<devourer::UsbDeviceLock> usb_lock;
    rc = devourer::claim_interface_then_reset(devHandle, iface, logger, false, usb_lock);
    if (rc < 0) {
        libusb_close(devHandle);
        devHandle = nullptr;

        libusb_exit(ctx);
        ctx = nullptr;

        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to claim interface: {}", rc);

        return false;
    }

#ifndef _WIN32
    tx_frame = std::make_shared<TxFrame>(tun_enabled);
#endif

    usbThread = std::make_shared<std::thread>([=, this]() {
        WiFiDriver wifi_driver{logger};
        try {
            if (exit_requested) {
                return;
            }

            rtlDevice = wifi_driver.CreateRtlDevice(devHandle, ctx, usb_lock);

            if (exit_requested) {
                return;
            }

#ifndef _WIN32
            // if (!usb_event_thread) {
            //     auto usb_event_thread_func = [this] {
            //         while (true) {
            //             if (devHandle == nullptr) {
            //                 break;
            //             }
            //             struct timeval timeout = {0, 500000}; // 500 ms timeout
            //             int r = libusb_handle_events_timeout(ctx, &timeout);
            //             if (r < 0) {
            //                 // this->log->error("Error handling events: {}", r);
            //             }
            //         }
            //     };
            //
            //     init_thread(usb_event_thread, [=]() { return std::make_unique<std::thread>(usb_event_thread_func);
            //     });
            // }

            std::shared_ptr<TxArgs> args = std::make_shared<TxArgs>();
            args->udp_port = 8001;
            args->link_id = link_id;
            args->keypair = keyPath;
            args->stbc = true;
            args->ldpc = true;
            args->mcs_index = 0;
            args->vht_mode = false;
            args->short_gi = false;
            args->bandwidth = 20;
            args->k = 1;
            args->n = 5;
            args->radio_port = WFB_TX_PORT;

            // printf("Radio link ID %d, radio port %d\n", args->link_id, args->radio_port);

            if (!usb_tx_thread) {
                init_thread(usb_tx_thread, [&]() {
                    return std::make_unique<std::thread>([this, args] {
                        tx_frame->run(rtlDevice.get(), args.get());
                        GuiInterface::Instance().PutLog(LogLevel::Info, "USB TX thread should stop");
                    });
                });
            }

            if (alink_enabled) {
                stop_adaptive_link();
                start_link_quality_thread();
            }
#endif

            rtlDevice->Init(
                [this](const Packet &p) {
                    handle_80211_frame(p);
                    GuiInterface::Instance().UpdateCount();
                },
                SelectedChannel{
                    .Channel = channel,
                    .ChannelOffset = 0,
                    .ChannelWidth = static_cast<ChannelWidth_t>(channelWidthMode),
                });

            GuiInterface::Instance().PutLog(LogLevel::Info, "RTL device loop exited");
        } catch (const std::runtime_error &e) {
            GuiInterface::Instance().PutLog(LogLevel::Error, e.what());

            GuiInterface::Instance().ShowTip("invalid device", true);
        } catch (...) {
        }

        // Clean shutdown: halt TRX DMA and power down the chip while USB is
        // still open, then destroy the device object so its destructor
        // (quiesce_tx, thread joins, hal_deinit) runs BEFORE libusb_close.
        // Destroying after close is UB (destructor does USB register writes).
        if (rtlDevice) {
            rtlDevice->Stop();
            rtlDevice.reset();
        }

        auto rc1 = libusb_release_interface(devHandle, iface);
        if (rc1 < 0) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to release interface");
        }

#ifndef _WIN32
        stop_adaptive_link();
        tx_frame->stop();
        destroy_thread(usb_tx_thread);
        GuiInterface::Instance().PutLog(LogLevel::Info, "USB TX thread stopped");
// destroy_thread(usb_event_thread);
#endif

        libusb_close(devHandle);
        libusb_exit(ctx);

        devHandle = nullptr;
        ctx = nullptr;

        GuiInterface::Instance().EmitWifiStopped();
        first_rtp_packet_received = false;

        GuiInterface::Instance().PutLog(LogLevel::Info, "USB thread stopped");
    });
    // usbThread->detach();

#ifdef __linux__
    if (tun_enabled) {
        tun_ = std::make_unique<Tun>();
        tun_->init("10.5.0.3", 24, 8001, 8000);
        tun_->start();
    }
#endif

    return true;
}

#ifndef _WIN32
void WfbngLink::init_thread(std::unique_ptr<std::thread> &thread,
                            const std::function<std::unique_ptr<std::thread>()> &init_func) {
    std::unique_lock lock(thread_mutex);
    destroy_thread(thread);
    thread = init_func();
}

void WfbngLink::destroy_thread(std::unique_ptr<std::thread> &thread) {
    std::unique_lock lock(thread_mutex);
    if (thread && thread->joinable()) {
        thread->join();
        thread = nullptr;
    }
}

void WfbngLink::start_link_quality_thread() {
    GuiInterface::Instance().PutLog(LogLevel::Info, "Start alink thread");

    auto thread_func = [this]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        fec_controller.setEnabled(true);

        std::string ip = "127.0.0.1";
        int port = 8001;

    #ifdef __linux__
        if (tun_enabled) {
            ip = "10.5.0.10";
            port = 9999;
        }
    #endif

        const int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

        // Create UDP socket
        if (sock_fd < 0) {
            printf("Socket creation failed");
            return;
        }

        int opt = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        // Convert the IP address from text to binary form
        if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
            printf("Invalid IP address");
            close(sock_fd);
            return;
        }

        while (!this->alink_should_stop) {
            auto quality = signal_quality_calculator->calculate_signal_quality();

            // Best values of the antennas.
            int best_rssi = std::max(quality.rssi[0], quality.rssi[1]);
            int best_snr = std::max(quality.snr[0], quality.snr[1]);
            int best_link_score = std::max(quality.link_score[0], quality.link_score[1]);

            time_t currentEpoch = time(nullptr);

            // Prepare & send a message
            {
                uint32_t len;
                char message[100];

                /**
                     1741491090:1602:1602:1:0:-70:24:num_ants:pnlt:fec_change:code

                     <gs_time>:<link_score>:<link_score>:<fec>:<lost>:<rssi_dB>:<snr_dB>:<num_ants>:<noise_penalty>:<fec_change>:<idr_request_code>

                    gs_time: gs clock
                    link_score: 1000 - 2000 sent twice (already including any penalty)
                    link_score: 1000 - 2000 sent twice (already including any penalty)
                    fec: instantaneus fec_rec (only used by old fec_rec_pntly now disabled by default)
                    lost: instantaneus lost (not used)
                    rssi_dB: best antenna rssi (for osd)
                    snr_dB: best antenna snr_dB (for osd)
                    num_ants: number of gs antennas (for osd)
                    noise_penalty: penalty deducted from score due to noise (for osd)
                    fec_change: int from 0 to 5 : how much to alter fec based on noise
                    optional idr_request_code: 4 char unique code to request 1 keyframe (no need to send special extra
                   packets)
                 */

                // Change FEC level.
                if (quality.lost_last_second > 2)
                    fec_controller.bump(5);
                else {
                    if (quality.recovered_last_second > 30) {
                        fec_controller.bump(5);
                    }
                    if (quality.recovered_last_second > 24) {
                        fec_controller.bump(3);
                    }
                    if (quality.recovered_last_second > 22) {
                        fec_controller.bump(2);
                    }
                    if (quality.recovered_last_second > 18) {
                        fec_controller.bump(1);
                    }
                    if (quality.recovered_last_second < 18) {
                        fec_controller.bump(0);
                    }
                }

                const int fec_lvl = fec_controller.value();
                GuiInterface::Instance().drone_fec_level_ = fec_lvl;

                // Prepare the TX message
                snprintf(message + sizeof(len),
                         sizeof(message) - sizeof(len),
                         "%ld:%d:%d:%d:%d:%d:%f:0:-1:%d:%s\n",
                         static_cast<long>(currentEpoch),
                         best_link_score,
                         best_link_score,
                         quality.recovered_last_second,
                         quality.lost_last_second,
                         best_rssi,
                         (float)best_snr,
                         fec_lvl,
                         quality.idr_code.c_str());

                len = strlen(message + sizeof(len));

                // Put message length in the message header
                uint32_t net_len = htonl(len);
                memcpy(message, &net_len, sizeof(len));

                // printf("TX message: %s", message + sizeof(len));

                const size_t buf_size = len + sizeof(len);

                // printf("Alink thread sends a packet, size %lu\n", buf_size);

                const ssize_t sent =
                    sendto(sock_fd, message, buf_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
                if (sent < 0) {
                    printf("Failed to send message");
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        close(sock_fd);
        this->alink_should_stop = false;
    };

    init_thread(link_quality_thread, [=]() { return std::make_unique<std::thread>(thread_func); });

    rtlDevice->SetTxPower(static_cast<uint8_t>(alink_tx_power));
}

void WfbngLink::stop_adaptive_link() {
    GuiInterface::Instance().PutLog(LogLevel::Info, "Stopping alink thread");

    std::unique_lock lock(thread_mutex);

    if (!link_quality_thread) {
        return;
    }

    alink_should_stop = true;
    destroy_thread(link_quality_thread);

    GuiInterface::Instance().PutLog(LogLevel::Info, "Alink thread stopped");
}
#endif

void WfbngLink::handle_80211_frame(const Packet &packet) {
    GuiInterface::Instance().wifiFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    const RxFrame frame(packet.Data);
    if (!frame.IsValidWfbFrame()) {
        return;
    }

    GuiInterface::Instance().wfbngFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    static uint8_t video_radio_port = 0;
    static uint64_t epoch = 0;

    static uint32_t video_channel_id_f = (link_id << 8) + video_radio_port;
    static uint32_t video_channel_id_be = htobe32(video_channel_id_f);

    static auto *video_channel_id_be8 = reinterpret_cast<uint8_t *>(&video_channel_id_be);

    int mavlink_client_port = 14550;
    uint8_t mavlink_radio_port = 0x10;
    uint32_t mavlink_channel_id_f = (link_id << 8) + mavlink_radio_port;
    static uint32_t mavlink_channel_id_be = htobe32(mavlink_channel_id_f);
    auto *mavlink_channel_id_be8 = reinterpret_cast<uint8_t *>(&mavlink_channel_id_be);

    int udp_client_port = 8000;
    uint8_t udp_radio_port = WFB_RX_PORT;
    uint32_t udp_channel_id_f = (link_id << 8) + udp_radio_port;
    static uint32_t udp_channel_id_be = htobe32(udp_channel_id_f);
    auto *udp_channel_id_be8 = reinterpret_cast<uint8_t *>(&udp_channel_id_be);

    std::string client_addr = "127.0.0.1";

#ifndef _WIN32
    if (!video_aggregator) {
        video_aggregator = std::make_unique<AggregatorX>(client_addr,
                                                         GuiInterface::Instance().playerPort,
                                                         keyPath.c_str(),
                                                         epoch,
                                                         video_channel_id_f,
                                                         0);
    }
    if (!udp_aggregator) {
        udp_aggregator =
            std::make_unique<AggregatorX>(client_addr, udp_client_port, keyPath, epoch, udp_channel_id_f, 0);
    }
#else
    if (!video_aggregator) {
        video_aggregator = std::make_unique<Aggregator>(
            keyPath.c_str(),
            epoch,
            video_channel_id_f,
            [this](uint8_t *payload, uint16_t packet_size) { handle_rtp(payload, packet_size); });
    }
#endif

    static int8_t rssi[2] = {1, 1};
    static uint8_t antenna[4] = {1, 1, 1, 1};
    uint32_t freq = 0;
    int8_t noise[4] = {1, 1, 1, 1};

    std::lock_guard lock(agg_mutex);

    // Video frame
    if (frame.MatchesChannelID(video_channel_id_be8)) {
        // Update signal quality
        signal_quality_calculator->add_rssi(packet.RxAtrib.rssi[0], packet.RxAtrib.rssi[1]);
        signal_quality_calculator->add_snr(packet.RxAtrib.snr[0], packet.RxAtrib.snr[1]);

#ifndef _WIN32
        video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                         packet.Data.size() - sizeof(ieee80211_header) - 4,
                                         0,
                                         antenna,
                                         rssi,
                                         noise,
                                         freq,
                                         0,
                                         0,
                                         NULL);

        signal_quality_calculator->add_fec(video_aggregator->count_p_all,
                                           video_aggregator->count_p_fec_recovered,
                                           video_aggregator->count_p_lost);

        // This is necessary.
        video_aggregator->clear_stats();
#else
        video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                         packet.Data.size() - sizeof(ieee80211_header) - 4,
                                         0,
                                         antenna,
                                         rssi);
#endif

        const auto quality = signal_quality_calculator->calculate_signal_quality();
        link_score_[0] = quality.link_score[0];
        link_score_[1] = quality.link_score[1];
        packets_lost_ = quality.lost_last_second;
    }
    // MAVLink frame
    else if (frame.MatchesChannelID(mavlink_channel_id_be8)) {
        // GuiInterface::Instance().PutLog(LogLevel::Warn, "Received a MAVLink frame, but we're unable to handle it!");
    }
    // UDP frame
    else if (frame.MatchesChannelID(udp_channel_id_be8)) {
        // GuiInterface::Instance().PutLog(LogLevel::Warn, "Received a UDP frame, but we're unable to handle it!");

#ifdef __linux__
        if (tun_enabled) {
            udp_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                           packet.Data.size() - sizeof(ieee80211_header) - 4,
                                           0,
                                           antenna,
                                           rssi,
                                           noise,
                                           freq,
                                           0,
                                           0,
                                           NULL);
        }
#endif
    }
}

std::array<int, ANTENNA_COUNT> WfbngLink::get_link_score() const {
    return link_score_;
}

int WfbngLink::get_packet_loss() const {
    return packets_lost_;
}

#if defined(_WIN32)
void WfbngLink::handle_rtp(uint8_t *payload, uint16_t packet_size) {
    GuiInterface::Instance().rtpPktCount_++;
    GuiInterface::Instance().UpdateCount();

    if (exit_requested) {
        return;
    }
    if (packet_size < 12) {
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(GuiInterface::Instance().playerPort);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    auto *header = (RtpHeader *)payload;

    if (!first_rtp_packet_received) {
        first_rtp_packet_received = true;
        // Check H264 or H265
        if (isH264(header->getPayloadData())) {
            GuiInterface::Instance().playerCodec = "H264";
        } else {
            GuiInterface::Instance().playerCodec = "H265";
        }
        GuiInterface::Instance().NotifyRtpStream(header->pt,
                                                 ntohl(header->ssrc),
                                                 GuiInterface::Instance().playerPort,
                                                 GuiInterface::Instance().playerCodec);
    }

    // Send payload via socket.
    auto ret = sendto(socketFd,
                      reinterpret_cast<const char *>(payload),
                      packet_size,
                      0,
                      (sockaddr *)&serverAddr,
                      sizeof(serverAddr));
    if (ret == -1) {
        fprintf(stderr, "sendto failed: %s\n", strerror(errno));
    }
}
#endif

void WfbngLink::stop() {
    // Signal the thread immediately.
    exit_requested = true;

    if (rtlDevice) {
        rtlDevice->StopRxLoop();
    }
#ifdef __linux__
    if (tun_) {
        tun_->stop();
    }
#endif

    // Wait for the USB thread to exit.
    if (usbThread && usbThread->joinable()) {
        usbThread->join();
        usbThread.reset();
    }
}

bool WfbngLink::get_alink_enabled() const {
    return alink_enabled;
}

int WfbngLink::get_alink_tx_power() const {
    return alink_tx_power;
}

void WfbngLink::enable_alink(const bool enable) {
#ifndef _WIN32
    if (alink_enabled == enable) {
        return;
    }

    alink_enabled = enable;
    alink_should_stop = !enable;

    // Enable alink during playing.
    if (alink_enabled && usbThread) {
        if (link_quality_thread && link_quality_thread->joinable()) {
            link_quality_thread->join();
            link_quality_thread = nullptr;
        }
        start_link_quality_thread();
    }
#endif
}

void WfbngLink::set_alink_tx_power(const int tx_power) {
#ifndef _WIN32
    if (tx_power <= 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "Invalid alink tx power!");
        return;
    }
    alink_tx_power = tx_power;

    // Change alink tx power during playing.
    if (alink_enabled && link_quality_thread) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Set alink tx power (live): {}", tx_power);

        rtlDevice->SetTxPower(static_cast<uint8_t>(alink_tx_power));
    } else {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Set alink tx power: {}", tx_power);
    }
#endif
}

WfbngLink::WfbngLink() {
#if defined(_WIN32) || defined(__APPLE__)
    #if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "WSAStartup failed");
        return;
    }
    #endif

    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
#endif

    signal_quality_calculator = std::make_unique<SignalQualityCalculator>();
}

WfbngLink::~WfbngLink() {
#ifdef _WIN32
    closesocket(socketFd);
    WSACleanup();
#endif

#ifdef __APPLE__
    close(socketFd);
#endif

    socketFd = INVALID_SOCKET;

    stop();
}
