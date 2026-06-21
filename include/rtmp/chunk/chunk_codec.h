#pragma once

#include "rtmp/protocol/message.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtmp::chunk {

/**
 * 单次 consume() 的解析结果。
 *
 * 一次 TCP 输入可能包含多个完整 RTMP Message，也可能只包含某个 Chunk 的
 * 一部分。半包不是错误，此时 messages 可以为空且 ok 仍为 true。
 */
struct DecodeResult {
    // 本次输入使其组装完成的全部消息，保持线上到达顺序。
    std::vector<protocol::Message> messages;

    // false 表示协议格式或资源限制错误，当前连接应终止。
    bool ok{true};

    // ok == false 时提供错误原因。
    std::string error;
};

/**
 * 有状态的 RTMP Chunk 解码器。
 *
 * 每条 RTMP 连接必须拥有独立实例。解码器缓存 TCP 半包，并为每个 Chunk
 * Stream ID 保存上一条消息头；这是解析 fmt 1/2/3 压缩头所必需的状态。
 */
class ChunkDecoder {
public:
    // max_message_size 限制单条消息的内存占用，防止恶意长度字段导致大分配。
    explicit ChunkDecoder(std::size_t max_message_size = 16U * 1024U * 1024U);

    /**
     * 追加并解析网络字节。
     *
     * 函数会在内部保留不完整数据，因此调用者在返回后可移除传入的全部
     * 字节。Set Chunk Size 消息会在输出该消息的同时更新后续入站分片大小。
     */
    [[nodiscard]] DecodeResult consume(const char* data, std::size_t size);

    // 当前入站 Chunk Size；RTMP 默认值为 128。
    [[nodiscard]] std::uint32_t chunkSize() const noexcept { return chunk_size_; }

private:
    /**
     * 一个 Chunk Stream 的压缩头和消息组装状态。
     *
     * RTMP 允许多个 csid 交错传输，因此不能只保存“上一条全局消息头”。
     */
    struct ChunkStreamState {
        // 是否已经收到 fmt 0 完整头，决定压缩头能否引用该状态。
        bool initialized{false};

        // 上一头是否使用 Extended Timestamp，fmt 3 需要继承该信息。
        bool extended_timestamp{false};

        // 当前正在组装或最近完成消息的绝对时间戳。
        std::uint32_t timestamp{0};

        // 最近的时间戳增量，供新消息的 fmt 3 头复用。
        std::uint32_t timestamp_delta{0};

        // 当前消息声明的完整 payload 长度。
        std::uint32_t message_length{0};

        // Message Stream ID；RTMP 规定此字段在线上使用小端序。
        std::uint32_t message_stream_id{0};

        // 原始 1 字节 Message Type ID。
        std::uint8_t message_type{0};

        // 跨多个 Chunk 累积的当前消息体。
        std::vector<std::uint8_t> payload;
    };

    // 尚不足以解析成完整 Chunk 的 TCP 尾部数据。
    std::vector<std::uint8_t> input_;

    // 按 csid 隔离的消息头及重组状态。
    std::unordered_map<std::uint32_t, ChunkStreamState> streams_;

    // 对端通过 Set Chunk Size 修改；初始值来自 RTMP 规范。
    std::uint32_t chunk_size_{128};

    // 单条消息允许的最大长度。
    std::size_t max_message_size_;
};

/**
 * 无状态 RTMP Chunk 编码器。
 *
 * 当前实现首个 Chunk 使用 fmt 0，后续分片使用 fmt 3。这样编码逻辑明确，
 * 不依赖跨消息缓存，同时仍兼容标准 RTMP 客户端。
 */
class ChunkEncoder {
public:
    // 返回可直接写入 TCP 的二进制字符串；参数非法时返回空字符串。
    [[nodiscard]] static std::string encode(
        const protocol::Message& message,
        std::uint32_t chunk_size = 4096);
};

}  // namespace rtmp::chunk
