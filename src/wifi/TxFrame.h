#pragma once

#ifdef __linux__

    #include <vector>

    #include "Rtl8812aDevice.h"

// -- External C Libraries --

extern "C" {
    #include "fec.h" // FEC library
}

    #include <netinet/in.h>
    #include <sys/socket.h>

    #include <memory>

    #include "transmitter.h"

// //-------------------------------------------------------------
// // Utility function to format strings (variadic).

// static std::string string_format(const char* fmt, ...) {
//     va_list args;
//     va_start(args, fmt);
//     char buffer[1024];
//     vsnprintf(buffer, sizeof(buffer), fmt, args);
//     va_end(args);
//     return std::string(buffer);
// }

//-------------------------------------------------------------
// A custom deleter for FEC pointer usage in unique_ptr

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
    int udp_port = 5600; // Local port to receive data
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
    TxFrame();
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

    /**
     * @brief Create a UDP socket for receiving data
     * @param port UDP port to bind to
     * @param buf_size Receive buffer size
     * @return Socket file descriptor
     */
    static int open_udp_socket_for_rx(int port, int buf_size);
};

#endif
