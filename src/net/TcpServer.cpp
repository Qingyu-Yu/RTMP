#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "base/Logger.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <errno.h>

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& nameArg)
    : loop_(loop),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, true)),
      threadPool_(new EventLoopThreadPool(loop, nameArg)),
      listenAddr_(listenAddr),
      started_(false),
      nextConnId_(1)
{
    // Acceptor 只产出 connfd；TcpServer 接管后续对象创建、线程分配和生命周期。
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
}

void TcpServer::start()
{
    if (started_)
    {
        return;
    }
    started_ = true;

    threadPool_->start();
    acceptor_->listen();

    LOG_INFO("TcpServer started on %s", listenAddr_.toIpPort().c_str());
}

void TcpServer::newConnection(int connfd, const InetAddress& peerAddr)
{
    // 本函数运行在 baseLoop（Acceptor 所在线程）。
    char buf[32];
    snprintf(buf, sizeof(buf), "-%d", nextConnId_++);
    std::string connName = name_ + buf;

    // 选择一个 I/O 线程的 EventLoop 来管理该连接。
    EventLoop* ioLoop = threadPool_->getNextLoop();
    InetAddress localAddr;
    socklen_t addrLen = static_cast<socklen_t>(sizeof(struct sockaddr_in));
    if (::getsockname(connfd, reinterpret_cast<struct sockaddr*>(localAddr.getSockAddr()), &addrLen) < 0)
    {
        LOG_ERROR("getsockname failed: %s", strerror(errno));
    }

    auto conn = std::make_shared<TcpConnection>(ioLoop, connfd, localAddr, peerAddr);
    // 绑定 TcpConnection 对象到 Channel，避免对象销毁后事件回调悬空。
    conn->channel()->tie(conn);
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    // 先通知上层连接已经关闭，使 RTMP 层可以删除该连接对应的握手会话；
    // 再执行 TcpServer 自身的连接表清理和 TcpConnection 销毁流程。
    conn->setCloseCallback([this](const std::shared_ptr<TcpConnection>& connection) {
        if (closeCallback_)
        {
            closeCallback_(connection);
        }
        removeConnection(connection);
    });

    // 连接表由 baseLoop 独占访问。shared_ptr 是连接的主要所有者；
    // 删除该条目后，异步任务和回调中的临时 shared_ptr 释放完毕即可析构连接。
    connections_[connName] = conn;
    // 在选中的 ioLoop 线程中完成连接建立操作。
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
    // close 事件发生在连接所属的 ioLoop，但 connections_ 属于 baseLoop，
    // 因此不能直接 erase，必须投递回 baseLoop。
    loop_->queueInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    // 当前实现通过指针查找连接名。删除连接表持有的 shared_ptr 后，再清理 Channel。
    std::string connName;
    for (const auto& kv : connections_)
    {
        if (kv.second == conn)
        {
            connName = kv.first;
            break;
        }
    }
    if (!connName.empty())
    {
        connections_.erase(connName);
    }
    conn->connectDestroyed();
}
