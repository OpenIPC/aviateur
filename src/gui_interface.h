#pragma once

#include <common/any_callable.h>
#include <mini/ini.h>
#include <servers/translation_server.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>

#ifdef __linux__
    #include <pwd.h>
    #include <unistd.h>
#endif

#include "app.h"
#include "wifi/wfbng_link.h"

#define CONFIG_FILE "config.ini"

#define CONFIG_CONFIG "config"
#define CONFIG_VERSION "version"

#define CONFIG_WIFI "wifi"
#define WIFI_DEVICE "pid_vid"
#define WIFI_CHANNEL "channel"
#define WIFI_CHANNEL_WIDTH_MODE "channel_width_mode"
#define WIFI_GS_KEY "key"
#define WIFI_ALINK_ENABLED "alink_enabled"
#define WIFI_ALINK_TX_POWER "alink_tx_power"

#define CONFIG_LOCALHOST "localhost"
#define CONFIG_LOCALHOST_PORT "port"
#define CONFIG_LOCALHOST_CODEC "codec"

#define CONFIG_SETTINGS "settings"
#define CONFIG_SETTINGS_LANG "language"
#define CONFIG_SETTINGS_DARK_MODE "dark_mode"
#define CONFIG_SETTINGS_MEDIA_BACKEND "media_backend"
#define CONFIG_SETTINGS_RENDER_BACKEND "render_backend"

#define DEFAULT_PORT 52356

constexpr auto LOGGER_MODULE = "Aviateur";

/// Bump this if the config structure changes.
constexpr auto CONFIG_VERSION_NUM = 6;

const revector::ColorU GREEN = revector::ColorU(78, 135, 82);
const revector::ColorU RED = revector::ColorU(201, 79, 79);
const revector::ColorU YELLOW = revector::ColorU(255, 201, 14);

const std::map<int, std::string> CHANNELS = {
    // 2.4GHz
    {1, "2412 MHz [1]"},
    {2, "2417 MHz [2]"},
    {3, "2422 MHz [3]"},
    {4, "2427 MHz [4]"},
    {5, "2432 MHz [5]"},
    {6, "2437 MHz [6]"},
    {7, "2442 MHz [7]"},
    {8, "2447 MHz [8]"},
    {9, "2452 MHz [9]"},
    {10, "2457 MHz [10]"},
    {11, "2462 MHz [11]"},
    {12, "2467 MHz [12]"},
    {13, "2472 MHz [13]"},
    {14, "2484 MHz [14]"},
    // 5.8GHz
    {36, "5180 MHz [36]"},
    {40, "5200 MHz [40]"},
    {44, "5220 MHz [44]"},
    {48, "5240 MHz [48]"},
    {52, "5260 MHz [52]"},
    {56, "5280 MHz [56]"},
    {60, "5300 MHz [60]"},
    {64, "5320 MHz [64]"},
    {100, "5500 MHz [100]"},
    {104, "5520 MHz [104]"},
    {108, "5540 MHz [108]"},
    {112, "5560 MHz [112]"},
    {116, "5580 MHz [116]"},
    {120, "5600 MHz [120]"},
    {124, "5620 MHz [124]"},
    {128, "5640 MHz [128]"},
    {132, "5660 MHz [132]"},
    {136, "5680 MHz [136]"},
    {140, "5700 MHz [140]"},
    {144, "5720 MHz [144]"},
    {149, "5745 MHz [149]"},
    {153, "5765 MHz [153]"},
    {157, "5785 MHz [157]"},
    {161, "5805 MHz [161]"},
    {165, "5825 MHz [165]"},
    {169, "5845 MHz [169]"},
    {173, "5865 MHz [173]"},
    {177, "5885 MHz [177]"},
};

/// Channel widths.
constexpr std::array CHANNEL_WIDTHS{
    "20 MHz",
    "40 MHz",
};

/// Alink TX powers.
constexpr std::array ALINK_TX_POWERS{
    "1 mW",
    "10 mW",
    "20 mW",
    "30 mW",
    "40 mW",
};

enum class LogLevel {
    Info,
    Debug,
    Warn,
    Error,
};

inline std::string IniToString(const mINI::INIStructure &ini) {
    std::ostringstream oss;

    for (const auto &[section, entries] : ini) {
        oss << "[" << section << "]\n";

        for (const auto &[key, value] : entries) {
            oss << key << "=" << value << "\n";
        }
        oss << "\n";
    }
    return oss.str();
}

/// Acts as an interface between GUI and core.
class GuiInterface {
public:
    static GuiInterface &Instance() {
        static GuiInterface interface;
        return interface;
    }

    GuiInterface() = default;

    ~GuiInterface() = default;

    void init() {
#ifdef _WIN32
        ShowWindow(GetConsoleWindow(), SW_HIDE); // SW_RESTORE to bring back

        // Windows crash dump
        SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#endif

        // Redirect standard output to a file
        // DO NOT USE WHEN DEPLOYING, AS IT WILL CRASH THE APP ON USER PCs.
        // freopen((GetAppDataDir() + std::string("last_run_log.txt")).c_str(), "w", stdout);

        // Set up loggers
        {
            // revector::Logger::set_default_level(revector::Logger::Level::Info);
            revector::Logger::set_module_level("revector", revector::Logger::Level::Info);
            revector::Logger::set_module_level(LOGGER_MODULE, revector::Logger::Level::Info);

            auto logCallback = [](LogLevel level, std::string msg) {
                switch (level) {
                    case LogLevel::Info: {
                        revector::Logger::info(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Debug: {
                        revector::Logger::debug(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Warn: {
                        revector::Logger::warn(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Error: {
                        revector::Logger::error(msg, LOGGER_MODULE);
                    } break;
                    default:;
                }
            };
            logCallbacks.emplace_back(logCallback);
        }

        // Load config.
        if (bool read_success = ReadConfig(ini_)) {
            set_locale(ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_LANG]);
            use_gstreamer_ = ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_MEDIA_BACKEND] == "gstreamer";
            use_vulkan_ = ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_RENDER_BACKEND] == "vulkan";
#ifdef __APPLE__
            // No OpenGL on macOS
            use_vulkan_ = true;
#endif
            rtp_codec_ = ini_[CONFIG_LOCALHOST][CONFIG_LOCALHOST_CODEC];
            dark_mode_ = ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_DARK_MODE] == "true";
        }
    }

    static std::vector<DeviceId> GetDeviceList() {
        return WfbngLink::get_device_list();
    }

    static std::string GetAppDataDir() {
#if defined(_MSC_VER)
        auto dir = std::string(getenv("APPDATA")) + "\\Aviateur\\";
#elif defined(__APPLE__)
        std::string home_dir = getenv("HOME");
        auto dir = std::string(home_dir + "/Library/Application Support/Aviateur/");
#elif defined(__linux__)
        passwd *pw = getpwuid(getuid());
        const char *home_dir = pw->pw_dir;
        auto dir = std::string(home_dir) + "/.aviateur/";
#endif
        return dir;
    }

    static std::string GetCaptureDir() {
#if defined(_WIN32)
        auto dir = std::string(getenv("USERPROFILE")) + R"(\Videos\Aviateur Captures\)";
#elif defined(__APPLE__)
        std::string home_dir = getenv("HOME");
        auto dir = std::string(home_dir + "/Pictures/Aviateur Captures/");
#else
        passwd *pw = getpwuid(getuid());
        const char *home_dir = pw->pw_dir;
        auto dir = std::string(home_dir) + "/Pictures/Aviateur Captures/";
#endif
        return dir;
    }

    static bool ReadConfig(mINI::INIStructure &ini) {
        ini.clear();

        auto dir = GetAppDataDir();
        mINI::INIFile file(dir + CONFIG_FILE);
        bool read_success = file.read(ini);

        if (!ini.has(CONFIG_CONFIG)) {
            read_success = false;
        } else {
            int version_num = std::stoi(ini[CONFIG_CONFIG][CONFIG_VERSION]);

            // The config version is not compatible (no matter too old or too new).
            if (version_num != CONFIG_VERSION_NUM) {
                Instance().PutLog(LogLevel::Info, "Clear incompatible config");
                ini.clear();
                read_success = false;
            }
        }

        // Default config.
        if (!read_success) {
            ini[CONFIG_CONFIG][CONFIG_VERSION] = std::to_string(CONFIG_VERSION_NUM);

            ini[CONFIG_WIFI][WIFI_DEVICE] = "";
            ini[CONFIG_WIFI][WIFI_CHANNEL] = "161";
            ini[CONFIG_WIFI][WIFI_CHANNEL_WIDTH_MODE] = "0";
            ini[CONFIG_WIFI][WIFI_GS_KEY] = "";
            ini[CONFIG_WIFI][WIFI_ALINK_ENABLED] = "false";
            ini[CONFIG_WIFI][WIFI_ALINK_TX_POWER] = "20";

            ini[CONFIG_LOCALHOST][CONFIG_LOCALHOST_PORT] = "5600";
            ini[CONFIG_LOCALHOST][CONFIG_LOCALHOST_CODEC] = "H264";

            ini[CONFIG_SETTINGS][CONFIG_SETTINGS_LANG] = "en";
            ini[CONFIG_SETTINGS][CONFIG_SETTINGS_MEDIA_BACKEND] = "ffmpeg";
            ini[CONFIG_SETTINGS][CONFIG_SETTINGS_RENDER_BACKEND] = "opengl";
            ini[CONFIG_SETTINGS][CONFIG_SETTINGS_DARK_MODE] = "true";
        }

        if (read_success) {
            Instance().PutLog(LogLevel::Info, "Read config:\n{}", IniToString(Instance().ini_));
        }

        return read_success;
    }

    static bool SaveConfig() {
        // For clearing obsolete entries.
        // Instance().ini_.clear();

        Instance().ini_[CONFIG_WIFI][WIFI_ALINK_ENABLED] = WfbngLink::Instance().get_alink_enabled() ? "true" : "false";
        Instance().ini_[CONFIG_WIFI][WIFI_ALINK_TX_POWER] = std::to_string(WfbngLink::Instance().get_alink_tx_power());

        Instance().ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_LANG] = Instance().locale_;
        Instance().ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_MEDIA_BACKEND] =
            Instance().use_gstreamer_ ? "gstreamer" : "ffmpeg";
        Instance().ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_RENDER_BACKEND] = Instance().use_vulkan_ ? "vulkan" : "opengl";
        Instance().ini_[CONFIG_SETTINGS][CONFIG_SETTINGS_DARK_MODE] = Instance().dark_mode_ ? "true" : "false";

        Instance().ini_[CONFIG_LOCALHOST][CONFIG_LOCALHOST_CODEC] = Instance().rtp_codec_;

        auto dir = GetAppDataDir();

        try {
            if (!std::filesystem::exists(dir)) {
                std::filesystem::create_directories(dir);
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }

        mINI::INIFile file(dir + std::string(CONFIG_FILE));
        bool write_success = file.write(Instance().ini_, true);

        if (write_success) {
            Instance().PutLog(LogLevel::Info, "Save config:\n{}", IniToString(Instance().ini_));
        }

        return write_success;
    }

    static bool Start(const DeviceId &deviceId, int channel, int channelWidthMode, std::string gsKeyPath) {
        Instance().ini_[CONFIG_WIFI][WIFI_DEVICE] = deviceId.display_name;
        Instance().ini_[CONFIG_WIFI][WIFI_CHANNEL] = std::to_string(channel);
        Instance().ini_[CONFIG_WIFI][WIFI_CHANNEL_WIDTH_MODE] = std::to_string(channelWidthMode);
        Instance().ini_[CONFIG_WIFI][WIFI_GS_KEY] = gsKeyPath;

        // Set port.
        Instance().playerPort = GetFreePort(DEFAULT_PORT);
        Instance().PutLog(LogLevel::Info, "Using port: {}", Instance().playerPort);

        // If no custom key provided by the user, use the default key.
        if (gsKeyPath.empty()) {
            gsKeyPath = revector::get_asset_dir("gs.key");
            Instance().PutLog(LogLevel::Info, "Using GS key: {}", gsKeyPath);
        }
        return WfbngLink::Instance().start(deviceId, channel, channelWidthMode, gsKeyPath);
    }

    static bool Stop() {
        WfbngLink::Instance().stop();
        return true;
    }

    static void EnableAlink(bool enable) {
        Instance().PutLog(LogLevel::Info, "Enable alink: {}", enable);
        WfbngLink::Instance().enable_alink(enable);
    }

    static void SetAlinkTxPower(int power) {
        WfbngLink::Instance().set_alink_tx_power(power);
    }

    static void BuildSdp(const std::string &filePath, const std::string &codec, int payloadType, int port) {
        auto absolutePath = std::filesystem::absolute(filePath);
        std::string dirPath = absolutePath.parent_path().string();

        try {
            if (!std::filesystem::exists(dirPath)) {
                std::filesystem::create_directories(dirPath);
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }

        std::ofstream sdpFos(filePath);
        sdpFos << "v=0\n";
        sdpFos << "o=- 0 0 IN IP4 127.0.0.1\n";
        sdpFos << "s=No Name\n";
        sdpFos << "c=IN IP4 127.0.0.1\n";
        sdpFos << "t=0 0\n";
        sdpFos << "m=video " << port << " RTP/AVP " << payloadType << "\n";
        sdpFos << "a=rtpmap:" << payloadType << " " << codec << "/90000\n";
        sdpFos.flush();
        sdpFos.close();

        Instance().PutLog(LogLevel::Info,
                          "Built SDP: Codec: " + codec + ", Payload type: " + std::to_string(payloadType) +
                              ", Port: " + std::to_string(port));
    }

    template <typename... Args>
    void PutLog(LogLevel level, const std::string_view message, Args... format_items) {
        std::string str = std::vformat(message, std::make_format_args(format_items...));
        EmitLog(level, str);
    }

    void NotifyRtpStream(int pt, uint16_t ssrc, int port, const std::string &codec) {
        std::string sdpFile = "sdp/sdp" + std::to_string(port) + ".sdp";

        BuildSdp(sdpFile, codec, pt, port);

        EmitRtpStream(sdpFile);
    }

    void UpdateCount() {
        EmitWifiFrameCountUpdated(wifiFrameCount_);
        EmitWfbFrameCountUpdated(wfbngFrameCount_);
        EmitRtpPktCountUpdated(rtpPktCount_);
    }

    long long GetWfbngFrameCount() const {
        return wfbngFrameCount_;
    }
    long long GetRtpPktCount() const {
        return rtpPktCount_;
    }
    long long GetWifiFrameCount() const {
        return wifiFrameCount_;
    }

    int GetPlayerPort() const {
        return playerPort;
    }
    std::string GetPlayerCodec() const {
        return playerCodec;
    }

    static int GetFreePort(int start_port) {
#ifdef _WIN32
        // Declare some variables
        WSADATA wsaData;

        int free_port = 0;

        int iResult = 0; // used to return function results

        // the listening socket to be created
        SOCKET soc = INVALID_SOCKET;

        //----------------------
        // Initialize Winsock
        iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != NO_ERROR) {
            wprintf(L"Error at WSAStartup()\n");
            return 0;
        }

        // Create a SOCKET for listening for incoming connection requests
        soc = socket(AF_INET, SOCK_DGRAM, 0);
        if (soc == INVALID_SOCKET) {
            wprintf(L"socket function failed with error: %u\n", WSAGetLastError());
            WSACleanup();
            return 0;
        }

        for (int port = start_port; port < start_port + 200; ++port) {
            // The sockaddr_in structure specifies the address family,
            // IP address, and port for the socket that is being bound.
            sockaddr_in sin;
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = inet_addr("0.0.0.0");
            sin.sin_port = htons(port);

            // Bind the socket.
            iResult = bind(soc, (sockaddr *)&sin, sizeof(sin));
            if (iResult == SOCKET_ERROR) {
                Instance().PutLog(LogLevel::Info, "bind failed with error {}", WSAGetLastError());
            } else {
                free_port = port;
                break;
            }
        }

        closesocket(soc);
        WSACleanup();

        return free_port;
#else
        return start_port;
#endif
    }

    void set_locale(std::string locale) {
        locale_ = locale;
        revector::TranslationServer::get_singleton()->set_locale(locale_);
    }

    mINI::INIStructure ini_;

    std::string locale_ = "en";

    /// Number of received 802.11 frames
    long long wifiFrameCount_ = 0;
    /// Number of received wfb-ng frames
    long long wfbngFrameCount_ = 0;
    /// Number of received RTP packets
    long long rtpPktCount_ = 0;

    int playerPort = 0;
    std::string playerCodec;

    // Local RTP listener
    std::string rtp_codec_;

    bool dark_mode_ = false;

    bool config_file_exists = true;

    bool is_using_wifi = true;

    float link_quality_ = 0; // Percentage
    float packet_loss_ = 0;  // Percentage
    int drone_fec_level_ = 0;

    // Use gstreamer for decoding instead of ffmpeg
    bool use_gstreamer_ = false;

#ifdef __APPLE__
    bool use_vulkan_ = true;
#else
    bool use_vulkan_ = false;
#endif

    // Signals.
    std::vector<revector::AnyCallable<void>> logCallbacks;
    std::vector<revector::AnyCallable<void>> tipCallbacks;
    std::vector<revector::AnyCallable<void>> wifiStopCallbacks;
    std::vector<revector::AnyCallable<void>> wifiFrameCountCallbacks;
    std::vector<revector::AnyCallable<void>> wfbFrameCountCallbacks;
    std::vector<revector::AnyCallable<void>> rtpPktCountCallbacks;
    std::vector<revector::AnyCallable<void>> rtpStreamCallbacks;
    std::vector<revector::AnyCallable<void>> bitrateUpdateCallbacks;
    std::vector<revector::AnyCallable<void>> decoderReadyCallbacks;

    std::vector<revector::AnyCallable<void>> urlStreamShouldStopCallbacks;

    void EmitLog(LogLevel level, std::string msg) {
        for (auto &callback : logCallbacks) {
            try {
                callback.operator()<LogLevel, std::string>(std::move(level), std::move(msg));
            } catch (std::bad_any_cast &) {
            }
        }
    }

    void ShowTip(std::string msg) {
        for (auto &callback : tipCallbacks) {
            try {
                callback.operator()<std::string>(std::move(msg));
            } catch (std::bad_any_cast &) {
            }
        }
    }

    void EmitWifiStopped() {
        for (auto &callback : wifiStopCallbacks) {
            try {
                callback();
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitWifiFrameCountUpdated(long long count) {
        for (auto &callback : wifiFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitWfbFrameCountUpdated(long long count) {
        for (auto &callback : wfbFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitRtpPktCountUpdated(long long count) {
        for (auto &callback : rtpPktCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitRtpStream(std::string sdp) {
        for (auto &callback : rtpStreamCallbacks) {
            try {
                callback.operator()<std::string>(std::move(sdp));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitBitrateUpdate(uint64_t bitrate) {
        for (auto &callback : bitrateUpdateCallbacks) {
            try {
                callback.operator()<uint64_t>(std::move(bitrate));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitDecoderReady(uint32_t width, uint32_t height, float videoFps, std::string decoder_name) {
        for (auto &callback : decoderReadyCallbacks) {
            try {
                callback.operator()<uint32_t, uint32_t, float, std::string>(std::move(width),
                                                                            std::move(height),
                                                                            std::move(videoFps),
                                                                            std::move(decoder_name));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitUrlStreamShouldStop() {
        for (auto &callback : urlStreamShouldStopCallbacks) {
            try {
                callback.operator()<>();
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }
};
