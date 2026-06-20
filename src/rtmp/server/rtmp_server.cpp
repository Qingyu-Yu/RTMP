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

std::uint32_t currentTimeSeconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

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
    return reinterpret_cast<stream::ClientId>(connection.get());
}

}  // namespace

class RtmpServer::Impl {
public:
    Impl(EventLoop* loop, const InetAddress& listen_address)
        : server_(loop, listen_address, "RtmpServer") {
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
        if (!connection->connected()) {
            onClose(connection);
            return;
        }

        auto context = std::make_shared<ConnectionContext>(
            currentTimeSeconds(), makeRandomBytes());
        context->connection = connection;
        {
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
        buffer->retrieveAll();
        if (!result.ok) {
            LOG_ERROR("RTMP protocol error for %s: %s",
                      connection->peerAddress().toIpPort().c_str(),
                      result.error.c_str());
            connection->shutdown();
            return;
        }

        sendMessages(connection, context->protocol, result.outbound);
        processEvents(id, context, result.events);
    }

    void onClose(const TcpConnection::Ptr& connection) {
        const auto id = clientId(connection);
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
            switch (event.type) {
                case session::EventType::kPublish:
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

        const bool found = streams_.subscribe(client, stream_key);
        sendMessages(connection, context->protocol,
                     context->protocol.playStatus(found));
        if (!found) {
            return;
        }

        auto bootstrap = streams_.bootstrap(stream_key);
        for (auto& message : bootstrap) {
            message.stream_id = 1;
        }
        sendMessages(connection, context->protocol, bootstrap);
    }

    void routeMedia(
        stream::ClientId publisher,
        const session::Event& event) {
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
            const auto encoded = protocol_session.encode(message);
            if (!encoded.empty()) {
                connection->send(encoded);
            }
        }
    }

    ContextPtr findContext(stream::ClientId client) {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        const auto found = contexts_.find(client);
        return found == contexts_.end() ? nullptr : found->second;
    }

    TcpServer server_;
    std::mutex contexts_mutex_;
    std::unordered_map<stream::ClientId, ContextPtr> contexts_;
    stream::StreamRegistry streams_;
};

RtmpServer::RtmpServer(
    EventLoop* loop,
    const InetAddress& listen_address)
    : impl_(std::make_unique<Impl>(loop, listen_address)) {}

RtmpServer::~RtmpServer() = default;

void RtmpServer::setThreadNum(int thread_count) {
    impl_->setThreadNum(thread_count);
}

void RtmpServer::start() {
    impl_->start();
}

}  // namespace rtmp::server
