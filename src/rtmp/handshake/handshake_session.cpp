#include "rtmp/handshake/handshake_session.h"

#include <algorithm>
#include <cstring>

namespace rtmp::handshake {
namespace {

// 从网络字节序（大端）读取一个 32 位无符号整数。
// RTMP 握手中的 time 和 time2 字段均使用大端编码。
std::uint32_t readUint32Be(const std::byte* data) {
    return (std::to_integer<std::uint32_t>(data[0]) << 24U) |
           (std::to_integer<std::uint32_t>(data[1]) << 16U) |
           (std::to_integer<std::uint32_t>(data[2]) << 8U) |
           std::to_integer<std::uint32_t>(data[3]);
}

// 将主机中的 32 位无符号整数写为网络字节序。
void writeUint32Be(std::uint32_t value, std::byte* output) {
    output[0] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    output[1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    output[2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    output[3] = static_cast<std::byte>(value & 0xFFU);
}

// 判断 [begin, end) 范围内是否全部为 0。
// simple handshake 使用该函数检查 C1 的 4 字节 time2/zero 字段。
bool allZero(const std::byte* begin, const std::byte* end) {
    return std::all_of(begin, end, [](std::byte value) {
        return value == std::byte{0};
    });
}

}  // namespace

HandshakeSession::HandshakeSession(
    std::uint32_t server_time,
    const std::array<std::byte, kRandomSize>& server_random)
    : server_time_(server_time), server_random_(server_random) {
    // 第一阶段最多缓存 C0+C1，共 1537 字节。提前 reserve 可以避免半包
    // 多次追加时频繁扩容，但不改变 vector 当前的逻辑长度。
    input_.reserve(kC0C1Size);
}

ConsumeResult HandshakeSession::consume(const char* data, std::size_t size) {
    // 每次调用创建独立结果。先复制当前状态，保证空输入也能返回会话现状。
    ConsumeResult result;
    result.state = state_;
    result.error = error_;

    // 完成或失败均为终止状态，状态机不再消费任何数据。
    // 这样可以防止误把握手后的 Chunk 数据当成新的握手数据。
    if (data == nullptr || size == 0 || state_ == HandshakeState::kComplete ||
        state_ == HandshakeState::kFailed) {
        return result;
    }

    // 循环允许在同一次调用中处理粘在一起的 C0+C1 和 C2。
    // 一旦握手完成、失败或当前阶段数据不足，循环就会停止。
    while (result.consumed < size &&
           state_ != HandshakeState::kComplete &&
           state_ != HandshakeState::kFailed) {
        // 第一阶段需要 1537 字节的 C0+C1；第二阶段需要 1536 字节的 C2。
        const std::size_t target =
            state_ == HandshakeState::kWaitingC0C1 ? kC0C1Size
                                                   : kHandshakeBlockSize;

        // 只复制补齐当前握手阶段所需的字节。输入中的后续 Chunk 数据不会
        // 被提前放入 input_，从而可以通过 result.consumed 留在 muduo Buffer。
        const std::size_t remaining = target - input_.size();
        const std::size_t available = size - result.consumed;
        const std::size_t copied = std::min(remaining, available);
        const auto* begin =
            reinterpret_cast<const std::byte*>(data + result.consumed);
        input_.insert(input_.end(), begin, begin + copied);
        result.consumed += copied;

        // 当前 TCP 数据只是半包。保留 input_，等待下一次 consume() 继续追加。
        if (input_.size() != target) {
            break;
        }

        // 当前阶段数据完整后再解析，解析函数不需要处理越界或半包。
        if (state_ == HandshakeState::kWaitingC0C1) {
            processC0C1(result);
        } else {
            processC2(result);
        }
    }

    result.state = state_;
    result.error = error_;
    return result;
}

void HandshakeSession::processC0C1(ConsumeResult& result) {
    // C0 只有 1 字节，用于声明 RTMP 版本。本实现只支持版本 3。
    if (std::to_integer<std::uint8_t>(input_[0]) != kRtmpVersion) {
        fail(HandshakeError::kUnsupportedVersion, result);
        return;
    }

    // C1 紧跟在 C0 后面，固定为 1536 字节：
    //
    // +----------------+----------------+--------------------------+
    // | time (4 bytes) | time2 (4 bytes)| random (1528 bytes)      |
    // +----------------+----------------+--------------------------+
    //
    // simple handshake 中客户端首次发送的 time2 必须为 0。
    const std::byte* c1 = input_.data() + 1;
    if (!allZero(c1 + 4, c1 + 8)) {
        fail(HandshakeError::kInvalidC1ReservedField, result);
        return;
    }

    // 服务端一次生成完整的 S0+S1+S2，交给网络适配层一次发送。
    // std::array 会零初始化，所以 S1.time2 默认就是协议要求的 0。
    std::array<std::byte, kS0S1S2Size> response{};

    // S0：1 字节 RTMP 版本号。
    response[0] = static_cast<std::byte>(kRtmpVersion);

    // S1 布局与 C1 相同：
    // time = server_time_，time2 = 0，random = server_random_。
    std::byte* s1 = response.data() + 1;
    writeUint32Be(server_time_, s1);
    std::copy(server_random_.begin(), server_random_.end(), s1 + 8);

    // S2 用于回应 C1：
    // time = C1.time，time2 = 服务端收到 C1 时的时间，random = C1.random。
    // 先完整复制 C1，再覆盖 S2.time2，可自然保留 C1.time 和 C1.random。
    std::byte* s2 = s1 + kHandshakeBlockSize;
    std::copy(c1, c1 + kHandshakeBlockSize, s2);
    writeUint32Be(server_time_, s2 + 4);

    result.response.assign(
        reinterpret_cast<const char*>(response.data()), response.size());

    // C0+C1 已处理完毕，清空缓存并为下一阶段 C2 调整预留容量。
    input_.clear();
    input_.reserve(kHandshakeBlockSize);
    state_ = HandshakeState::kWaitingC2;
}

void HandshakeSession::processC2(ConsumeResult& result) {
    // C2 应回显服务端的 S1：
    // time 应等于 S1.time，random 应等于 S1.random。
    //
    // C2.time2 是客户端收到 S1 的时间，不影响服务端确认回显内容，因此
    // 本实现不校验该字段。
    const std::byte* c2 = input_.data();
    if (readUint32Be(c2) != server_time_ ||
        !std::equal(server_random_.begin(), server_random_.end(), c2 + 8)) {
        fail(HandshakeError::kInvalidC2, result);
        return;
    }

    // 校验成功后不再需要保存握手临时数据。
    input_.clear();
    state_ = HandshakeState::kComplete;
}

void HandshakeSession::fail(HandshakeError error, ConsumeResult& result) {
    // 失败是终止状态。清空缓存避免保留无效或恶意输入。
    input_.clear();
    state_ = HandshakeState::kFailed;
    error_ = error;

    // 协议校验失败时不向客户端发送部分或错误的握手响应。
    result.response.clear();
}

}  // namespace rtmp::handshake
