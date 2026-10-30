#include "web_ui_bridge.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>

#include "net_compat.h"
#include "../gui_interface.h"

extern "C" {
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"

uint32_t sys_now(void) {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return static_cast<uint32_t>(ms);
}
}

static struct netif sky_netif;
static int g_udp_send_fd = -1;

// LwIP netif output callback: sends IP packet out via UDP port 8001
static err_t sky_netif_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr) {
    if (g_udp_send_fd < 0) return ERR_VAL;

    static uint8_t send_buf[2048];
    uint16_t tot_len = 0;

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (tot_len + q->len > sizeof(send_buf) - 2) break;
        memcpy(send_buf + 2 + tot_len, q->payload, q->len);
        tot_len += q->len;
    }

    uint16_t net_len = htons(tot_len);
    memcpy(send_buf, &net_len, 2);

    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8001);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    wfb_sendto(g_udp_send_fd, send_buf, tot_len + 2, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: Outbound IP packet -> UDP 8001 ({} bytes)", tot_len + 2);

    return ERR_OK;
}

static err_t sky_netif_init(struct netif *netif) {
    netif->name[0] = 's';
    netif->name[1] = 'k';
    netif->output = sky_netif_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

// Proxy session state bridging host browser SOCKET and LwIP tcp_pcb
struct ClientSession {
    SOCKET client_socket = INVALID_SOCKET;
    struct tcp_pcb *tpcb = nullptr;
    bool connected = false;
    bool closed = false;
    std::vector<char> pending_send_data;
};

static std::unordered_map<SOCKET, ClientSession*> g_active_sessions;

// LwIP TCP callbacks
static err_t lwip_tcp_recv_fn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    auto *session = static_cast<ClientSession *>(arg);
    if (!session || session->closed) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }

    if (p == nullptr) {
        session->closed = true;
        GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: LwIP TCP connection closed by remote (sky end)");
        return ERR_OK;
    }

    // Forward data from LwIP (sky end) to browser client socket
    for (struct pbuf *q = p; q != nullptr; q = q->next) {
        send(session->client_socket, (const char *)q->payload, (int)q->len, 0);
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t lwip_tcp_sent_fn(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    auto *session = static_cast<ClientSession *>(arg);
    if (session && !session->pending_send_data.empty() && session->connected) {
        size_t to_send = std::min((size_t)tcp_sndbuf(tpcb), session->pending_send_data.size());
        if (to_send > 0) {
            if (tcp_write(tpcb, session->pending_send_data.data(), (u16_t)to_send, TCP_WRITE_FLAG_COPY) == ERR_OK) {
                tcp_output(tpcb);
                session->pending_send_data.erase(session->pending_send_data.begin(), session->pending_send_data.begin() + to_send);
            }
        }
    }
    return ERR_OK;
}

static void lwip_tcp_err_fn(void *arg, err_t err) {
    auto *session = static_cast<ClientSession *>(arg);
    if (session) {
        session->tpcb = nullptr;
        session->closed = true;
        GuiInterface::Instance().PutLog(LogLevel::Error, "WebUiBridge: LwIP TCP error: {}", (int)err);
    }
}

static err_t lwip_tcp_connected_fn(void *arg, struct tcp_pcb *tpcb, err_t err) {
    auto *session = static_cast<ClientSession *>(arg);
    if (session) {
        session->connected = true;
        GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: LwIP TCP connected to sky end!");
        if (!session->pending_send_data.empty()) {
            lwip_tcp_sent_fn(arg, tpcb, 0);
        }
    }
    return ERR_OK;
}

WebUiBridge::WebUiBridge() = default;

WebUiBridge::~WebUiBridge() {
    stop();
}

bool WebUiBridge::start(uint16_t local_proxy_port, const std::string &sky_ip, uint16_t sky_port) {
    if (running_) return true;

    local_proxy_port_ = local_proxy_port;
    sky_ip_ = sky_ip;
    sky_port_ = sky_port;
    running_ = true;

    g_udp_send_fd = socket(AF_INET, SOCK_DGRAM, 0);

    lwip_thread_ = std::make_unique<std::thread>(&WebUiBridge::lwip_thread_func, this);

    GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge started, listening on http://127.0.0.1:{}", local_proxy_port_);

    return true;
}

void WebUiBridge::stop() {
    if (!running_) return;
    running_ = false;

    if (g_udp_send_fd >= 0) {
        wfb_close(g_udp_send_fd);
        g_udp_send_fd = -1;
    }

    if (lwip_thread_ && lwip_thread_->joinable()) {
        lwip_thread_->join();
    }

    GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge stopped");
}

void WebUiBridge::lwip_thread_func() {
    lwip_init();

    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 10, 5, 0, 3);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 10, 5, 0, 1);

    netif_add(&sky_netif, &ipaddr, &netmask, &gw, NULL, sky_netif_init, ip_input);
    netif_set_default(&sky_netif);
    netif_set_up(&sky_netif);
    netif_set_link_up(&sky_netif);

    // 1. UDP Downlink socket (port 8000)
    int udp_recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    wfb_setsockopt(udp_recv_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(8000);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind(udp_recv_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    // 2. TCP Proxy server socket (local_proxy_port_)
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    wfb_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(local_proxy_port_);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    uint8_t udp_buf[2048];
    char tcp_buf[2048];

    auto last_time = std::chrono::steady_clock::now();

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(udp_recv_fd, &read_fds);
        FD_SET(server_fd, &read_fds);

        SOCKET max_fd = std::max((SOCKET)udp_recv_fd, server_fd);

        for (auto &pair : g_active_sessions) {
            if (!pair.second->closed && pair.second->client_socket != INVALID_SOCKET) {
                FD_SET(pair.second->client_socket, &read_fds);
                max_fd = std::max(max_fd, pair.second->client_socket);
            }
        }

        struct timeval tv = {0, 20000}; // 20ms timeout
        int activity = select((int)(max_fd + 1), &read_fds, NULL, NULL, &tv);

        if (activity > 0) {
            // A. Check UDP downlink from sky end
            if (FD_ISSET(udp_recv_fd, &read_fds)) {
                ssize_t len = recv(udp_recv_fd, (char *)udp_buf, sizeof(udp_buf), 0);
                if (len > 2) {
                    GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: Inbound IP packet <- UDP 8000 ({} bytes)", len);
                    uint16_t ip_len = len - 2;
                    struct pbuf *p = pbuf_alloc(PBUF_RAW, ip_len, PBUF_POOL);
                    if (p != nullptr) {
                        pbuf_take(p, udp_buf + 2, ip_len);
                        if (sky_netif.input(p, &sky_netif) != ERR_OK) {
                            pbuf_free(p);
                        }
                    }
                }
            }

            // B. Check new browser connection
            if (FD_ISSET(server_fd, &read_fds)) {
                SOCKET client_socket = accept(server_fd, NULL, NULL);
                if (client_socket != INVALID_SOCKET) {
                    GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: Browser connected on port 8080, connecting to sky end {}:{}", sky_ip_, sky_port_);
                    auto *session = new ClientSession();
                    session->client_socket = client_socket;

                    // Open LwIP TCP connection to sky end
                    struct tcp_pcb *tpcb = tcp_new();
                    if (tpcb) {
                        session->tpcb = tpcb;
                        tcp_arg(tpcb, session);
                        tcp_recv(tpcb, lwip_tcp_recv_fn);
                        tcp_sent(tpcb, lwip_tcp_sent_fn);
                        tcp_err(tpcb, lwip_tcp_err_fn);

                        ip4_addr_t dest_ip;
                        if (ip4addr_aton(sky_ip_.c_str(), &dest_ip)) {
                            struct netif *r_netif = ip4_route(&dest_ip);
                            if (r_netif == nullptr) {
                                GuiInterface::Instance().PutLog(LogLevel::Error, "WebUiBridge: ip4_route returned NULL! No route to {}", sky_ip_);
                            } else {
                                GuiInterface::Instance().PutLog(LogLevel::Info, "WebUiBridge: ip4_route found route successfully.");
                            }

                            err_t conn_err = tcp_connect(tpcb, &dest_ip, sky_port_, lwip_tcp_connected_fn);
                            if (conn_err != ERR_OK) {
                                GuiInterface::Instance().PutLog(LogLevel::Error, "WebUiBridge: tcp_connect returned error code {}", (int)conn_err);
                            }
                        } else {
                            GuiInterface::Instance().PutLog(LogLevel::Error, "WebUiBridge: ip4addr_aton failed for IP: {}", sky_ip_);
                        }

                        g_active_sessions[client_socket] = session;
                    } else {
                        wfb_close(client_socket);
                        delete session;
                    }
                }
            }

            // C. Check browser client sockets for incoming HTTP data
            for (auto it = g_active_sessions.begin(); it != g_active_sessions.end();) {
                SOCKET sock = it->first;
                ClientSession *session = it->second;

                if (session->closed || session->client_socket == INVALID_SOCKET) {
                    if (session->client_socket != INVALID_SOCKET) wfb_close(session->client_socket);
                    if (session->tpcb) {
                        tcp_arg(session->tpcb, NULL);
                        tcp_close(session->tpcb);
                    }
                    delete session;
                    it = g_active_sessions.erase(it);
                    continue;
                }

                if (FD_ISSET(sock, &read_fds)) {
                    int r_len = recv(sock, tcp_buf, sizeof(tcp_buf), 0);
                    if (r_len > 0) {
                        if (session->connected && session->tpcb) {
                            u16_t snd_buf = tcp_sndbuf(session->tpcb);
                            u16_t to_send = (u16_t)std::min((size_t)snd_buf, (size_t)r_len);
                            if (to_send > 0) {
                                if (tcp_write(session->tpcb, tcp_buf, to_send, TCP_WRITE_FLAG_COPY) == ERR_OK) {
                                    tcp_output(session->tpcb);
                                }
                            }
                            if (to_send < (size_t)r_len) {
                                session->pending_send_data.insert(session->pending_send_data.end(),
                                                                  tcp_buf + to_send, tcp_buf + r_len);
                            }
                        } else {
                            session->pending_send_data.insert(session->pending_send_data.end(),
                                                              tcp_buf, tcp_buf + r_len);
                        }
                    } else {
                        session->closed = true;
                    }
                }
                ++it;
            }
        }

        // 3. Drive LwIP timers
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count() >= 250) {
            sys_check_timeouts();
            last_time = now;
        }
    }

    // Cleanup sessions
    for (auto &pair : g_active_sessions) {
        if (pair.second->client_socket != INVALID_SOCKET) wfb_close(pair.second->client_socket);
        if (pair.second->tpcb) {
            tcp_arg(pair.second->tpcb, NULL);
            tcp_close(pair.second->tpcb);
        }
        delete pair.second;
    }
    g_active_sessions.clear();

    wfb_close(udp_recv_fd);
    wfb_close(server_fd);
}
