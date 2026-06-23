#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "base/Logger.h"

#include <cstdio>

TcpServer::TcpServer(
    EventLoop* loop,
    const InetAddress& listenAddress,
    const std::string& name)
    : loop_(loop),
      name_(name),
      acceptor_(std::make_unique<Acceptor>(loop, listenAddress, true)),
      threadPool_(std::make_unique<EventLoopThreadPool>(loop, name)),
      listenAddress_(listenAddress),
      started_(false),
      nextConnId_(1)
{
    acceptor_->setNewConnectionCallback(
        [this](int connectionFd, const InetAddress& peerAddress) {
            handleNewConnection(connectionFd, peerAddress);
        });
}

TcpServer::~TcpServer()
{
}

void TcpServer::start()
{
    loop_->assertInLoopThread();
    if (started_)
    {
        return;
    }
    started_ = true;

    threadPool_->start();
    acceptor_->listen();

    LOG_INFO(
        "TcpServer started on %s",
        listenAddress_.toIpPort().c_str());
}

void TcpServer::handleNewConnection(
    int connectionFd,
    const InetAddress& peerAddress)
{
    loop_->assertInLoopThread();
    // 本函数运行在 baseLoop（Acceptor 所在线程）。
    char buf[32];
    snprintf(buf, sizeof(buf), "-%d", nextConnId_++);
    const std::string connectionName = name_ + buf;

    // 选择一个 I/O 线程的 EventLoop 来管理该连接。
    EventLoop* ioLoop = threadPool_->getNextLoop();
    auto conn = std::make_shared<TcpConnection>(
        ioLoop,
        connectionName,
        connectionFd,
        peerAddress);
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
    connections_[connectionName] = conn;
    // 在选中的 ioLoop 线程中完成连接建立操作。
    ioLoop->runInLoop([conn] {
        conn->connectEstablished();
    });
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
    // close 事件发生在连接所属的 ioLoop，但 connections_ 属于 baseLoop，
    // 因此不能直接 erase，必须投递回 baseLoop。
    loop_->queueInLoop([this, conn] {
        removeConnectionInLoop(conn);
    });
}

void TcpServer::removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    loop_->assertInLoopThread();

    // name_ 与连接表 key 相同，可以 O(1) 删除，无需遍历查找 shared_ptr。
    connections_.erase(conn->name());

    // connections_ 属于 baseLoop，Channel 属于连接自己的 ioLoop。
    // 删除连接表后，必须回到 ioLoop 才能从 Epoll 移除 Channel。
    conn->getLoop()->queueInLoop([conn] {
        conn->connectDestroyed();
    });
}
