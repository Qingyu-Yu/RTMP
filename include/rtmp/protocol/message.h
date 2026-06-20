#pragma once

#include <cstdint>
#include <vector>

namespace rtmp::protocol {

enum class MessageType : std::uint8_t {
    kSetChunkSize = 1,
    kAbort = 2,
    kAcknowledgement = 3,
    kUserControl = 4,
    kWindowAcknowledgementSize = 5,
    kSetPeerBandwidth = 6,
    kAudio = 8,
    kVideo = 9,
    kDataAmf0 = 18,
    kCommandAmf0 = 20,
};

struct Message {
    std::uint32_t timestamp{0};
    std::uint32_t stream_id{0};
    std::uint32_t chunk_stream_id{3};
    MessageType type{MessageType::kCommandAmf0};
    std::vector<std::uint8_t> payload;
};

}  // namespace rtmp::protocol
