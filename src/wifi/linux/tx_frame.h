#pragma once

#include <memory>
#include <vector>

#include "transmitter.h"

class Rtl8812aDevice;

/**
 * @struct TxArgs
 * @brief Command-line or user-provided arguments controlling the transmitter setup.
 */
struct TxArgs {
    // Fec args
    uint8_t k = 8;
    uint8_t n = 12;

    uint8_t radio_port = 0;
    uint32_t link_id = 0x0;
    uint64_t epoch = 0;
    int udp_port = 5600; // Port to receive data
    int log_interval = 1000;

    int bandwidth = 20;
    int short_gi = 0;
    int stbc = 0;
    int ldpc = 0;
    int mcs_index = 1;
    int vht_nss = 1;

    // Send packets to an IP:PORT for debugging
    std::string debug_ip = "127.0.0.1";
    int debug_port = 0;

    int fec_timeout = 20;
    int rcv_buf = 0;
    bool mirror = false;
    bool vht_mode = false;
    std::string keypair = "tx.key";
};

/**
 * @class TxFrame
 * @brief Orchestrates the receiving of inbound packets, the creation of Transmitter(s),
 *        and the forwarding of data via FEC and encryption.
 */
class TxFrame {
public:
    explicit TxFrame(bool tun_enabled);

    ~TxFrame();

    /**
     * @brief Extracts the SO_RXQ_OVFL counter from msghdr control messages.
     * @param msg The msghdr structure from recvmsg.
     * @return The current overflow counter, or 0 if not present.
     */
    static uint32_t extractRxqOverflow(struct msghdr *msg);

    /**
     * @brief Main loop that polls inbound sockets, reading data and passing it to the transmitter.
     * @param transmitter The shared transmitter (UdpTransmitter, RawSocketTransmitter, etc.).
     * @param rxFds Vector of inbound sockets (e.g., from open_udp_socket_for_rx).
     * @param fecTimeout Timeout in ms for finalizing FEC blocks with empty packets.
     * @param mirror If true, sends the same packet to all outputs simultaneously.
     * @param logInterval Interval in ms for printing stats.
     */
    void dataSource(std::shared_ptr<Transmitter> &transmitter,
                    std::vector<int> &rxFds,
                    int fecTimeout,
                    bool mirror,
                    int logInterval);

    /**
     * @brief Configures and runs the transmitter with the given arguments.
     * @param rtlDevice The Rtl8812aDevice pointer (if using USB).
     * @param arg TxArgs structure with user parameters.
     */
    void run(Rtl8812aDevice *rtlDevice, TxArgs *arg);

    /**
     * @brief Signals that the main loop should stop.
     */
    void stop();

private:
    bool shouldStop_ = false;

    bool tun_enabled_ = false;

    /**
     * @brief Create a UDP socket for receiving data
     * @param port UDP port to bind to
     * @param buf_size Receive buffer size
     * @return Socket file descriptor
     */
    static int open_udp_socket_for_rx(int port, int buf_size);
};
