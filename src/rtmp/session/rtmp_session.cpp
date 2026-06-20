#include "rtmp/session/rtmp_session.h"

#include "rtmp/amf/amf0.h"

#include <algorithm>
#include <utility>

namespace rtmp::session {
namespace {

using protocol::Message;
using protocol::MessageType;

void appendBe16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void appendBe32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t readBe16(const std::vector<std::uint8_t>& input,
                       std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8U) |
        input[offset + 1]);
}

std::uint32_t readBe32(const std::vector<std::uint8_t>& input,
                       std::size_t offset = 0) {
    return (static_cast<std::uint32_t>(input[offset]) << 24U) |
           (static_cast<std::uint32_t>(input[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(input[offset + 2]) << 8U) |
           input[offset + 3];
}

Message controlMessage(MessageType type, std::vector<std::uint8_t> payload) {
    Message message;
    message.chunk_stream_id = 2;
    message.type = type;
    message.payload = std::move(payload);
    return message;
}

Message commandMessage(std::uint32_t stream_id,
                       std::vector<amf::Value> values) {
    Message message;
    message.stream_id = stream_id;
    message.chunk_stream_id = stream_id == 0 ? 3 : 5;
    message.type = MessageType::kCommandAmf0;
    message.payload = amf::encode(values);
    return message;
}

Message userControl(std::uint16_t event_type, std::uint32_t stream_id) {
    std::vector<std::uint8_t> payload;
    appendBe16(payload, event_type);
    appendBe32(payload, stream_id);
    return controlMessage(MessageType::kUserControl, std::move(payload));
}

amf::Value statusObject(const std::string& level, const std::string& code,
                        const std::string& description) {
    return amf::Value::object({
        {"level", amf::Value(level)},
        {"code", amf::Value(code)},
        {"description", amf::Value(description)},
    });
}

std::string normalizedStreamName(std::string name) {
    const auto query = name.find('?');
    if (query != std::string::npos) {
        name.resize(query);
    }
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }
    return name;
}

}  // namespace

ProcessResult RtmpSession::consume(const char* data, std::size_t size) {
    ProcessResult result;
    received_bytes_ += size;
    auto decoded = decoder_.consume(data, size);
    if (!decoded.ok) {
        result.ok = false;
        result.error = std::move(decoded.error);
        return result;
    }
    for (const auto& message : decoded.messages) {
        handleMessage(message, result);
        if (!result.ok) {
            break;
        }
    }
    if (result.ok && acknowledgement_window_ > 0 &&
        received_bytes_ - last_acknowledged_bytes_ >=
            acknowledgement_window_) {
        std::vector<std::uint8_t> sequence;
        appendBe32(sequence, static_cast<std::uint32_t>(received_bytes_));
        result.outbound.push_back(controlMessage(
            MessageType::kAcknowledgement, std::move(sequence)));
        last_acknowledged_bytes_ = received_bytes_;
    }
    return result;
}

void RtmpSession::handleMessage(const Message& message,
                                ProcessResult& result) {
    switch (message.type) {
        case MessageType::kCommandAmf0:
            handleCommand(message, result);
            break;
        case MessageType::kAudio:
        case MessageType::kVideo:
        case MessageType::kDataAmf0:
            if (!stream_name_.empty()) {
                result.events.push_back(
                    {EventType::kMedia,
                     application_ + "/" + stream_name_,
                     message});
            }
            break;
        case MessageType::kWindowAcknowledgementSize:
            if (message.payload.size() == 4) {
                acknowledgement_window_ = readBe32(message.payload);
            }
            break;
        case MessageType::kUserControl:
            if (message.payload.size() >= 6 &&
                readBe16(message.payload, 0) == 6) {
                result.outbound.push_back(
                    userControl(7, readBe32(message.payload, 2)));
            }
            break;
        default:
            break;
    }
}

void RtmpSession::handleCommand(const Message& message,
                                ProcessResult& result) {
    std::vector<amf::Value> values;
    std::string error;
    if (!amf::decode(message.payload, values, &error) || values.empty() ||
        values[0].type() != amf::Value::Type::kString) {
        result.ok = false;
        result.error = error.empty() ? "invalid RTMP command" : error;
        return;
    }

    const std::string command = values[0].string();
    const double transaction =
        values.size() > 1 ? values[1].number() : 0;

    if (command == "connect") {
        if (values.size() > 2) {
            if (const auto* app = values[2].find("app")) {
                application_ = normalizedStreamName(app->string());
            }
        }

        std::vector<std::uint8_t> chunk_size;
        appendBe32(chunk_size, outbound_chunk_size_);
        result.outbound.push_back(
            controlMessage(MessageType::kSetChunkSize, std::move(chunk_size)));

        std::vector<std::uint8_t> window_size;
        appendBe32(window_size, 5000000);
        result.outbound.push_back(controlMessage(
            MessageType::kWindowAcknowledgementSize, window_size));

        window_size.push_back(2);
        result.outbound.push_back(controlMessage(
            MessageType::kSetPeerBandwidth, std::move(window_size)));

        result.outbound.push_back(commandMessage(
            0,
            {amf::Value("_result"),
             amf::Value(transaction),
             amf::Value::object({
                 {"fmsVer", amf::Value("FMS/3,5,7,7009")},
                 {"capabilities", amf::Value(31.0)},
             }),
             amf::Value::object({
                 {"level", amf::Value("status")},
                 {"code", amf::Value("NetConnection.Connect.Success")},
                 {"description", amf::Value("Connection succeeded.")},
                 {"objectEncoding", amf::Value(0.0)},
             })}));
        return;
    }

    if (command == "createStream") {
        result.outbound.push_back(commandMessage(
            0, {amf::Value("_result"), amf::Value(transaction),
                amf::Value{}, amf::Value(static_cast<double>(stream_id_))}));
        return;
    }

    if (command == "releaseStream" || command == "FCPublish" ||
        command == "FCUnpublish") {
        result.outbound.push_back(commandMessage(
            0, {amf::Value("_result"), amf::Value(transaction),
                amf::Value{}, amf::Value{}}));
        return;
    }

    if (command == "publish" || command == "play") {
        if (values.size() < 4 ||
            values[3].type() != amf::Value::Type::kString) {
            result.ok = false;
            result.error = command + " command has no stream name";
            return;
        }
        stream_id_ = message.stream_id == 0 ? 1 : message.stream_id;
        stream_name_ = normalizedStreamName(values[3].string());
        if (application_.empty() || stream_name_.empty()) {
            result.ok = false;
            result.error = "RTMP application or stream name is empty";
            return;
        }
        result.events.push_back(
            {command == "publish" ? EventType::kPublish : EventType::kPlay,
             application_ + "/" + stream_name_, {}});
        return;
    }

    if (command == "deleteStream" || command == "closeStream") {
        if (!stream_name_.empty()) {
            result.events.push_back(
                {EventType::kCloseStream,
                 application_ + "/" + stream_name_, {}});
        }
    }
}

Message RtmpSession::publishStatus(bool accepted) const {
    return commandMessage(
        stream_id_,
        {amf::Value("onStatus"), amf::Value(0.0), amf::Value{},
         statusObject(
             accepted ? "status" : "error",
             accepted ? "NetStream.Publish.Start"
                      : "NetStream.Publish.BadName",
             accepted ? "Publishing started."
                      : "The stream is already being published.")});
}

std::vector<Message> RtmpSession::playStatus(bool found) const {
    std::vector<Message> messages;
    messages.push_back(userControl(0, stream_id_));
    if (!found) {
        messages.push_back(commandMessage(
            stream_id_,
            {amf::Value("onStatus"), amf::Value(0.0), amf::Value{},
             statusObject("error", "NetStream.Play.StreamNotFound",
                          "The requested stream was not found.")}));
        return messages;
    }
    messages.push_back(commandMessage(
        stream_id_,
        {amf::Value("onStatus"), amf::Value(0.0), amf::Value{},
         statusObject("status", "NetStream.Play.Reset",
                      "Playing and resetting stream.")}));
    messages.push_back(commandMessage(
        stream_id_,
        {amf::Value("onStatus"), amf::Value(0.0), amf::Value{},
         statusObject("status", "NetStream.Play.Start",
                      "Playback started.")}));
    return messages;
}

std::string RtmpSession::encode(const Message& message) const {
    return chunk::ChunkEncoder::encode(message, outbound_chunk_size_);
}

}  // namespace rtmp::session
