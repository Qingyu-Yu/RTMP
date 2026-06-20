#include "rtmp/chunk/chunk_codec.h"

#include <algorithm>
#include <limits>

namespace rtmp::chunk {
namespace {

std::uint32_t readBe24(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 16U) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           static_cast<std::uint32_t>(data[2]);
}

std::uint32_t readBe32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint32_t readLe32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void appendBe24(std::string& output, std::uint32_t value) {
    output.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

void appendBe32(std::string& output, std::uint32_t value) {
    output.push_back(static_cast<char>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

void appendBasicHeader(std::string& output, std::uint8_t format,
                       std::uint32_t chunk_stream_id) {
    if (chunk_stream_id >= 2 && chunk_stream_id <= 63) {
        output.push_back(static_cast<char>(
            (static_cast<std::uint32_t>(format) << 6U) | chunk_stream_id));
    } else if (chunk_stream_id <= 319) {
        output.push_back(static_cast<char>(
            static_cast<std::uint32_t>(format) << 6U));
        output.push_back(static_cast<char>(chunk_stream_id - 64));
    } else {
        const std::uint32_t encoded = chunk_stream_id - 64;
        output.push_back(static_cast<char>(
            (static_cast<std::uint32_t>(format) << 6U) | 1U));
        output.push_back(static_cast<char>(encoded & 0xFFU));
        output.push_back(static_cast<char>((encoded >> 8U) & 0xFFU));
    }
}

}  // namespace

ChunkDecoder::ChunkDecoder(std::size_t max_message_size)
    : max_message_size_(max_message_size) {}

DecodeResult ChunkDecoder::consume(const char* data, std::size_t size) {
    DecodeResult result;
    if (data != nullptr && size > 0) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
        input_.insert(input_.end(), begin, begin + size);
    }

    std::size_t cursor = 0;
    while (cursor < input_.size()) {
        const std::size_t chunk_begin = cursor;
        if (input_.size() - cursor < 1) {
            break;
        }

        const std::uint8_t basic = input_[cursor++];
        const std::uint8_t format = basic >> 6U;
        std::uint32_t chunk_stream_id = basic & 0x3FU;
        if (chunk_stream_id == 0) {
            if (input_.size() - cursor < 1) {
                cursor = chunk_begin;
                break;
            }
            chunk_stream_id = 64U + input_[cursor++];
        } else if (chunk_stream_id == 1) {
            if (input_.size() - cursor < 2) {
                cursor = chunk_begin;
                break;
            }
            chunk_stream_id =
                64U + input_[cursor] +
                (static_cast<std::uint32_t>(input_[cursor + 1]) << 8U);
            cursor += 2;
        }

        auto found = streams_.find(chunk_stream_id);
        const bool has_state = found != streams_.end() && found->second.initialized;
        if (format != 0 && !has_state) {
            result.ok = false;
            result.error = "compressed chunk header has no previous state";
            break;
        }

        ChunkStreamState next =
            has_state ? found->second : ChunkStreamState{};
        const bool continuation = has_state && !next.payload.empty();
        if (continuation && format != 3) {
            result.ok = false;
            result.error =
                "new chunk header interrupted an incomplete RTMP message";
            break;
        }
        const std::size_t header_size =
            format == 0 ? 11U : (format == 1 ? 7U : (format == 2 ? 3U : 0U));
        if (input_.size() - cursor < header_size) {
            cursor = chunk_begin;
            break;
        }

        std::uint32_t timestamp_field = 0;
        if (format <= 2) {
            timestamp_field = readBe24(input_.data() + cursor);
        }
        if (format == 0) {
            next.message_length = readBe24(input_.data() + cursor + 3);
            next.message_type = input_[cursor + 6];
            next.message_stream_id = readLe32(input_.data() + cursor + 7);
            next.timestamp_delta = 0;
            if (!continuation) {
                next.payload.clear();
            }
        } else if (format == 1) {
            next.message_length = readBe24(input_.data() + cursor + 3);
            next.message_type = input_[cursor + 6];
            if (!continuation) {
                next.payload.clear();
            }
        } else if (format == 2 && !continuation) {
            next.payload.clear();
        } else if (format == 3 && !continuation) {
            next.payload.clear();
        }
        cursor += header_size;

        const bool extended =
            format <= 2 ? timestamp_field == 0xFFFFFFU
                        : next.extended_timestamp;
        std::uint32_t extended_value = 0;
        if (extended) {
            if (input_.size() - cursor < 4) {
                cursor = chunk_begin;
                break;
            }
            extended_value = readBe32(input_.data() + cursor);
            cursor += 4;
        }

        if (format == 0) {
            next.timestamp = extended ? extended_value : timestamp_field;
            next.extended_timestamp = extended;
        } else if (format == 1 || format == 2) {
            const std::uint32_t delta = extended ? extended_value : timestamp_field;
            next.timestamp_delta = delta;
            if (!continuation) {
                next.timestamp += delta;
            }
            next.extended_timestamp = extended;
        } else if (!continuation) {
            next.timestamp += next.timestamp_delta;
        }

        if (next.message_length > max_message_size_) {
            result.ok = false;
            result.error = "RTMP message exceeds configured size limit";
            break;
        }
        if (next.payload.size() > next.message_length) {
            result.ok = false;
            result.error = "invalid RTMP chunk message length";
            break;
        }

        const std::size_t remaining =
            next.message_length - next.payload.size();
        const std::size_t payload_size =
            std::min<std::size_t>(remaining, chunk_size_);
        if (input_.size() - cursor < payload_size) {
            cursor = chunk_begin;
            break;
        }

        next.payload.insert(next.payload.end(), input_.begin() + cursor,
                            input_.begin() + cursor + payload_size);
        cursor += payload_size;
        next.initialized = true;
        streams_[chunk_stream_id] = next;

        if (next.payload.size() == next.message_length) {
            protocol::Message message;
            message.timestamp = next.timestamp;
            message.stream_id = next.message_stream_id;
            message.chunk_stream_id = chunk_stream_id;
            message.type =
                static_cast<protocol::MessageType>(next.message_type);
            message.payload = std::move(streams_[chunk_stream_id].payload);
            streams_[chunk_stream_id].payload.clear();

            if (message.type == protocol::MessageType::kSetChunkSize &&
                message.payload.size() == 4) {
                const std::uint32_t requested =
                    readBe32(message.payload.data()) & 0x7FFFFFFFU;
                if (requested == 0) {
                    result.ok = false;
                    result.error = "RTMP chunk size must be greater than zero";
                    break;
                }
                chunk_size_ = requested;
            }
            result.messages.push_back(std::move(message));
        }
    }

    if (cursor > 0) {
        input_.erase(input_.begin(), input_.begin() + cursor);
    }
    return result;
}

std::string ChunkEncoder::encode(const protocol::Message& message,
                                 std::uint32_t chunk_size) {
    if (chunk_size == 0 || message.payload.size() > 0xFFFFFFU) {
        return {};
    }

    const std::uint32_t timestamp_field =
        std::min(message.timestamp, 0xFFFFFFU);
    const bool extended = message.timestamp >= 0xFFFFFFU;
    std::string output;
    output.reserve(message.payload.size() + 32);

    std::size_t offset = 0;
    bool first = true;
    do {
        appendBasicHeader(output, first ? 0 : 3, message.chunk_stream_id);
        if (first) {
            appendBe24(output, timestamp_field);
            appendBe24(output,
                       static_cast<std::uint32_t>(message.payload.size()));
            output.push_back(static_cast<char>(message.type));
            output.push_back(static_cast<char>(message.stream_id & 0xFFU));
            output.push_back(
                static_cast<char>((message.stream_id >> 8U) & 0xFFU));
            output.push_back(
                static_cast<char>((message.stream_id >> 16U) & 0xFFU));
            output.push_back(
                static_cast<char>((message.stream_id >> 24U) & 0xFFU));
        }
        if (extended) {
            appendBe32(output, message.timestamp);
        }

        const std::size_t amount = std::min<std::size_t>(
            chunk_size, message.payload.size() - offset);
        if (amount > 0) {
            output.append(
                reinterpret_cast<const char*>(message.payload.data() + offset),
                amount);
        }
        offset += amount;
        first = false;
    } while (offset < message.payload.size());

    return output;
}

}  // namespace rtmp::chunk
