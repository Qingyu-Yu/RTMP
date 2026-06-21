#include "rtmp/session/rtmp_session.h"

#include "rtmp/amf/amf0.h"

#include <algorithm>
#include <utility>

namespace rtmp::session {
namespace {

using protocol::Message;
using protocol::MessageType;

// RTMP 控制消息中的整数使用大端编码。
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
    // Chunk Stream 2 按约定用于协议控制消息，Message Stream ID 保持 0。
    Message message;
    message.chunk_stream_id = 2;
    message.type = type;
    message.payload = std::move(payload);
    return message;
}

Message commandMessage(std::uint32_t stream_id,
                       std::vector<amf::Value> values) {
    // 连接级命令放在 csid 3，NetStream 级命令放在独立的 csid 5。
    Message message;
    message.stream_id = stream_id;
    message.chunk_stream_id = stream_id == 0 ? 3 : 5;
    message.type = MessageType::kCommandAmf0;
    message.payload = amf::encode(values);
    return message;
}

Message userControl(std::uint16_t event_type, std::uint32_t stream_id) {
    // User Control payload = 2 字节事件类型 + 4 字节事件数据。
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
    // 查询参数通常用于鉴权，不应成为流注册表 key 的一部分。
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
    // Acknowledgement sequence number 统计进入 RTMP 会话层的原始字节。
    received_bytes_ += size;
    auto decoded = decoder_.consume(data, size);
    if (!decoded.ok) {
        result.ok = false;
        result.error = std::move(decoded.error);
        return result;
    }
    for (const auto& message : decoded.messages) {
        // 一个 TCP 包可以重组出多个消息，按线上顺序逐个处理。
        handleMessage(message, result);
        if (!result.ok) {
            break;
        }
    }
    if (result.ok && acknowledgement_window_ > 0 &&
        received_bytes_ - last_acknowledged_bytes_ >=
            acknowledgement_window_) {
        // 达到客户端声明的窗口后报告低 32 位累计字节数。
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
            // 只有 publish/play 已确定 stream key 后，媒体消息才有路由目标。
            if (!stream_name_.empty()) {
                result.events.push_back(
                    {EventType::kMedia,
                     application_ + "/" + stream_name_,
                     message});
            }
            break;
        case MessageType::kWindowAcknowledgementSize:
            // 客户端可随时重新声明窗口，后续确认使用新值。
            if (message.payload.size() == 4) {
                acknowledgement_window_ = readBe32(message.payload);
            }
            break;
        case MessageType::kUserControl:
            // Event Type 6 是 Ping Request，响应必须复用相同的 4 字节时间值。
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
    // RTMP AMF0 命令的第一个值必须是命令名字符串，第二个通常是事务 ID。
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
        // connect 的 command object 中 app 决定后续流所属应用命名空间。
        if (values.size() > 2) {
            if (const auto* app = values[2].find("app")) {
                application_ = normalizedStreamName(app->string());
            }
        }

        // 先发送协议控制参数，再发送 connect 的 _result；该顺序兼容常见
        // FFmpeg/Flash 客户端，并让后续较大响应使用新的出站 Chunk Size。
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
        // 当前连接实现单 NetStream 模型，固定分配 Message Stream ID 1。
        result.outbound.push_back(commandMessage(
            0, {amf::Value("_result"), amf::Value(transaction),
                amf::Value{}, amf::Value(static_cast<double>(stream_id_))}));
        return;
    }

    if (command == "releaseStream" || command == "FCPublish" ||
        command == "FCUnpublish") {
        // 这些是常见推流客户端的兼容性预命令，不改变本地流状态。
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
        // 部分客户端可能错误地在 stream 0 上发送命令，回退到已分配的 1。
        stream_id_ = message.stream_id == 0 ? 1 : message.stream_id;
        stream_name_ = normalizedStreamName(values[3].string());
        if (application_.empty() || stream_name_.empty()) {
            result.ok = false;
            result.error = "RTMP application or stream name is empty";
            return;
        }
        // 是否允许发布、目标流是否存在由全局 StreamRegistry 决定。
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
    // onStatus 不使用事务匹配，transaction id 按惯例为 0。
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
    // User Control Stream Begin 必须先于 NetStream.Play.* 状态消息。
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
