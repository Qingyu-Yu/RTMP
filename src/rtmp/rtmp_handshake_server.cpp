#include "rtmp/server/rtmp_handshake_server.h"

#include "base/Logger.h"
#include "net/Buffer.h"
#include "net/InetAddress.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>

namespace rtmp::server {
namespace {

// 获取单调时钟的秒数并截断为 RTMP 使用的 32 位时间字段。
// 握手时间主要用于双方回显和计算时间差，不要求是 Unix 时间。
std::uint32_t currentTimeSeconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

// 为单条连接生成 S1.random。
// random_device 仅用于播种，随后使用伪随机引擎批量生成，避免为 1528 个
// 字节逐次访问系统随机源。
std::array<std::byte, handshake::kRandomSize> makeRandomBytes() {
    std::array<std::byte, handshake::kRandomSize> bytes{};
    std::random_device seed_source;
    std::seed_seq seed{
        seed_source(), seed_source(), seed_source(), seed_source()};
    std::mt19937 generator(seed);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    for (auto& byte : bytes) {
        byte = static_cast<std::byte>(distribution(generator));
    }
    return bytes;
}

// 将协议错误枚举转换成适合日志输出的稳定文本。
const char* errorText(handshake::HandshakeError error) {
    switch (error) {
        case handshake::HandshakeError::kUnsupportedVersion:
            return "unsupported RTMP version";
        case handshake::HandshakeError::kInvalidC1ReservedField:
            return "invalid C1 reserved field";
        case handshake::HandshakeError::kInvalidC2:
            return "C2 does not echo S1";
        case handshake::HandshakeError::kNone:
            return "none";
    }
    return "unknown";
}

}  // namespace

RtmpHandshakeServer::RtmpHandshakeServer(
    EventLoop* loop,
    const InetAddress& listen_address)
    : server_(loop, listen_address, "RtmpHandshakeServer") {
    // RtmpHandshakeServer 只注册回调，不侵入 muduo 的网络实现。
    // lambda 将 muduo 回调转发给类的私有成员函数，集中处理会话生命周期。
    server_.setConnectionCallback(
        [this](const TcpConnection::Ptr& connection) {
            onConnection(connection);
        });
    server_.setMessageCallback(
        [this](const TcpConnection::Ptr& connection, Buffer* buffer) {
            onMessage(connection, buffer);
        });
    server_.setCloseCallback([this](const TcpConnection::Ptr& connection) {
        onClose(connection);
    });
}

void RtmpHandshakeServer::setThreadNum(int thread_count) {
    // 线程配置直接委托给 muduo。会话表通过互斥锁支持多个 I/O 线程。
    server_.setThreadNum(thread_count);
}

void RtmpHandshakeServer::start() {
    // 实际的 bind、listen、accept 和线程启动由 muduo 完成。
    server_.start();
}

void RtmpHandshakeServer::onConnection(
    const TcpConnection::Ptr& connection) {
    // muduo 的 ConnectionCallback 同时表示建立和断开，通过 connected()
    // 区分。断开时复用 onClose()，使清理逻辑只有一个实现。
    if (!connection->connected()) {
        onClose(connection);
        return;
    }

    // 每条连接拥有独立时间、随机数据和状态机，连接之间不会共享协议状态。
    auto session = std::make_shared<handshake::HandshakeSession>(
        currentTimeSeconds(), makeRandomBytes());
    {
        // 会话表可能由不同 I/O 线程并发访问，因此插入操作必须加锁。
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[connection.get()] = std::move(session);
    }

    LOG_INFO("RTMP handshake connection established: %s",
             connection->peerAddress().toIpPort().c_str());
}

void RtmpHandshakeServer::onMessage(
    const TcpConnection::Ptr& connection,
    Buffer* buffer) {
    SessionPtr session;
    {
        // 只在查询映射时持锁。取得 shared_ptr 后立即释放锁，避免协议解析和
        // socket 发送阻塞其他连接的建立、读取或关闭。
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto found = sessions_.find(connection.get());
        if (found == sessions_.end()) {
            LOG_ERROR("RTMP handshake session not found: %s",
                      connection->peerAddress().toIpPort().c_str());
            connection->shutdown();
            return;
        }
        session = found->second;
    }

    // 将 muduo Buffer 的当前全部可读区域交给状态机。状态机可能只消费其中
    // 的握手部分，并通过 consumed 精确返回可安全移除的长度。
    const auto result =
        session->consume(buffer->peek(), buffer->readableBytes());

    // 不能使用 retrieveAll()：握手与第一个 RTMP Chunk 可能粘在同一个 TCP
    // 包中，握手完成后的 Chunk 字节必须留给下一阶段处理。
    buffer->retrieve(result.consumed);

    // 只有收到完整 C0+C1 时 response 才包含 S0+S1+S2。
    if (!result.response.empty()) {
        connection->send(result.response);
    }

    // 当前阶段仅实现握手。完成后保留连接和 Buffer，后续可在此处把控制权
    // 移交给 Chunk 解析模块。
    if (result.state == handshake::HandshakeState::kComplete) {
        LOG_INFO("RTMP handshake completed: %s",
                 connection->peerAddress().toIpPort().c_str());
        return;
    }

    // 非法握手不再具备恢复意义，记录具体原因后关闭连接写端。
    if (result.state == handshake::HandshakeState::kFailed) {
        LOG_ERROR("RTMP handshake failed for %s: %s",
                  connection->peerAddress().toIpPort().c_str(),
                  errorText(result.error));
        connection->shutdown();
    }
}

void RtmpHandshakeServer::onClose(
    const TcpConnection::Ptr& connection) {
    // erase 对不存在的键也是安全的，因此该函数可以被关闭回调和连接状态
    // 回调重复调用，不需要额外判断。
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(connection.get());
}

}  // namespace rtmp::server
