#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "base/noncopyable.h"
#include "net/Buffer.h"
#include "net/Channel.h"
#include "net/InetAddress.h"
#include "net/Socket.h"

class EventLoop;

// TcpConnection 表示一条已建立的 TCP 连接。
//
// 线程约束：
//   - socket、Channel、输入/输出缓冲区只在所属 loop_ 线程中访问；
//   - send()/shutdown() 可以跨线程调用，它们会把操作投递回 loop_；
//   - 对象由 shared_ptr 管理，异步任务捕获 shared_ptr 以保证执行前对象不被销毁。
class TcpConnection : public noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const Ptr&)>;
    using MessageCallback = std::function<void(const Ptr&, Buffer*)>;
    using CloseCallback = std::function<void(const Ptr&)>;
    using WriteCompleteCallback = Channel::EventCallback;

    // 构造函数，关联 EventLoop、连接名称、socket 描述符和地址信息。
    // 每个 TcpConnection 代表一个连接，包含 socket、Channel、输入/输出缓冲区和回调。
    TcpConnection(EventLoop* loop,
                  std::string name,
                  int socketFd,
                  const InetAddress& peerAddress);

    ~TcpConnection() = default;

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddress_; }
    const InetAddress& peerAddress() const { return peerAddress_; }
    bool connected() const;

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
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    Channel* channel() const { return channel_.get(); }
    Buffer* inputBuffer() { return &inputBuffer_; }
    Buffer* outputBuffer() { return &outputBuffer_; }

private:
    enum class State
    {
        kConnecting,
        kConnected,
        kDisconnecting,
        kDisconnected,
    };

    void sendInLoop(const std::string& message);
    void shutdownInLoop();

    EventLoop* loop_;                    // 连接所属的唯一事件循环。
    const std::string name_;             // TcpServer 分配的唯一连接名称。
    Socket socket_;                      // RAII 管理连接 fd。
    std::unique_ptr<Channel> channel_;   // 把连接 fd 接入 Epoll。
    InetAddress localAddress_;           // 本地地址。
    InetAddress peerAddress_;            // 对端地址。
    std::atomic<State> state_;           // 显式连接生命周期，允许跨线程读取。

    Buffer inputBuffer_;                 // 接收数据缓冲区。
    Buffer outputBuffer_;                // 发送数据缓冲区。

    ConnectionCallback connectionCallback_;   // 连接状态变化回调。
    MessageCallback messageCallback_;         // 收到消息回调。
    CloseCallback closeCallback_;             // 关闭连接回调。
    WriteCompleteCallback writeCompleteCallback_; // 发送完成回调。
};
