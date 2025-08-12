﻿#include "wfbng_link.h"

#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>

#include "../gui_interface.h"
#include "WiFiDriver.h"
#include "logger.h"
#include "rtp.h"
#include "rx_frame.h"
#include "signal_quality.h"

#ifdef __linux__
    #include "linux/tun.h"
    #include "linux/tx_frame.h"
    #include "wfb-ng/rx.hpp"

    #define INVALID_SOCKET (-1)
#else
    #include "windows/wfbng_processor.h"

    #pragma comment(lib, "ws2_32.lib")
#endif

#define GET_H264_NAL_UNIT_TYPE(buffer_ptr) (buffer_ptr[0] & 0x1F)

static int socketFd = INVALID_SOCKET;
static std::atomic playing = false;

constexpr u8 WFB_TX_PORT = 160;
constexpr u8 WFB_RX_PORT = 32;

inline bool isH264(const uint8_t *data) {
    auto h264NalType = GET_H264_NAL_UNIT_TYPE(data);
    return h264NalType == 24 || h264NalType == 28;
}

#ifdef __linux__
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

        static uint16_t prev_seq_num = seq_num;
        if (seq_num - prev_seq_num > 1) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "RTP packets lost: {}", seq_num - prev_seq_num - 1);
        }
        prev_seq_num = seq_num;

        if (!playing) {
            playing = true;

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
        sendto(sockfd, reinterpret_cast<const char *>(payload), packet_size, 0, (sockaddr *)&saddr, sizeof(saddr));
    }

private:
    AggregatorX(const AggregatorX &);
    AggregatorX &operator=(const AggregatorX &);

    // int sockfd;
    // struct sockaddr_in saddr;
};
#endif

std::vector<DeviceId> WfbngLink::GetDeviceList() {
    std::vector<DeviceId> list;

    // Initialize libusb
    libusb_context *find_ctx;
    libusb_init(&find_ctx);

    // Get a list of USB devices
    libusb_device **devs;
    ssize_t count = libusb_get_device_list(find_ctx, &devs);
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

                DeviceId dev_id = {
                    .vendor_id = desc.idVendor,
                    .product_id = desc.idProduct,
                    .display_name = ss.str(),
                    .bus_num = bus_num,
                    .port_num = port_num,
                };

                list.push_back(dev_id);
            }
        }
    }

    // std::sort(list.begin(), list.end(), [](std::string &a, std::string &b) {
    //     static std::vector<std::string> specialStrings = {"0b05:17d2", "0bda:8812", "0bda:881a"};
    //     auto itA = std::find(specialStrings.begin(), specialStrings.end(), a);
    //     auto itB = std::find(specialStrings.begin(), specialStrings.end(), b);
    //     if (itA != specialStrings.end() && itB == specialStrings.end()) {
    //         return true;
    //     }
    //     if (itB != specialStrings.end() && itA == specialStrings.end()) {
    //         return false;
    //     }
    //     return a < b;
    // });

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    // Deinitialize libusb
    libusb_exit(find_ctx);

    return list;
}

bool WfbngLink::start(const DeviceId &deviceId, uint8_t channel, int channelWidthMode, const std::string &kPath) {
    GuiInterface::Instance().wifiFrameCount_ = 0;
    GuiInterface::Instance().wfbFrameCount_ = 0;
    GuiInterface::Instance().rtpPktCount_ = 0;
    GuiInterface::Instance().UpdateCount();

    keyPath = kPath;

    if (usbThread) {
        return false;
    }

    auto logger = std::make_shared<Logger>();

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
                int bus_num = libusb_get_bus_number(dev);
                int port_num = libusb_get_port_number(dev);

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
        GuiInterface::Instance().ShowTip(FTR("invalid usb msg"));
        return false;
    }

    // Check if the kernel driver attached
    if (libusb_kernel_driver_active(devHandle, 0)) {
        // Detach driver
        rc = libusb_detach_kernel_driver(devHandle, 0);
    }

    rc = libusb_claim_interface(devHandle, 0);
    if (rc < 0) {
        libusb_close(devHandle);
        devHandle = nullptr;

        libusb_exit(ctx);
        ctx = nullptr;

        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to claim interface");
        return false;
    }

#ifdef __linux__
    tx_frame = std::make_shared<TxFrame>(tun_enabled);
#endif

    usbThread = std::make_shared<std::thread>([=, this]() {
        WiFiDriver wifi_driver{logger};
        try {
            rtlDevice = wifi_driver.CreateRtlDevice(devHandle);

#ifdef __linux__
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
                [](const Packet &p) {
                    Instance().handle_80211_frame(p);
                    GuiInterface::Instance().UpdateCount();
                },
                SelectedChannel{
                    .Channel = channel,
                    .ChannelOffset = 0,
                    .ChannelWidth = static_cast<ChannelWidth_t>(channelWidthMode),
                });
        } catch (const std::runtime_error &e) {
            GuiInterface::Instance().PutLog(LogLevel::Error, e.what());
        } catch (...) {
        }

        auto rc1 = libusb_release_interface(devHandle, 0);
        if (rc1 < 0) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to release interface");
        }

#ifdef __linux__
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

        usbThread.reset();

        GuiInterface::Instance().EmitWifiStopped();
        playing = false;

        GuiInterface::Instance().PutLog(LogLevel::Info, "USB thread stopped");
    });
    usbThread->detach();

#ifdef __linux__
    if (tun_enabled) {
        tun_ = std::make_unique<Tun>();
        tun_->init("10.5.0.3", 24, 8001, 8000);
        tun_->start();
    }
#endif

    return true;
}

#ifdef __linux__

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

        const auto map_range = [](double value, double inputMin, double inputMax, double outputMin, double outputMax) {
            return outputMin + ((value - inputMin) * (outputMax - outputMin) / (inputMax - inputMin));
        };

        while (!this->alink_should_stop) {
            auto quality = SignalQualityCalculator::get_instance().calculate_signal_quality();
            GuiInterface::Instance().link_quality_ = map_range(quality.quality, -1024, 1024, 0, 100);
            if (quality.total_last_second != 0) {
                GuiInterface::Instance().packet_loss_ =
                    std::round((float)quality.lost_last_second / quality.total_last_second * 1000.0f) / 10.0f;
            } else {
                GuiInterface::Instance().packet_loss_ = 100;
            }

            time_t currentEpoch = time(nullptr);

            // Map to 1000..2000
            quality.quality = map_range(quality.quality, -1024, 1024, 1000, 2000);

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

                // Change FEC
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

                int fec_lvl = fec_controller.value();
                GuiInterface::Instance().drone_fec_level_ = fec_lvl;

                // Prepare the TX message
                snprintf(message + sizeof(len),
                         sizeof(message) - sizeof(len),
                         "%ld:%d:%d:%d:%d:%d:%f:0:-1:%d:%s\n",
                         static_cast<long>(currentEpoch),
                         quality.quality,
                         quality.quality,
                         quality.recovered_last_second,
                         quality.lost_last_second,
                         quality.quality,
                         quality.snr,
                         fec_lvl,
                         quality.idr_code.c_str());

                len = strlen(message + sizeof(len));

                // Put message length in the message header
                uint32_t net_len = htonl(len);
                memcpy(message, &net_len, sizeof(len));

                // printf("TX message: %s", message + sizeof(len));

                size_t buf_size = len + sizeof(len);

                // printf("Alink thread sends a packet, size %lu\n", buf_size);

                ssize_t sent =
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

    rtlDevice->SetTxPower(alink_tx_power);
}

void WfbngLink::stop_adaptive_link() {
    GuiInterface::Instance().PutLog(LogLevel::Info, "Stop alink thread");

    std::unique_lock lock(thread_mutex);

    if (!link_quality_thread) {
        return;
    }

    alink_should_stop = true;
    destroy_thread(link_quality_thread);
}

#endif

void WfbngLink::handle_80211_frame(const Packet &packet) {
    GuiInterface::Instance().wifiFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    RxFrame frame(packet.Data);
    if (!frame.IsValidWfbFrame()) {
        return;
    }

    GuiInterface::Instance().wfbFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    static uint32_t link_id = 7669206; // sha1 hash of link_domain="default"
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

#ifdef __linux__
    static std::unique_ptr<AggregatorX> video_aggregator =
        std::make_unique<AggregatorX>(client_addr,
                                      GuiInterface::Instance().playerPort,
                                      keyPath.c_str(),
                                      epoch,
                                      video_channel_id_f,
                                      0);

    static std::unique_ptr<AggregatorX> udp_aggregator =
        std::make_unique<AggregatorX>(client_addr, udp_client_port, keyPath, epoch, udp_channel_id_f, 0);
#else
    static std::unique_ptr<Aggregator> video_aggregator = std::make_unique<Aggregator>(
        keyPath.c_str(),
        epoch,
        video_channel_id_f,
        [](uint8_t *payload, uint16_t packet_size) { Instance().handle_rtp(payload, packet_size); });
#endif

    static int8_t rssi[2] = {1, 1};
    static uint8_t antenna[4] = {1, 1, 1, 1};
    uint32_t freq = 0;
    int8_t noise[4] = {1, 1, 1, 1};

    // The aggregator is static, so we need a mutex to modify it
    // Considering to make it non-static
    static std::mutex agg_mutex;
    std::lock_guard lock(agg_mutex);

    // Video frame
    if (frame.MatchesChannelID(video_channel_id_be8)) {
        // Update signal quality
        SignalQualityCalculator::get_instance().add_rssi(packet.RxAtrib.rssi[0], packet.RxAtrib.rssi[1]);
        SignalQualityCalculator::get_instance().add_snr(packet.RxAtrib.snr[0], packet.RxAtrib.snr[1]);

#ifdef __linux__
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

        SignalQualityCalculator::get_instance().add_fec_data(video_aggregator->count_p_all,
                                                             video_aggregator->count_p_fec_recovered,
                                                             video_aggregator->count_p_lost);
#else
        video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                         packet.Data.size() - sizeof(ieee80211_header) - 4,
                                         0,
                                         antenna,
                                         rssi);

        auto quality = SignalQualityCalculator::get_instance().calculate_signal_quality();
        GuiInterface::Instance().link_quality_ = map_range(quality.quality, -1024, 1024, 0, 100);
#endif
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

#ifdef _WIN32
void WfbngLink::handle_rtp(uint8_t *payload, uint16_t packet_size) {
    GuiInterface::Instance().rtpPktCount_++;
    GuiInterface::Instance().UpdateCount();

    if (rtlDevice->should_stop) {
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

    if (!playing) {
        playing = true;
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
    sendto(socketFd,
           reinterpret_cast<const char *>(payload),
           packet_size,
           0,
           (sockaddr *)&serverAddr,
           sizeof(serverAddr));
}
#endif

void WfbngLink::stop() const {
    if (rtlDevice) {
        rtlDevice->should_stop = true;
    }
#ifdef __linux__
    if (tun_) {
        tun_->stop();
    }
#endif
}

bool WfbngLink::get_alink_enabled() const {
#ifdef __linux__
    return alink_enabled;
#else
    return false;
#endif
}

int WfbngLink::get_alink_tx_power() const {
#ifdef __linux__
    return alink_tx_power;
#else
    return 0;
#endif
}

void WfbngLink::enable_alink(bool enable) {
#ifdef __linux__
    if (alink_enabled == enable) {
        return;
    }

    alink_enabled = enable;
    alink_should_stop = !enable;

    // Enable alink during playing.
    if (alink_enabled && link_quality_thread) {
        start_link_quality_thread();
    }
#endif
}

void WfbngLink::set_alink_tx_power(int tx_power) {
#ifdef __linux__
    if (tx_power <= 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "Invalid alink tx power!");
        return;
    }
    alink_tx_power = tx_power;

    // Change alink tx power during playing.
    if (alink_enabled && link_quality_thread) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Set alink tx power (live): {}", tx_power);

        rtlDevice->SetTxPower(alink_tx_power);
    } else {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Set alink tx power: {}", tx_power);
    }
#endif
}

WfbngLink::WfbngLink() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "WSAStartup failed");
        return;
    }

    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
#endif
}

WfbngLink::~WfbngLink() {
#ifdef _WIN32
    closesocket(socketFd);
    socketFd = INVALID_SOCKET;
    WSACleanup();
#endif

    stop();
}
