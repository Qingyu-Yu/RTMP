#pragma once

#include <string>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>

// InetAddress 封装了 IPv4 socket 地址对象。
class InetAddress
{
public:
    InetAddress()
    {
        memset(&addr_, 0, sizeof(addr_));
    }

    explicit InetAddress(uint16_t port, std::string ip = "127.0.0.1");
    explicit InetAddress(const struct sockaddr_in& addr)
        : addr_(addr)
    {}

    // 返回点分十进制 IP 字符串。
    std::string toIp() const;

    // 返回 ip:port 格式字符串。
    std::string toIpPort() const;

    // 返回主机字节序的端口号。
    uint16_t toPort() const;

    // 返回内部 sockaddr_in 的只读指针。
    const struct sockaddr_in* getSockAddr() const { return &addr_; }

    // 返回内部 sockaddr_in 的可写指针。
    struct sockaddr_in* getSockAddr() { return &addr_; }

private:
    struct sockaddr_in addr_; // IPv4 地址存储结构。
};