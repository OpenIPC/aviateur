#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class WebUiBridge {
public:
    WebUiBridge();
    ~WebUiBridge();

    bool start(uint16_t local_proxy_port = 8080, const std::string &sky_ip = "10.5.0.10", uint16_t sky_port = 80);
    void stop();

    bool is_running() const { return running_; }

private:
    void lwip_thread_func();
    void local_proxy_thread_func();

    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> lwip_thread_;
    std::unique_ptr<std::thread> proxy_thread_;

    uint16_t local_proxy_port_ = 8080;
    std::string sky_ip_ = "10.5.0.10";
    uint16_t sky_port_ = 80;

    int udp_send_fd_ = -1;
    int udp_recv_fd_ = -1;
};
