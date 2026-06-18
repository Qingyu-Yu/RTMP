#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rtmp::handshake {

// RTMP 协议版本。当前握手模块只接受 RTMP 3。
inline constexpr std::uint8_t kRtmpVersion = 3;

// C1、S1、C2、S2 每个握手数据块的固定长度。
inline constexpr std::size_t kHandshakeBlockSize = 1536;

// 客户端首次发送的数据长度：C0(1 字节) + C1(1536 字节)。
inline constexpr std::size_t kC0C1Size = 1 + kHandshakeBlockSize;

// 服务端握手响应长度：S0(1 字节) + S1(1536 字节) + S2(1536 字节)。
inline constexpr std::size_t kS0S1S2Size = 1 + 2 * kHandshakeBlockSize;

// 握手块中随机数据的长度。前 8 字节分别用于 time 和 time2。
inline constexpr std::size_t kRandomSize = kHandshakeBlockSize - 8;

/**
 * 表示一条 RTMP 连接当前所处的握手阶段。
 *
 * 状态只能按以下方向迁移：
 * kWaitingC0C1 -> kWaitingC2 -> kComplete
 *                         \----> kFailed
 */
enum class HandshakeState {
    // 等待客户端发送完整的 C0+C1。
    kWaitingC0C1,

    // 已向客户端生成 S0+S1+S2，等待客户端发送 C2。
    kWaitingC2,

    // C2 校验成功，简单握手完成。
    kComplete,

    // 握手数据不符合协议，当前会话不再继续消费数据。
    kFailed,
};

// 握手失败的具体原因，供网络适配层记录日志或关闭连接。
enum class HandshakeError {
    // 没有错误。
    kNone,

    // C0 中的版本号不是当前支持的 RTMP 3。
    kUnsupportedVersion,

    // simple handshake 要求 C1 的 time2 字段为 0。
    kInvalidC1ReservedField,

    // C2 没有正确回显服务端发送的 S1。
    kInvalidC2,
};

/**
 * consume() 单次调用的处理结果。
 *
 * 该结构同时返回“消费了多少输入”和“需要发送什么响应”，使协议状态机
 * 不需要直接依赖 muduo::Buffer 或 TcpConnection。
 */
struct ConsumeResult {
    // 本次从调用者输入中消费的字节数。调用者只能从网络 Buffer 移除这些字节。
    std::size_t consumed{0};

    // 本次需要发送给客户端的数据。收到完整 C0+C1 时为 S0+S1+S2。
    std::string response;

    // 本次处理结束后的握手状态。
    HandshakeState state{HandshakeState::kWaitingC0C1};

    // 本次处理结束后的错误状态；握手正常时为 kNone。
    HandshakeError error{HandshakeError::kNone};
};

/**
 * 管理单条 RTMP 连接的简单握手状态。
 *
 * 类本身只处理字节流和协议状态，不负责 socket 收发、连接管理和日志。
 * 因此它可以独立进行单元测试，也可以适配其他网络库。
 *
 * 每个 TcpConnection 必须对应一个独立的 HandshakeSession，不应在多个连接
 * 之间共享同一个实例。
 */
class HandshakeSession {
public:
    /**
     * 创建一条新的握手会话。
     *
     * @param server_time   写入 S1.time 的服务端时间。
     * @param server_random 写入 S1.random 的随机数据；后续用于校验 C2。
     *
     * 将时间和随机数作为构造参数传入，而不是在状态机内部生成，可以降低
     * 协议层与系统时钟、随机数设施的耦合，并使单元测试结果可重复。
     */
    HandshakeSession(
        std::uint32_t server_time,
        const std::array<std::byte, kRandomSize>& server_random);

    /**
     * 消费一段来自客户端的网络数据。
     *
     * 函数内部会缓存不完整的 C0+C1 或 C2，因此允许调用者传入 TCP 半包。
     * 如果一次输入中同时包含 C0+C1 和 C2，函数会连续处理，支持粘包。
     *
     * 握手完成后函数立即停止消费。输入末尾可能存在的 RTMP Chunk 数据不会
     * 被取走，调用者可将其交给下一层协议解析器。
     *
     * @param data 输入数据首地址；size 为 0 时允许传入 nullptr。
     * @param size 当前可读数据长度。
     * @return 本次消费字节数、待发送响应、最新状态和错误信息。
     */
    [[nodiscard]] ConsumeResult consume(const char* data, std::size_t size);

    // 返回会话当前状态，不修改对象。
    [[nodiscard]] HandshakeState state() const noexcept { return state_; }

    // 返回最近一次握手错误；未发生错误时返回 kNone。
    [[nodiscard]] HandshakeError error() const noexcept { return error_; }

private:
    // 解析已经完整缓存的 C0+C1，并生成 S0+S1+S2。
    void processC0C1(ConsumeResult& result);

    // 校验已经完整缓存的 C2，成功后将状态切换为 kComplete。
    void processC2(ConsumeResult& result);

    // 统一处理失败状态：清空临时数据、记录错误并禁止继续处理。
    void fail(HandshakeError error, ConsumeResult& result);

    // 本连接的服务端握手时间，写入 S1.time，并要求 C2.time 回显该值。
    std::uint32_t server_time_;

    // 本连接的服务端随机数据，写入 S1.random，并用于校验 C2.random。
    std::array<std::byte, kRandomSize> server_random_;

    // 跨多次 consume() 调用保存尚未完整到达的 C0+C1 或 C2。
    std::vector<std::byte> input_;

    // 当前握手阶段；新会话从等待 C0+C1 开始。
    HandshakeState state_{HandshakeState::kWaitingC0C1};

    // 最近一次错误；一旦非 kNone，会话状态同时进入 kFailed。
    HandshakeError error_{HandshakeError::kNone};
};

}  // namespace rtmp::handshake
