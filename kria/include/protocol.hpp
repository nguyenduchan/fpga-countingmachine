#pragma once

// Ethernet camera wire protocol, version 1.
// Little-endian multi-byte integers. Magic bytes are ASCII "ETHC" in that order.
//
// Header (12 bytes):
//   char    magic[4]      = 'E','T','H','C'
//   uint16  version       = 1
//   uint16  type
//   uint32  payload_len
//
// Types:
//   1 HELLO        server -> client, once after accept
//   2 START        client -> server, begin a capture session
//   3 STOP         client -> server, end the current session
//   4 FRAME        server -> client, one JPEG frame
//   5 SESSION_END  server -> client, session closed
//   6 ERROR        server -> client, UTF-8 message
//
// HELLO payload:
//   uint16 board_name_len + utf8
//   uint16 ubuntu_len + utf8
//   uint16 camera_len + utf8
//   uint32 width
//   uint32 height
//   uint32 fps
//
// START / STOP payload:
//   uint32 session_id
//
// FRAME payload:
//   uint32 session_id
//   uint32 frame_index
//   uint64 timestamp_us   (Unix epoch)
//   uint32 width
//   uint32 height
//   uint32 format         (1 = JPEG)
//   uint32 data_len
//   uint8  data[data_len]
//
// SESSION_END payload:
//   uint32 session_id
//   uint32 frame_count
//   uint64 duration_us

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ethcam {

constexpr uint8_t kMagic[4] = {'E', 'T', 'H', 'C'};
constexpr uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr uint32_t kMaxPayload = 8u * 1024u * 1024u;

constexpr uint16_t kHello = 1;
constexpr uint16_t kStart = 2;
constexpr uint16_t kStop = 3;
constexpr uint16_t kFrame = 4;
constexpr uint16_t kSessionEnd = 5;
constexpr uint16_t kError = 6;
constexpr uint32_t kFormatJpeg = 1;
constexpr uint32_t kFormatStrips = 2;

inline void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

inline void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

inline void append_u64(std::vector<uint8_t>& out, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
}

inline void append_str(std::vector<uint8_t>& out, const std::string& text) {
    const auto length = static_cast<uint16_t>(text.size() > 65535 ? 65535 : text.size());
    append_u16(out, length);
    out.insert(out.end(), text.begin(), text.begin() + length);
}

inline uint16_t load_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

inline uint32_t load_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

inline bool magic_ok(const uint8_t* header) {
    return std::memcmp(header, kMagic, 4) == 0;
}

inline std::vector<uint8_t> encode_message(uint16_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> message;
    message.reserve(kHeaderSize + payload.size());
    message.insert(message.end(), kMagic, kMagic + 4);
    append_u16(message, kVersion);
    append_u16(message, type);
    append_u32(message, static_cast<uint32_t>(payload.size()));
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

}  // namespace ethcam
