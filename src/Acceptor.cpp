#include "net/Acceptor.h"
#include "net/EventLoop.h"
#include "base/Logger.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>

namespace
{
int createNonblockingSocket()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        LOG_FATAL("socket create failed");
    }
    int flags = ::fcntl(sockfd, F_GETFL, 0);
    ::fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    return sockfd;
}

void setReuseAddr(int sockfd)
{
    int opt = 1;
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void setReusePort(int sockfd)
{
    int opt = 1;
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reusePort)
    : loop_(loop),
      acceptChannel_(nullptr),
      listenAddr_(listenAddr),
      acceptSocket_(-1),
      listening_(false)
{
    acceptSocket_ = createNonblockingSocket();
    setReuseAddr(acceptSocket_);
    if (reusePort)
    {
        setReusePort(acceptSocket_);
    }
    acceptChannel_.reset(new Channel(loop_, acceptSocket_));
    acceptChannel_->setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    if (acceptSocket_ >= 0)
    {
        ::close(acceptSocket_);
    }
}

void Acceptor::listen()
{
    if (!listening_)
    {
        if (::bind(acceptSocket_, reinterpret_cast<const struct sockaddr*>(listenAddr_.getSockAddr()), sizeof(struct sockaddr_in)) < 0)
        {
            LOG_FATAL("bind failed: %s", strerror(errno));
        }
        if (::listen(acceptSocket_, SOMAXCONN) < 0)
        {
            LOG_FATAL("listen failed: %s", strerror(errno));
        }

        acceptChannel_->enableReading();
        listening_ = true;

        LOG_INFO("Acceptor listening on %s", listenAddr_.toIpPort().c_str());
    }
}

void Acceptor::handleRead()
{
    struct sockaddr_in peerAddr;
    socklen_t addrLen = static_cast<socklen_t>(sizeof(peerAddr));
    int connfd = ::accept(acceptSocket_, reinterpret_cast<struct sockaddr*>(&peerAddr), &addrLen);
    if (connfd >= 0)
    {
        // 将新连接设置为非阻塞模式。
        ::fcntl(connfd, F_SETFL, ::fcntl(connfd, F_GETFL, 0) | O_NONBLOCK);
        InetAddress peerAddress(peerAddr);
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddress);
        }
    }
    else
    {
        // 非阻塞 accept 在没有新连接时会返回 EAGAIN/EWOULDBLOCK，该情况不认为是错误。
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOG_ERROR("accept failed: %s", strerror(errno));
        }
    }
}
