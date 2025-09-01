#pragma once

#include <sodium.h>
#include <sodium/crypto_box.h>

#include <algorithm>
#include <string>
#include <unordered_map>

#undef min
#undef max

#if defined(_WIN32) || defined(__APPLE__)

static uint8_t ieee80211_header[] = {
    0x08, 0x01, 0x00, 0x00,             // data frame, not protected, from STA to DS via an AP, duration not set
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // receiver is broadcast
    0x57, 0x42, 0xaa, 0xbb, 0xcc, 0xdd, // last four bytes will be replaced by channel_id
    0x57, 0x42, 0xaa, 0xbb, 0xcc, 0xdd, // last four bytes will be replaced by channel_id
    0x00, 0x00,                         // (seq_num << 4) + fragment_num
};

    #define IEEE80211_RADIOTAP_MCS_HAVE_BW 0x01
    #define IEEE80211_RADIOTAP_MCS_HAVE_MCS 0x02
    #define IEEE80211_RADIOTAP_MCS_HAVE_GI 0x04
    #define IEEE80211_RADIOTAP_MCS_HAVE_FMT 0x08

    #define IEEE80211_RADIOTAP_MCS_BW_20 0
    #define IEEE80211_RADIOTAP_MCS_BW_40 1
    #define IEEE80211_RADIOTAP_MCS_BW_20L 2
    #define IEEE80211_RADIOTAP_MCS_BW_20U 3
    #define IEEE80211_RADIOTAP_MCS_SGI 0x04
    #define IEEE80211_RADIOTAP_MCS_FMT_GF 0x08

    #define IEEE80211_RADIOTAP_MCS_HAVE_FEC 0x10
    #define IEEE80211_RADIOTAP_MCS_HAVE_STBC 0x20
    #define IEEE80211_RADIOTAP_MCS_FEC_LDPC 0x10
    #define IEEE80211_RADIOTAP_MCS_STBC_MASK 0x60
    #define IEEE80211_RADIOTAP_MCS_STBC_1 1
    #define IEEE80211_RADIOTAP_MCS_STBC_2 2
    #define IEEE80211_RADIOTAP_MCS_STBC_3 3
    #define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

    #define MCS_KNOWN                                                                                        \
        (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | \
         IEEE80211_RADIOTAP_MCS_HAVE_STBC | IEEE80211_RADIOTAP_MCS_HAVE_FEC)

static uint8_t radiotap_header[] __attribute__((unused)) = {
    0x00,
    0x00, // <-- radiotap version
    0x0d,
    0x00, // <- radiotap header length
    0x00,
    0x80,
    0x08,
    0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
    0x08,
    0x00, // RADIOTAP_F_TX_NOACK
    MCS_KNOWN,
    0x00,
    0x00 // bitmap, flags, mcs_index
};

typedef struct {
    uint64_t block_idx;
    uint8_t **fragments;
    uint8_t *fragment_map;
    uint8_t fragment_to_send_idx;
    uint8_t has_fragments;
} rx_ring_item_t;

inline int modN(int x, int base) {
    return (base + (x % base)) % base;
}

class antennaItem {
public:
    antennaItem(void) : count_all(0), rssi_sum(0), rssi_min(0), rssi_max(0) {}

    void log_rssi(int8_t rssi) {
        if (count_all == 0) {
            rssi_min = rssi;
            rssi_max = rssi;
        } else {
            rssi_min = std::min(rssi, rssi_min);
            rssi_max = std::max(rssi, rssi_max);
        }
        rssi_sum += rssi;
        count_all += 1;
    }

    int32_t count_all;
    int32_t rssi_sum;
    int8_t rssi_min;
    int8_t rssi_max;
};

typedef std::unordered_map<uint64_t, antennaItem> antenna_stat_t;

    #define RX_RING_SIZE 40

    #pragma pack(push, 1)
typedef struct {
    uint8_t packet_type;
    uint8_t session_nonce[crypto_box_NONCEBYTES]; // random data
} wsession_hdr_t;
    #pragma pack(pop)

    #pragma pack(push, 1)
typedef struct {
    uint64_t epoch;      // Drop session packets from old epoch
    uint32_t channel_id; // (link_id << 8) + port_number
    uint8_t fec_type;    // Now only supported type is WFB_FEC_VDM_RS
    uint8_t k;           // FEC k
    uint8_t n;           // FEC n
    uint8_t session_key[crypto_aead_chacha20poly1305_KEYBYTES];
} wsession_data_t;
    #pragma pack(pop)

    // Data packet. Embed FEC-encoded data
    #pragma pack(push, 1)
typedef struct {
    uint8_t packet_type;
    uint64_t data_nonce; // big endian, data_nonce = (block_idx << 8) + fragment_idx
} wblock_hdr_t;
    #pragma pack(pop)

    // Plain data packet after FEC decode
    #pragma pack(push, 1)
typedef struct {
    uint8_t flags;
    uint16_t packet_size; // big endian
} wpacket_hdr_t;
    #pragma pack(pop)

    #define WIFI_MTU 4045

    #define MAX_PAYLOAD_SIZE                                                                                \
        (WIFI_MTU - sizeof(ieee80211_header) - sizeof(wblock_hdr_t) - crypto_aead_chacha20poly1305_ABYTES - \
         sizeof(wpacket_hdr_t))
    #define MAX_FEC_PAYLOAD \
        (WIFI_MTU - sizeof(ieee80211_header) - sizeof(wblock_hdr_t) - crypto_aead_chacha20poly1305_ABYTES)
    #define MAX_FORWARDER_PACKET_SIZE (WIFI_MTU - sizeof(ieee80211_header))

    #define BLOCK_IDX_MASK ((1LLU << 56) - 1)
    #define MAX_BLOCK_IDX ((1LLU << 55) - 1)

    // packet types
    #define WFB_PACKET_DATA 0x1
    #define WFB_PACKET_KEY 0x2

    // FEC types
    #define WFB_FEC_VDM_RS 0x1 // Reed-Solomon on Vandermonde matrix

    // packet flags
    #define WFB_PACKET_FEC_ONLY 0x1

    #define SESSION_KEY_ANNOUNCE_MSEC 1000
    #define RX_ANT_MAX 4

#endif
