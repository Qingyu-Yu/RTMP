#pragma once

#include "rtmp/chunk/chunk_codec.h"
#include "rtmp/protocol/message.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rtmp::session {

enum class EventType {
    kPublish,
    kPlay,
    kMedia,
    kCloseStream,
};

struct Event {
    EventType type{EventType::kCloseStream};
    std::string stream_key;
    protocol::Message message;
};

struct ProcessResult {
    bool ok{true};
    std::string error;
    std::vector<protocol::Message> outbound;
    std::vector<Event> events;
};

class RtmpSession {
public:
    [[nodiscard]] ProcessResult consume(const char* data, std::size_t size);

    [[nodiscard]] protocol::Message publishStatus(bool accepted) const;
    [[nodiscard]] std::vector<protocol::Message> playStatus(bool found) const;
    [[nodiscard]] std::string encode(const protocol::Message& message) const;

    [[nodiscard]] const std::string& application() const noexcept {
        return application_;
    }
    [[nodiscard]] const std::string& streamName() const noexcept {
        return stream_name_;
    }

private:
    void handleMessage(const protocol::Message& message,
                       ProcessResult& result);
    void handleCommand(const protocol::Message& message,
                       ProcessResult& result);

    chunk::ChunkDecoder decoder_;
    std::string application_;
    std::string stream_name_;
    std::uint32_t stream_id_{1};
    std::uint32_t outbound_chunk_size_{4096};
    std::uint32_t acknowledgement_window_{0};
    std::uint64_t received_bytes_{0};
    std::uint64_t last_acknowledged_bytes_{0};
};

}  // namespace rtmp::session
