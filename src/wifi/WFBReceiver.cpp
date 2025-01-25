//
// Created by Talus on 2024/6/10.
//

#include "WFBReceiver.h"

#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>

#include "../gui_interface.h"
#include "Rtp.h"
#include "RxFrame.h"
#include "WFBProcessor.h"
#include "WiFiDriver.h"
#include "logger.h"

#ifdef _WIN32
    #include <winsock2.h>

    #pragma comment(lib, "ws2_32.lib")
#else
    /* Assume that any non-Windows platform uses POSIX-style sockets instead. */
    #include <arpa/inet.h>
    #include <netdb.h> /* Needed for getaddrinfo() and freeaddrinfo() */
    #include <sys/socket.h>
    #include <unistd.h> /* Needed for close() */
#endif

std::vector<std::string> WFBReceiver::GetDongleList() {
    GuiInterface::Instance().PutLog(LogLevel::Info, "Refresh USB devices");

    std::vector<std::string> list;

    // Initialize libusb
    libusb_context *find_ctx;
    libusb_init(&find_ctx);

    // Get list of USB devices
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
                std::stringstream ss;
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idVendor << ":";
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idProduct;
                list.push_back(ss.str());

                GuiInterface::Instance().PutLog(LogLevel::Info,
                                                "Device VID PID: {} | USB version: {:x}",
                                                ss.str(),
                                                desc.bcdUSB);
            }
        }
    }

    std::sort(list.begin(), list.end(), [](std::string &a, std::string &b) {
        static std::vector<std::string> specialStrings = {"0b05:17d2", "0bda:8812", "0bda:881a"};
        auto itA = std::find(specialStrings.begin(), specialStrings.end(), a);
        auto itB = std::find(specialStrings.begin(), specialStrings.end(), b);
        if (itA != specialStrings.end() && itB == specialStrings.end()) {
            return true;
        }
        if (itB != specialStrings.end() && itA == specialStrings.end()) {
            return false;
        }
        return a < b;
    });

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    // Deinitialize libusb
    libusb_exit(find_ctx);

    return list;
}

bool WFBReceiver::Start(const std::string &vidPid, uint8_t channel, int channelWidthMode, const std::string &kPath) {
    GuiInterface::Instance().wifiFrameCount_ = 0;
    GuiInterface::Instance().wfbFrameCount_ = 0;
    GuiInterface::Instance().rtpPktCount_ = 0;
    GuiInterface::Instance().UpdateCount();

    keyPath = kPath;

    if (usbThread) {
        return false;
    }

    // Get vid pid
    std::istringstream iss(vidPid);
    unsigned int wifiDeviceVid, wifiDevicePid;
    char c;
    iss >> std::hex >> wifiDeviceVid >> c >> wifiDevicePid;

    auto logger = std::make_shared<Logger>();

    int rc = libusb_init(&ctx);
    if (rc < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to initialize a libusb context");
        return false;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_ERROR);

    devHandle = libusb_open_device_with_vid_pid(ctx, wifiDeviceVid, wifiDevicePid);
    if (devHandle == nullptr) {
        GuiInterface::Instance().PutLog(LogLevel::Error,
                                        "Cannot open device {:04x}:{:04x}",
                                        wifiDeviceVid,
                                        wifiDevicePid);
        libusb_exit(ctx);
        return false;
    }

    // Check if the kernel driver attached
    if (libusb_kernel_driver_active(devHandle, 0)) {
        // Detach driver
        rc = libusb_detach_kernel_driver(devHandle, 0);
    }

    rc = libusb_claim_interface(devHandle, 0);
    if (rc < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to claim interface");
        return false;
    }

    usbThread = std::make_shared<std::thread>([=, this]() {
        WiFiDriver wifi_driver{logger};
        try {
            rtlDevice = wifi_driver.CreateRtlDevice(devHandle);
            rtlDevice->Init(
                [](const Packet &p) {
                    Instance().handle80211Frame(p);
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

        GuiInterface::Instance().PutLog(LogLevel::Info, "USB thread stopped");

        libusb_close(devHandle);
        libusb_exit(ctx);

        devHandle = nullptr;
        ctx = nullptr;

        usbThread.reset();

        GuiInterface::Instance().NotifyWifiStop();
    });
    usbThread->detach();

    return true;
}

void WFBReceiver::handle80211Frame(const Packet &packet) {
    GuiInterface::Instance().wifiFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    RxFrame frame(packet.Data);
    if (!frame.IsValidWfbFrame()) {
        return;
    }

    GuiInterface::Instance().wfbFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    static int8_t rssi[4] = {1, 1, 1, 1};
    static uint8_t antenna[4] = {1, 1, 1, 1};

    static uint32_t link_id = 7669206; // sha1 hash of link_domain="default"
    static uint8_t video_radio_port = 0;
    static uint64_t epoch = 0;

    static uint32_t video_channel_id_f = (link_id << 8) + video_radio_port;
    static uint32_t video_channel_id_be = htobe32(video_channel_id_f);

    static auto *video_channel_id_be8 = reinterpret_cast<uint8_t *>(&video_channel_id_be);

    static std::mutex agg_mutex;
    static std::unique_ptr<Aggregator> video_aggregator = std::make_unique<Aggregator>(
        keyPath.c_str(),
        epoch,
        video_channel_id_f,
        [](uint8_t *payload, uint16_t packet_size) { WFBReceiver::Instance().handleRtp(payload, packet_size); });

    std::lock_guard lock(agg_mutex);
    if (frame.MatchesChannelID(video_channel_id_be8)) {
        video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                         packet.Data.size() - sizeof(ieee80211_header) - 4,
                                         0,
                                         antenna,
                                         rssi);
    }
}

static unsigned long long sendFd = INVALID_SOCKET;
static volatile bool playing = false;

#define GET_H264_NAL_UNIT_TYPE(buffer_ptr) (buffer_ptr[0] & 0x1F)

inline bool isH264(const uint8_t *data) {
    auto h264NalType = GET_H264_NAL_UNIT_TYPE(data);
    return h264NalType == 24 || h264NalType == 28;
}

void WFBReceiver::handleRtp(uint8_t *payload, uint16_t packet_size) {
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
        if (GuiInterface::Instance().playerCodec == "AUTO") {
            // Check H264 or h265
            if (isH264(header->getPayloadData())) {
                GuiInterface::Instance().playerCodec = "H264";
            } else {
                GuiInterface::Instance().playerCodec = "H265";
            }
            GuiInterface::Instance().PutLog(LogLevel::Debug, "Check codec " + GuiInterface::Instance().playerCodec);
        }
        GuiInterface::Instance().NotifyRtpStream(header->pt, ntohl(header->ssrc));
    }

    // Send video to player.
    sendto(sendFd,
           reinterpret_cast<const char *>(payload),
           packet_size,
           0,
           (sockaddr *)&serverAddr,
           sizeof(serverAddr));
}

void WFBReceiver::Stop() const {
    playing = false;
    if (rtlDevice) {
        rtlDevice->should_stop = true;
    }
}

WFBReceiver::WFBReceiver() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "WSAStartup failed");
        return;
    }
#endif

    sendFd = socket(AF_INET, SOCK_DGRAM, 0);
}

WFBReceiver::~WFBReceiver() {
#ifdef _WIN32
    closesocket(sendFd);
    sendFd = INVALID_SOCKET;

    WSACleanup();
#else
    close(sendFd);
#endif

    Stop();
}
