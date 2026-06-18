#include "rtmp/handshake/handshake_session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using rtmp::handshake::HandshakeError;
using rtmp::handshake::HandshakeSession;
using rtmp::handshake::HandshakeState;

// 使用固定时间值，使测试可以精确检查网络字节序和回显字段。
constexpr std::uint32_t kClientTime = 0x01020304U;
constexpr std::uint32_t kServerTime = 0x11223344U;

// 测试辅助函数：将整数按 RTMP 使用的大端字节序写入字符串。
void writeUint32Be(std::uint32_t value, char* output) {
    output[0] = static_cast<char>((value >> 24U) & 0xFFU);
    output[1] = static_cast<char>((value >> 16U) & 0xFFU);
    output[2] = static_cast<char>((value >> 8U) & 0xFFU);
    output[3] = static_cast<char>(value & 0xFFU);
}

// 测试辅助函数：从响应中读取大端 32 位整数。
std::uint32_t readUint32Be(const char* input) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(input);
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

// 生成确定性的服务端随机数据，便于验证 S1 以及构造正确的 C2。
std::array<std::byte, rtmp::handshake::kRandomSize> makeServerRandom() {
    std::array<std::byte, rtmp::handshake::kRandomSize> random{};
    for (std::size_t i = 0; i < random.size(); ++i) {
        random[i] = static_cast<std::byte>((i * 7U) % 251U);
    }
    return random;
}

// 构造符合 simple handshake 格式的完整 C0+C1 测试数据。
std::string makeC0C1() {
    std::string packet(rtmp::handshake::kC0C1Size, '\0');
    packet[0] = static_cast<char>(rtmp::handshake::kRtmpVersion);
    writeUint32Be(kClientTime, packet.data() + 1);
    for (std::size_t i = 0; i < rtmp::handshake::kRandomSize; ++i) {
        packet[9 + i] = static_cast<char>(i % 251U);
    }
    return packet;
}

// 根据服务端时间和随机数据构造用于回显 S1 的 C2。
std::string makeC2(
    const std::array<std::byte, rtmp::handshake::kRandomSize>& random) {
    std::string packet(rtmp::handshake::kHandshakeBlockSize, '\0');
    writeUint32Be(kServerTime, packet.data());
    writeUint32Be(kClientTime, packet.data() + 4);
    std::copy(random.begin(), random.end(),
              reinterpret_cast<std::byte*>(packet.data() + 8));
    return packet;
}

// 最小测试断言工具：失败时输出原因并把结果返回给测试函数。
bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// 验证 C0+C1 分两次到达时，状态机能够缓存半包并在补齐后生成响应。
bool testFragmentedHandshake() {
    const auto random = makeServerRandom();
    HandshakeSession session(kServerTime, random);
    const std::string c0c1 = makeC0C1();

    auto first = session.consume(c0c1.data(), 300);
    if (!expect(first.consumed == 300 && first.response.empty() &&
                    first.state == HandshakeState::kWaitingC0C1,
                "partial C0+C1 should be buffered")) {
        return false;
    }

    auto second =
        session.consume(c0c1.data() + 300, c0c1.size() - 300);
    if (!expect(second.consumed == c0c1.size() - 300 &&
                    second.response.size() ==
                        rtmp::handshake::kS0S1S2Size &&
                    second.state == HandshakeState::kWaitingC2,
                "complete C0+C1 should produce S0+S1+S2")) {
        return false;
    }

    const char* s1 = second.response.data() + 1;
    const char* s2 = s1 + rtmp::handshake::kHandshakeBlockSize;
    if (!expect(second.response[0] ==
                    static_cast<char>(rtmp::handshake::kRtmpVersion),
                "S0 should contain version 3") ||
        !expect(readUint32Be(s1) == kServerTime,
                "S1 should contain server time") ||
        !expect(readUint32Be(s2) == kClientTime,
                "S2 should echo C1 time") ||
        !expect(readUint32Be(s2 + 4) == kServerTime,
                "S2 time2 should contain server time")) {
        return false;
    }

    const std::string c2 = makeC2(random);
    auto third = session.consume(c2.data(), c2.size());
    return expect(third.consumed == c2.size() &&
                      third.state == HandshakeState::kComplete,
                  "valid C2 should complete the handshake");
}

// 验证 C0+C1、C2 和后续 Chunk 粘包时，只消费握手部分。
bool testStickyPacketLeavesChunkBytes() {
    const auto random = makeServerRandom();
    HandshakeSession session(kServerTime, random);
    std::string input = makeC0C1() + makeC2(random) + "chunk";

    const auto result = session.consume(input.data(), input.size());
    return expect(result.state == HandshakeState::kComplete,
                  "C0+C1+C2 in one packet should complete") &&
           expect(result.consumed ==
                      rtmp::handshake::kC0C1Size +
                          rtmp::handshake::kHandshakeBlockSize,
                  "post-handshake chunk bytes must remain unconsumed") &&
           expect(result.response.size() ==
                      rtmp::handshake::kS0S1S2Size,
                  "sticky packet should still generate one response");
}

// 验证服务器拒绝非 RTMP 3 的 C0。
bool testInvalidVersion() {
    const auto random = makeServerRandom();
    HandshakeSession session(kServerTime, random);
    std::string c0c1 = makeC0C1();
    c0c1[0] = 2;

    const auto result = session.consume(c0c1.data(), c0c1.size());
    return expect(result.state == HandshakeState::kFailed &&
                      result.error == HandshakeError::kUnsupportedVersion,
                  "unsupported C0 version should fail");
}

// 验证 simple handshake 要求 C1 的 time2/zero 字段全部为 0。
bool testInvalidC1ReservedField() {
    const auto random = makeServerRandom();
    HandshakeSession session(kServerTime, random);
    std::string c0c1 = makeC0C1();
    c0c1[5] = 1;

    const auto result = session.consume(c0c1.data(), c0c1.size());
    return expect(
        result.state == HandshakeState::kFailed &&
            result.error == HandshakeError::kInvalidC1ReservedField,
        "simple handshake requires a zero C1 reserved field");
}

// 验证 C2 未正确回显 S1.random 时握手失败。
bool testInvalidC2() {
    const auto random = makeServerRandom();
    HandshakeSession session(kServerTime, random);
    const std::string c0c1 = makeC0C1();
    const auto server_hello = session.consume(c0c1.data(), c0c1.size());
    if (!expect(server_hello.state == HandshakeState::kWaitingC2,
                "valid C0+C1 should advance to C2")) {
        return false;
    }

    std::string c2 = makeC2(random);
    c2[8] ^= 1;
    const auto result = session.consume(c2.data(), c2.size());
    return expect(result.state == HandshakeState::kFailed &&
                      result.error == HandshakeError::kInvalidC2,
                  "C2 that does not echo S1 should fail");
}

}  // namespace

int main() {
    // 任一场景失败都返回非 0，便于 CTest 判断测试结果。
    if (!testFragmentedHandshake() ||
        !testStickyPacketLeavesChunkBytes() ||
        !testInvalidVersion() ||
        !testInvalidC1ReservedField() ||
        !testInvalidC2()) {
        return 1;
    }

    std::cout << "All handshake session tests passed\n";
    return 0;
}
