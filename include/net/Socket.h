#pragma once

#include <cstddef>
#include <sys/types.h>

#include "base/noncopyable.h"

class InetAddress;

// Socket 负责 socket 文件描述符的生命周期和常用系统调用。
//
// Acceptor 和 TcpConnection 只描述网络流程，不再分别处理
// socket/bind/listen/accept/shutdown/close 等底层细节。
class Socket : noncopyable
{
public:
    explicit Socket(int socketFd);
    ~Socket();

    static int createNonblocking();

    int fd() const { return socketFd_; }

    void bindAddress(const InetAddress& localAddress);
    void listen();
    int accept(InetAddress* peerAddress) const;

    ssize_t send(const void* data, std::size_t length) const;
    void shutdownWrite();

    InetAddress localAddress() const;
    int socketError() const;

    void setReuseAddress(bool enabled);
    void setReusePort(bool enabled);

private:
    const int socketFd_;
};
