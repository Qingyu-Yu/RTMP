#pragma once

#include <cstdint>
#include <vector>

namespace rtmp::protocol {

/**
 * RTMP Message Type ID。
 *
 * 这里仅列出当前服务器实际处理或产生的消息类型。枚举值直接对应 RTMP
 * Message Header 中的 1 字节 Message Type ID，不能随意修改。
 */
enum class MessageType : std::uint8_t {
    // 修改当前连接后续入站 Chunk 的最大负载长度。
    kSetChunkSize = 1,

    // 中止指定 Chunk Stream 上尚未组装完成的消息；当前仅保留类型定义。
    kAbort = 2,

    // 报告累计接收字节数，用于对端流量控制。
    kAcknowledgement = 3,

    // 流开始、Ping Request/Ping Response 等连接或流级控制事件。
    kUserControl = 4,

    // 对端要求收到多少字节后发送一次 Acknowledgement。
    kWindowAcknowledgementSize = 5,

    // 声明对端带宽窗口及限制方式。
    kSetPeerBandwidth = 6,

    // FLV Audio Tag Body，例如 AAC 原始帧或 AAC sequence header。
    kAudio = 8,

    // FLV Video Tag Body，例如 AVC NALU 或 AVC sequence header。
    kVideo = 9,

    // AMF0 数据消息，常用于 onMetaData。
    kDataAmf0 = 18,

    // AMF0 命令消息，承载 connect、publish、play、onStatus 等命令。
    kCommandAmf0 = 20,
};

/**
 * 已完成 Chunk 重组后的 RTMP 消息。
 *
 * Message 是协议层各模块之间传递的统一对象，不包含 TCP 或 muduo 类型。
 * 编码时 ChunkEncoder 会根据这些字段生成 RTMP Basic Header、Message Header
 * 和分片后的 payload。
 */
struct Message {
    // 消息时间戳，单位为毫秒；超过 0xFFFFFF 时使用 Extended Timestamp。
    std::uint32_t timestamp{0};

    // RTMP Message Stream ID。0 通常表示连接级控制，1 及以上表示媒体流。
    std::uint32_t stream_id{0};

    // Chunk Stream ID，用于区分控制、命令、音频和视频等逻辑通道。
    std::uint32_t chunk_stream_id{3};

    // Message Header 中的消息类型。
    MessageType type{MessageType::kCommandAmf0};

    // 完整消息体；对于音视频消息，其内容与 FLV Tag Body 兼容。
    std::vector<std::uint8_t> payload;
};

}  // namespace rtmp::protocol
