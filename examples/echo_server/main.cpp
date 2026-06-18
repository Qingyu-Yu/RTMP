#include "base/Logger.h"
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/InetAddress.h"

int main()
{
    EventLoop loop;
    InetAddress listenAddr(8080);
    TcpServer server(&loop, listenAddr, "EchoServer");

    server.setConnectionCallback([](const TcpConnection::Ptr& conn) {
        if (conn->connected())
        {
            LOG_INFO("Connection established: %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection closed: %s", conn->peerAddress().toIpPort().c_str());
        }
    });

    server.setMessageCallback([](const TcpConnection::Ptr& conn, Buffer* buf) {
        std::string message = buf->retrieveAllAsString();
        LOG_INFO("Received message from %s: %s", conn->peerAddress().toIpPort().c_str(), message.c_str());
        conn->send(message);
    });

    server.start();
    loop.loop();
    return 0;
}
