#pragma once

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>

#include "base/noncopyable.h"
#include "base/Timestamp.h"
#include "net/InetAddress.h"
#include "net/TcpConnection.h"
#include "net/Acceptor.h"
#include "net/EventLoopThreadPool.h"

class EventLoop;

// TcpServer 负责监听、分配连接和管理连接生命周期。
//
// 典型线程模型：
//   baseLoop（主线程）：Acceptor + connections_ 连接表
//   ioLoop（工作线程）：每条 TcpConnection 的 socket IO
//
// 新连接由 Acceptor 在 baseLoop 接收，再通过线程池轮询分配给某个 ioLoop。
class TcpServer : public noncopyable
{
public:
    using ConnectionCallback = TcpConnection::ConnectionCallback;
    using MessageCallback = TcpConnection::MessageCallback;
    using CloseCallback = TcpConnection::CloseCallback;

    // 构造函数，传入事件循环和监听地址。
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& nameArg = "TcpServer");
    ~TcpServer();

    // 设置连接、消息、关闭事件回调。
    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }
    void setThreadNum(int numThreads) { threadPool_->setThreadNum(numThreads); }

    // 启动服务器，绑定监听端口并开始监听。
    // 这个方法会先启动 EventLoop 线程池，再让 Acceptor 开始接受连接。
    void start();

private:
    void newConnection(int connfd, const InetAddress& peerAddr);
    void removeConnection(const std::shared_ptr<TcpConnection>& conn);
    void removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);

    EventLoop* loop_; // baseLoop：接受连接并维护 connections_。
    const std::string name_; // 服务器名称。
    std::unique_ptr<Acceptor> acceptor_; // 接受新连接的 Acceptor。
    std::unique_ptr<EventLoopThreadPool> threadPool_; // EventLoop 线程池，用于分发具体连接到 worker loop。
    const InetAddress listenAddr_; // 监听地址。
    bool started_; // 是否已经启动。
    int nextConnId_; // 为新连接生成唯一 id。

    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> connections_; // 活跃连接表。
    ConnectionCallback connectionCallback_; // 连接建立/关闭回调。
    MessageCallback messageCallback_; // 消息到达回调。
    CloseCallback closeCallback_; // 连接关闭回调。
};
