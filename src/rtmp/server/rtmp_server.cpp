#include "rtmp/server/rtmp_server.h"

#include "base/Logger.h"
#include "net/Buffer.h"
#include "net/InetAddress.h"
#include "net/TcpConnection.h"
#include "net/TcpServer.h"
#include "rtmp/handshake/handshake_session.h"
#include "rtmp/session/rtmp_session.h"
#include "rtmp/stream/stream_registry.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

namespace rtmp::server {
namespace {

// 握手时间只用于协议回显和时间差计算，使用单调时钟可避免系统时间回拨。
std::uint32_t currentTimeSeconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::array<std::byte, handshake::kRandomSize> makeRandomBytes() {
    // 每条连接生成独立 S1.random，C2 校验因此不能被其他连接的数据复用。
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

stream::ClientId clientId(const TcpConnection::Ptr& connection) {
    // ClientId 仅在连接存活期间作为进程内键使用，不会写入协议或持久化。
    return reinterpret_cast<stream::ClientId>(connection.get());
}

}  // namespace

class RtmpServer::Impl {
public:
    Impl(EventLoop* loop, const InetAddress& listen_address)
        : server_(loop, listen_address, "RtmpServer") {
        // 网络层只负责事件通知；所有 RTMP 状态保存在 ConnectionContext。
        server_.setConnectionCallback(
            [this](const TcpConnection::Ptr& connection) {
                onConnection(connection);
            });
        server_.setMessageCallback(
            [this](const TcpConnection::Ptr& connection, Buffer* buffer) {
                onMessage(connection, buffer);
            });
        server_.setCloseCallback(
            [this](const TcpConnection::Ptr& connection) {
                onClose(connection);
            });
    }

    void setThreadNum(int thread_count) {
        server_.setThreadNum(thread_count);
    }

    void start() {
        server_.start();
    }

private:
    /**
     * 单条 TCP 连接对应的完整 RTMP 状态。
     *
     * 握手和握手后协议分开保存，确保 ChunkDecoder 永远不会看到握手字节。
     * connection 使用 weak_ptr，避免上下文与 TcpConnection 互相强引用。
     */
    struct ConnectionContext {
        ConnectionContext(
            std::uint32_t server_time,
            const std::array<std::byte, handshake::kRandomSize>& random)
            : handshake(server_time, random) {}

        handshake::HandshakeSession handshake;
        session::RtmpSession protocol;
        std::weak_ptr<TcpConnection> connection;
    };

    using ContextPtr = std::shared_ptr<ConnectionContext>;

    void onConnection(const TcpConnection::Ptr& connection) {
        // TcpServer 的 ConnectionCallback 同时通知建立和断开。
        if (!connection->connected()) {
            onClose(connection);
            return;
        }

        // 连接之间不共享握手随机数、Chunk 压缩头或 AMF 命令状态。
        auto context = std::make_shared<ConnectionContext>(
            currentTimeSeconds(), makeRandomBytes());
        context->connection = connection;
        {
            // 回调可能运行在不同 I/O worker；映射修改必须串行化。
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            contexts_[clientId(connection)] = std::move(context);
        }

        LOG_INFO("RTMP connection established: %s",
                 connection->peerAddress().toIpPort().c_str());
    }

    void onMessage(const TcpConnection::Ptr& connection, Buffer* buffer) {
        const auto id = clientId(connection);
        auto context = findContext(id);
        if (!context) {
            LOG_ERROR("RTMP connection context not found: %s",
                      connection->peerAddress().toIpPort().c_str());
            connection->shutdown();
            return;
        }

        if (context->handshake.state() !=
            handshake::HandshakeState::kComplete) {
            // HandshakeSession 精确返回 consumed，使同一个 TCP 包中紧随 C2
            // 的第一个 RTMP Chunk 能继续留在 Buffer 中交给协议会话。
            const auto result = context->handshake.consume(
                buffer->peek(), buffer->readableBytes());
            buffer->retrieve(result.consumed);
            if (!result.response.empty()) {
                connection->send(result.response);
            }
            if (result.state == handshake::HandshakeState::kFailed) {
                LOG_ERROR("RTMP handshake failed for %s: %s",
                          connection->peerAddress().toIpPort().c_str(),
                          errorText(result.error));
                connection->shutdown();
                return;
            }
            if (result.state != handshake::HandshakeState::kComplete) {
                return;
            }
            LOG_INFO("RTMP handshake completed: %s",
                     connection->peerAddress().toIpPort().c_str());
        }

        if (buffer->readableBytes() == 0) {
            return;
        }

        const auto result = context->protocol.consume(
            buffer->peek(), buffer->readableBytes());
        // RtmpSession/ChunkDecoder 已内部缓存不完整 Chunk，因此本次交付的
        // 全部字节都可从 muduo Buffer 移除。
        buffer->retrieveAll();
        if (!result.ok) {
            LOG_ERROR("RTMP protocol error for %s: %s",
                      connection->peerAddress().toIpPort().c_str(),
                      result.error.c_str());
            connection->shutdown();
            return;
        }

        // 先回复当前连接的控制/命令消息，再处理可能影响其他连接的流事件。
        sendMessages(connection, context->protocol, result.outbound);
        processEvents(id, context, result.events);
    }

    void onClose(const TcpConnection::Ptr& connection) {
        const auto id = clientId(connection);
        // 先清除发布/订阅关系，避免后续路由继续返回已关闭连接。
        streams_.remove(id);
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        contexts_.erase(id);
    }

    void processEvents(
        stream::ClientId client,
        const ContextPtr& context,
        const std::vector<session::Event>& events) {
        auto connection = context->connection.lock();
        if (!connection) {
            return;
        }

        for (const auto& event : events) {
            // 这里是协议会话与全局流状态之间唯一的编排边界。
            switch (event.type) {
                case session::EventType::kPublish:
                    // StreamRegistry 决定名称是否冲突，会话只负责生成对应状态。
                    sendMessages(
                        connection, context->protocol,
                        {context->protocol.publishStatus(
                            streams_.publish(client, event.stream_key))});
                    break;
                case session::EventType::kPlay:
                    processPlay(client, context, event.stream_key);
                    break;
                case session::EventType::kMedia:
                    routeMedia(client, event);
                    break;
                case session::EventType::kCloseStream:
                    streams_.remove(client);
                    break;
            }
        }
    }

    void processPlay(
        stream::ClientId client,
        const ContextPtr& context,
        const std::string& stream_key) {
        auto connection = context->connection.lock();
        if (!connection) {
            return;
        }

        // 订阅注册和状态回复使用同一个 found 结果，防止判断与注册分离竞态。
        const bool found = streams_.subscribe(client, stream_key);
        sendMessages(connection, context->protocol,
                     context->protocol.playStatus(found));
        if (!found) {
            return;
        }

        // 新播放器必须先收到 metadata 和编解码 sequence header，才能解码
        // 后续实时音视频帧。
        auto bootstrap = streams_.bootstrap(stream_key);
        for (auto& message : bootstrap) {
            message.stream_id = 1;
        }
        sendMessages(connection, context->protocol, bootstrap);
    }

    void routeMedia(
        stream::ClientId publisher,
        const session::Event& event) {
        // route() 在锁内验证发布者并返回订阅者快照，网络发送发生在锁外。
        const auto subscribers =
            streams_.route(publisher, event.stream_key, event.message);
        for (const auto subscriber : subscribers) {
            auto target = findContext(subscriber);
            if (!target) {
                continue;
            }
            auto connection = target->connection.lock();
            if (!connection) {
                continue;
            }

            // 发布端和播放端的 Message Stream ID 属于各自连接；当前单流模型
            // 为播放端统一使用 createStream 返回的 ID 1。
            auto forwarded = event.message;
            forwarded.stream_id = 1;
            sendMessages(connection, target->protocol, {forwarded});
        }
    }

    static void sendMessages(
        const TcpConnection::Ptr& connection,
        const session::RtmpSession& protocol_session,
        const std::vector<protocol::Message>& messages) {
        for (const auto& message : messages) {
            // 每个目标连接使用自己的 RtmpSession 编码，以遵守其出站配置。
            const auto encoded = protocol_session.encode(message);
            if (!encoded.empty()) {
                connection->send(encoded);
            }
        }
    }

    ContextPtr findContext(stream::ClientId client) {
        // 返回 shared_ptr 后立即释放锁，后续解析或发送不会阻塞连接表操作。
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        const auto found = contexts_.find(client);
        return found == contexts_.end() ? nullptr : found->second;
    }

    // 实际监听、accept 和 I/O 线程分发由底层 TcpServer 完成。
    TcpServer server_;

    // 仅保护 contexts_；StreamRegistry 自身负责内部同步。
    std::mutex contexts_mutex_;

    // ClientId -> 每连接协议状态。
    std::unordered_map<stream::ClientId, ContextPtr> contexts_;

    // 全服务器共享的发布/订阅关系。
    stream::StreamRegistry streams_;
};

RtmpServer::RtmpServer(
    EventLoop* loop,
    const InetAddress& listen_address)
    // PImpl 使公共头文件无需包含 muduo 和 RTMP 内部实现头文件。
    : impl_(std::make_unique<Impl>(loop, listen_address)) {}

RtmpServer::~RtmpServer() = default;

void RtmpServer::setThreadNum(int thread_count) {
    impl_->setThreadNum(thread_count);
}

void RtmpServer::start() {
    impl_->start();
}

}  // namespace rtmp::server
