#pragma once

#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <string>

constexpr std::size_t kMaxFrameSize = 1024 * 1024;

inline std::uint32_t decode_be_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

inline void encode_be_u32(std::uint32_t value, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(value & 0xFF);
}

inline std::string frame_json_payload(const std::string& payload) {
    if (payload.size() > kMaxFrameSize) {
        throw std::runtime_error("Payload exceeds max frame size");
    }

    std::string framed;
    framed.resize(4 + payload.size());

    encode_be_u32(static_cast<std::uint32_t>(payload.size()),
                  reinterpret_cast<std::uint8_t*>(&framed[0]));
    std::copy(payload.begin(), payload.end(), framed.begin() + 4);
    return framed;
}
