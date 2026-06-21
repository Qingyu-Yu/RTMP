#pragma once

#include "rtmp/chunk/chunk_codec.h"
#include "rtmp/protocol/message.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rtmp::session {

/**
 * 协议会话向服务器编排层发出的领域事件。
 *
 * RtmpSession 不直接访问全局流表或 TcpConnection，而是通过事件请求上层
 * 完成发布注册、播放订阅和媒体转发，从而保持协议层可独立测试。
 */
enum class EventType {
    // 客户端请求成为某个 stream key 的发布者。
    kPublish,

    // 客户端请求订阅某个 stream key。
    kPlay,

    // 发布端产生音频、视频或 metadata 消息。
    kMedia,

    // 客户端主动删除或关闭当前 NetStream。
    kCloseStream,
};

struct Event {
    // 事件语义。
    EventType type{EventType::kCloseStream};

    // 规范化后的 "application/streamName"。
    std::string stream_key;

    // 仅 kMedia 使用；其他事件保持默认空消息。
    protocol::Message message;
};

/**
 * 一次会话输入产生的全部副作用。
 *
 * outbound 是应发回当前客户端的协议消息；events 交给服务器的流管理层。
 * 两者分开可避免 RtmpSession 同时承担 socket I/O 和跨连接路由。
 */
struct ProcessResult {
    bool ok{true};
    std::string error;
    std::vector<protocol::Message> outbound;
    std::vector<Event> events;
};

/**
 * 单条已完成握手的 RTMP 连接会话。
 *
 * 负责 Chunk 解码、控制消息、AMF0 命令和连接级状态；不负责握手、不持有
 * TcpConnection，也不管理其他客户端。每条连接必须使用独立实例。
 */
class RtmpSession {
public:
    // 消费握手后的 RTMP 字节，支持 TCP 半包和多个消息粘包。
    [[nodiscard]] ProcessResult consume(const char* data, std::size_t size);

    // 根据流注册结果生成 NetStream.Publish.Start 或 BadName。
    [[nodiscard]] protocol::Message publishStatus(bool accepted) const;

    // 生成 Stream Begin 以及播放成功/失败所需的 onStatus 消息序列。
    [[nodiscard]] std::vector<protocol::Message> playStatus(bool found) const;

    // 使用本连接协商的出站 Chunk Size 编码消息。
    [[nodiscard]] std::string encode(const protocol::Message& message) const;

    [[nodiscard]] const std::string& application() const noexcept {
        return application_;
    }
    [[nodiscard]] const std::string& streamName() const noexcept {
        return stream_name_;
    }

private:
    // 按 Message Type 分派控制、命令和媒体消息。
    void handleMessage(const protocol::Message& message,
                       ProcessResult& result);

    // 解码并处理 AMF0 命令调用。
    void handleCommand(const protocol::Message& message,
                       ProcessResult& result);

    // 本连接独享的 Chunk 解码状态。
    chunk::ChunkDecoder decoder_;

    // connect 命令中的 app，例如 "live"。
    std::string application_;

    // publish/play 命令中的流名，已移除查询参数和前导斜杠。
    std::string stream_name_;

    // createStream 后用于 NetStream 消息的 Message Stream ID。
    std::uint32_t stream_id_{1};

    // 服务端出站分片大小；connect 成功时通过 Set Chunk Size 通知客户端。
    std::uint32_t outbound_chunk_size_{4096};

    // 客户端要求的确认窗口；0 表示尚未协商。
    std::uint32_t acknowledgement_window_{0};

    // 进入会话层的累计字节数，用于生成 Acknowledgement sequence number。
    std::uint64_t received_bytes_{0};

    // 最近一次发送确认时的累计字节数。
    std::uint64_t last_acknowledged_bytes_{0};
};

}  // namespace rtmp::session
