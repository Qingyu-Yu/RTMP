#include "net/Acceptor.h"
#include "base/Logger.h"
#include "net/Channel.h"
#include "net/EventLoop.h"

#include <cerrno>
#include <cstring>

Acceptor::Acceptor(
    EventLoop* loop,
    const InetAddress& listenAddress,
    bool reusePort)
    : loop_(loop),
      acceptSocket_(Socket::createNonblocking()),
      acceptChannel_(std::make_unique<Channel>(loop, acceptSocket_.fd())),
      listenAddress_(listenAddress),
      listening_(false)
{
    acceptSocket_.setReuseAddress(true);
    acceptSocket_.setReusePort(reusePort);
    acceptChannel_->setReadCallback([this] {
        handleRead();
    });
}

Acceptor::~Acceptor()
{
    loop_->assertInLoopThread();
    if (acceptChannel_->state() == Channel::State::kAdded)
    {
        acceptChannel_->disableAll();
    }
    if (acceptChannel_->state() != Channel::State::kNew)
    {
        acceptChannel_->remove();
    }
}

void Acceptor::listen()
{
    loop_->assertInLoopThread();
    if (!listening_)
    {
        acceptSocket_.bindAddress(listenAddress_);
        acceptSocket_.listen();

        // 监听 socket 的“可读”不是普通数据到达，而是 accept 队列中有新连接。
        // enableReading() 最终会使 Epoll 执行 EPOLL_CTL_ADD。
        acceptChannel_->enableReading();
        listening_ = true;

        LOG_INFO(
            "Acceptor listening on %s",
            listenAddress_.toIpPort().c_str());
    }
}

void Acceptor::handleRead()
{
    loop_->assertInLoopThread();
    InetAddress peerAddress;
    const int connectionFd = acceptSocket_.accept(&peerAddress);
    if (connectionFd >= 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connectionFd, peerAddress);
        }
        else
        {
            // 没有接管者时必须关闭已接受 fd，避免资源泄漏。
            Socket unclaimedConnection(connectionFd);
        }
    }
    else
    {
        // 非阻塞 accept 在没有新连接时会返回 EAGAIN/EWOULDBLOCK，该情况不认为是错误。
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOG_ERROR("accept failed: %s", std::strerror(errno));
        }
    }
}
