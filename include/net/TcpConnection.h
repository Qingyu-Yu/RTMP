#pragma once

#include <memory>
#include <functional>
#include <string>

#include "base/noncopyable.h"
#include "net/Buffer.h"
#include "net/Channel.h"
#include "net/InetAddress.h"
#include "base/Timestamp.h"

class EventLoop;

class TcpConnection : public noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const Ptr&)>;
    using MessageCallback = std::function<void(const Ptr&, Buffer*)>;
    using CloseCallback = std::function<void(const Ptr&)>;
    using WriteCompleteCallback = Channel::EventCallback;

    // 构造函数，关联 EventLoop、socket 描述符和地址信息。
    // 每个 TcpConnection 代表一个连接，包含 socket、Channel、输入/输出缓冲区和回调。
    TcpConnection(EventLoop* loop, int sockfd,
                  const InetAddress& localAddr,
                  const InetAddress& peerAddr);

    // 析构函数，负责关闭 socket。
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }
    bool connected() const { return connected_; }

    // 设置各种回调。
    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }

    // 发送消息到对端。
    // 发送会先尝试直接写 socket，若无法全部写入则缓存到 outputBuffer_ 并监听可写事件。
    void send(const std::string& message);

    // 关闭连接的写端。
    // 该操作会在 loop 线程中执行，避免跨线程直接 shutdown。
    void shutdown();

    // 连接建立与销毁时调用。
    // connectEstablished 在 connection 初始化完成后由 TcpServer 调用，开始监听读事件并触发 connect 回调。
    void connectEstablished();
    // connectDestroyed 在 connection 关闭时调用，停止监听事件并触发关闭回调。
    void connectDestroyed();

    // 事件回调接口，由 Channel 触发。
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    Channel* channel() const { return channel_.get(); }
    Buffer* inputBuffer() { return &inputBuffer_; }
    Buffer* outputBuffer() { return &outputBuffer_; }

private:
    void sendInLoop(const std::string& message);
    void shutdownInLoop();

    EventLoop* loop_;                     // 事件循环所属线程。
    std::unique_ptr<Channel> channel_;    // 用于 socket IO 的 Channel 对象。
    const std::string name_;             // 连接名称。
    int sockfd_;                         // 连接 socket 描述符。
    InetAddress localAddr_;              // 本地地址。
    InetAddress peerAddr_;               // 对端地址。
    bool connected_;                     // 是否已经连接成功。

    Buffer inputBuffer_;                 // 接收数据缓冲区。
    Buffer outputBuffer_;                // 发送数据缓冲区。

    ConnectionCallback connectionCallback_;   // 连接状态变化回调。
    MessageCallback messageCallback_;         // 收到消息回调。
    CloseCallback closeCallback_;             // 关闭连接回调。
    WriteCompleteCallback writeCompleteCallback_; // 发送完成回调。
};
