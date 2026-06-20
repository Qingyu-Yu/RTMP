#pragma once

#include "rtmp/protocol/message.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtmp::chunk {

struct DecodeResult {
    std::vector<protocol::Message> messages;
    bool ok{true};
    std::string error;
};

class ChunkDecoder {
public:
    explicit ChunkDecoder(std::size_t max_message_size = 16U * 1024U * 1024U);

    [[nodiscard]] DecodeResult consume(const char* data, std::size_t size);
    [[nodiscard]] std::uint32_t chunkSize() const noexcept { return chunk_size_; }

private:
    struct ChunkStreamState {
        bool initialized{false};
        bool extended_timestamp{false};
        std::uint32_t timestamp{0};
        std::uint32_t timestamp_delta{0};
        std::uint32_t message_length{0};
        std::uint32_t message_stream_id{0};
        std::uint8_t message_type{0};
        std::vector<std::uint8_t> payload;
    };

    std::vector<std::uint8_t> input_;
    std::unordered_map<std::uint32_t, ChunkStreamState> streams_;
    std::uint32_t chunk_size_{128};
    std::size_t max_message_size_;
};

class ChunkEncoder {
public:
    [[nodiscard]] static std::string encode(
        const protocol::Message& message,
        std::uint32_t chunk_size = 4096);
};

}  // namespace rtmp::chunk
