#pragma once

#if defined(_WIN32)
    #pragma pack(push, 1)
#else
    #include <arpa/inet.h>
#endif

class RtpHeader {
public:
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || defined(_WIN32) && REG_DWORD == REG_DWORD_BIG_ENDIAN
    // Version number, fixed to 2
    uint32_t version : 2;
    // padding
    uint32_t padding : 1;
    // Extension
    uint32_t ext : 1;
    // CSRC count
    uint32_t csrc : 4;
    // Marker
    uint32_t mark : 1;
    // Payload type
    uint32_t pt : 7;
#else
    // CSRC count
    uint32_t csrc : 4;
    // Extension
    uint32_t ext : 1;
    // padding
    uint32_t padding : 1;
    // Version number, fixed to 2
    uint32_t version : 2;
    // Payload type
    uint32_t pt : 7;
    // Marker
    uint32_t mark : 1;
#endif
    // Sequence number
    uint32_t seq : 16;
    // Timestamp
    uint32_t stamp;
    // SSRC
    uint32_t ssrc;
    // Payload; if csrc and ext are present, offset includes 4 * csrc + (4 + 4 * ext_len)
    uint8_t payload;

public:
#define AV_RB16(x) ((((const uint8_t *)(x))[0] << 8) | ((const uint8_t *)(x))[1])

    size_t getCsrcSize() const {
        // Each CSRC takes 4 bytes
        return csrc << 2;
    }

    uint8_t *getCsrcData() {
        if (!csrc) {
            return nullptr;
        }
        return &payload;
    }

    size_t getExtSize() const {
        // RTP has extension
        if (!ext) {
            return 0;
        }
        auto ext_ptr = &payload + getCsrcSize();
        // uint16_t reserved = AV_RB16(ext_ptr);
        // Each extension takes 4 bytes
        return AV_RB16(ext_ptr + 2) << 2;
    }

    uint16_t getExtReserved() const {
        // RTP has extension
        if (!ext) {
            return 0;
        }
        auto ext_ptr = &payload + getCsrcSize();
        return AV_RB16(ext_ptr);
    }

    uint8_t *getExtData() {
        if (!ext) {
            return nullptr;
        }
        auto ext_ptr = &payload + getCsrcSize();
        // The extra 4 bytes are reserved and ext_len respectively
        return ext_ptr + 4;
    }

    size_t getPayloadOffset() const {
        // When extension is present, also ignore the 4 bytes of reserved and ext_len
        return getCsrcSize() + (ext ? (4 + getExtSize()) : 0);
    }

    uint8_t *getPayloadData() {
        return &payload + getPayloadOffset();
    }

    size_t getPaddingSize(size_t rtp_size) const {
        if (!padding) {
            return 0;
        }
        auto end = (uint8_t *)this + rtp_size - 1;
        return *end;
    }

    ssize_t getPayloadSize(size_t rtp_size) const {
        auto invalid_size = getPayloadOffset() + getPaddingSize(rtp_size);
        return (ssize_t)rtp_size - invalid_size - 12;
    }

    std::string dumpString(size_t rtp_size) const {
        std::stringstream printer;
        printer << "version:" << (int)version << "\r\n";
        printer << "padding:" << getPaddingSize(rtp_size) << "\r\n";
        printer << "ext:" << getExtSize() << "\r\n";
        printer << "csrc:" << getCsrcSize() << "\r\n";
        printer << "mark:" << (int)mark << "\r\n";
        printer << "pt:" << (int)pt << "\r\n";
        printer << "seq:" << ntohs(seq) << "\r\n";
        printer << "stamp:" << ntohl(stamp) << "\r\n";
        printer << "ssrc:" << ntohl(ssrc) << "\r\n";
        printer << "rtp size:" << rtp_size << "\r\n";
        printer << "payload offset:" << getPayloadOffset() << "\r\n";
        printer << "payload size:" << getPayloadSize(rtp_size) << "\r\n";
        return printer.str();
    }

    ///////////////////////////////////////////////////////////////////////
} PACKED;

#if defined(_WIN32)
    #pragma pack(pop)
#endif
