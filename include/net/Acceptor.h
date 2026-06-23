#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "base/noncopyable.h"
#include "net/InetAddress.h"
#include "net/Socket.h"

class Channel;
class EventLoop;

// Acceptor 只管理监听 socket。
//
// 监听 fd 可读表示有新连接到达。Acceptor 调用 accept() 得到 connectionFd 后，
// 通过 NewConnectionCallback 交给 TcpServer；后续连接 IO 不再由它负责。
class Acceptor : noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int, const InetAddress&)>;

    Acceptor(
        EventLoop* loop,
        const InetAddress& listenAddress,
        bool reusePort = true);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }
    bool listening() const { return listening_; }
    void listen();

private:
    void handleRead();

    EventLoop* loop_;                        // 监听 socket 所属的主 EventLoop。
    Socket acceptSocket_;                    // RAII 管理监听 fd。
    std::unique_ptr<Channel> acceptChannel_; // 监听新连接事件。
    const InetAddress listenAddress_;        // bind() 使用的本地地址。
    bool listening_;                         // 是否已经 bind/listen。
    NewConnectionCallback newConnectionCallback_;
};
