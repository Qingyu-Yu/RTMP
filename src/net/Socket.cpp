#include "net/Socket.h"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "base/Logger.h"
#include "net/InetAddress.h"

Socket::Socket(int socketFd)
    : socketFd_(socketFd)
{
}

Socket::~Socket()
{
    ::close(socketFd_);
}

int Socket::createNonblocking()
{
    const int socketFd = ::socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0);
    if (socketFd < 0)
    {
        LOG_FATAL("socket creation failed: %s", std::strerror(errno));
    }
    return socketFd;
}

void Socket::bindAddress(const InetAddress& localAddress)
{
    if (::bind(
            socketFd_,
            reinterpret_cast<const struct sockaddr*>(
                localAddress.getSockAddr()),
            sizeof(struct sockaddr_in)) < 0)
    {
        LOG_FATAL("bind failed: %s", std::strerror(errno));
    }
}

void Socket::listen()
{
    if (::listen(socketFd_, SOMAXCONN) < 0)
    {
        LOG_FATAL("listen failed: %s", std::strerror(errno));
    }
}

int Socket::accept(InetAddress* peerAddress) const
{
    struct sockaddr_in peer = {};
    socklen_t addressLength = static_cast<socklen_t>(sizeof peer);

    const int connectionFd = ::accept4(
        socketFd_,
        reinterpret_cast<struct sockaddr*>(&peer),
        &addressLength,
        SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connectionFd >= 0 && peerAddress)
    {
        *peerAddress = InetAddress(peer);
    }
    return connectionFd;
}

ssize_t Socket::send(const void* data, std::size_t length) const
{
    return ::send(socketFd_, data, length, MSG_NOSIGNAL);
}

void Socket::shutdownWrite()
{
    if (::shutdown(socketFd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdown write failed: %s", std::strerror(errno));
    }
}

InetAddress Socket::localAddress() const
{
    InetAddress local;
    socklen_t addressLength =
        static_cast<socklen_t>(sizeof(struct sockaddr_in));
    if (::getsockname(
            socketFd_,
            reinterpret_cast<struct sockaddr*>(local.getSockAddr()),
            &addressLength) < 0)
    {
        LOG_ERROR("getsockname failed: %s", std::strerror(errno));
    }
    return local;
}

int Socket::socketError() const
{
    int error = 0;
    socklen_t length = static_cast<socklen_t>(sizeof error);
    if (::getsockopt(
            socketFd_,
            SOL_SOCKET,
            SO_ERROR,
            &error,
            &length) < 0)
    {
        return errno;
    }
    return error;
}

void Socket::setReuseAddress(bool enabled)
{
    const int option = enabled ? 1 : 0;
    if (::setsockopt(
            socketFd_,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof option) < 0)
    {
        LOG_ERROR("setsockopt SO_REUSEADDR failed: %s", std::strerror(errno));
    }
}

void Socket::setReusePort(bool enabled)
{
    const int option = enabled ? 1 : 0;
    if (::setsockopt(
            socketFd_,
            SOL_SOCKET,
            SO_REUSEPORT,
            &option,
            sizeof option) < 0)
    {
        LOG_ERROR("setsockopt SO_REUSEPORT failed: %s", std::strerror(errno));
    }
}
