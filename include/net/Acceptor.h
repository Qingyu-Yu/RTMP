#pragma once

#include <functional>
#include <memory>

#include "base/noncopyable.h"
#include "net/Channel.h"
#include "net/InetAddress.h"

class EventLoop;

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

    EventLoop* loop_;
    std::unique_ptr<Channel> acceptChannel_;
    const InetAddress listenAddr_;
    int acceptSocket_;
    bool listening_;
    NewConnectionCallback newConnectionCallback_;
};
