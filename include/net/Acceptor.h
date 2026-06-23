#pragma once

#include <functional>
#include <memory>

#include "base/noncopyable.h"
#include "net/Channel.h"
#include "net/InetAddress.h"

class EventLoop;

// Acceptor 只管理监听 socket。
//
// 监听 fd 可读表示有新连接到达。Acceptor 调用 accept() 得到 connfd 后，通过
// NewConnectionCallback 把 connfd 交给 TcpServer；后续连接 IO 不再由它负责。
class Acceptor : noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reusePort = true);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }
    bool listening() const { return listening_; }
    void listen();

private:
    void handleRead();

    EventLoop* loop_;                       // 监听 socket 所属的主 EventLoop。
    std::unique_ptr<Channel> acceptChannel_;// 监听 acceptSocket_ 的可读事件。
    const InetAddress listenAddr_;          // bind() 使用的本地地址。
    int acceptSocket_;                      // 只负责 accept 的监听 fd。
    bool listening_;                        // 是否已经 bind/listen。
    NewConnectionCallback newConnectionCallback_;
};
