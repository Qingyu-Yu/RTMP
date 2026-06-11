# Muduo 库使用手册

## 1. 介绍

`muduo` 是一个轻量级的 C++ 网络库，实现了基于 Reactor 模型的 TCP 服务端框架。当前实现支持：

- 事件循环 (`EventLoop`)
- 多路复用器封装 (`Poller` / `EpollPoller`)
- 事件通道 (`Channel`)
- 监听器 (`Acceptor`)
- TCP 连接封装 (`TcpConnection`)
- TCP 服务器封装 (`TcpServer`)
- 线程池事件循环 (`EventLoopThreadPool`)
- 网络缓冲区 (`Buffer`)
- IPv4 地址封装 (`InetAddress`)
- 简易日志系统 (`Logger`)

该库适用于构建基本的 TCP 服务，如回显服务器、消息分发服务或自定义协议服务器。

## 2. 目录结构

```
include/
  base/
    Logger.h
    Timestamp.h
    noncopyable.h
  net/
    Acceptor.h
    Buffer.h
    Channel.h
    CurrentThread.h
    DefaultPoller.h
    EpollPoller.h
    EventLoop.h
    EventLoopThread.h
    EventLoopThreadPool.h
    InetAddress.h
    Poller.h
    TcpConnection.h
    TcpServer.h

src/
  Logger.cpp
  Timestamp.cpp
  InetAddress.cpp
  Buffer.cpp
  Channel.cpp
  Poller.cpp
  EpollPoller.cpp
  EventLoop.cpp
  Acceptor.cpp
  TcpConnection.cpp
  TcpServer.cpp
  EventLoopThread.cpp
  EventLoopThreadPool.cpp
  CurrentThread.cpp
  DefaultPoller.cpp

test/
  test.cpp
CMakeLists.txt
```

## 3. 构建

在项目根目录执行：

```bash
rm -rf build
mkdir -p build
cd build
cmake ..
make -j4
```

构建后生成文件：

- 静态库：`lib/libmuduo.a`
- 动态库：`lib/libmuduo.so`
- 测试可执行文件：`bin/muduo_test`

## 4. 运行示例

在项目根目录运行：

```bash
./bin/muduo_test
```

然后可以使用 `nc` 测试：

```bash
echo -e "hello world" | nc 127.0.0.1 8080
```

如果服务器正常工作，客户端将收到相同的回显文本。

## 5. 主要类说明

### 5.1 `InetAddress`

用于封装 IPv4 地址。

构造示例：

```cpp
InetAddress listenAddr(8080);
InetAddress listenAddr(8080, "0.0.0.0");
```

常用接口：

- `toIp()`
- `toIpPort()`
- `toPort()`

### 5.2 `EventLoop`

负责事件循环和跨线程任务调度。

核心方法：

- `loop()`
- `quit()`
- `runInLoop(cb)`
- `queueInLoop(cb)`
- `updateChannel(channel)`
- `removeChannel(channel)`

### 5.3 `Buffer`

用于管理网络读写缓冲区。

主要功能：

- `readableBytes()`
- `writableBytes()`
- `peek()`
- `retrieve(len)`
- `retrieveAllAsString()`
- `append(data, len)`
- `readFd(fd, &savedErrno)`

### 5.4 `Channel`

代表一个文件描述符上的事件通道，负责将 I/O 事件分发给回调。

### 5.5 `TcpConnection`

封装 TCP 连接对象，提供发送、关闭、事件处理等功能。

回调类型：

- `ConnectionCallback`：连接建立/关闭
- `MessageCallback`：消息到达
- `CloseCallback`：连接关闭

接口示例：

```cpp
conn->send(message);
conn->shutdown();
```

### 5.6 `TcpServer`

封装 TCP 服务端，负责监听新连接、创建 `TcpConnection` 并分发事件。

常用方法：

- `setConnectionCallback(...)`
- `setMessageCallback(...)`
- `setCloseCallback(...)`
- `setThreadNum(...)`
- `start()`

## 6. 示例代码

`test/test.cpp` 是一个典型回显服务器示例：

```cpp
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
```

## 7. 使用步骤总结

1. 创建 `EventLoop`。
2. 创建 `InetAddress`。
3. 创建 `TcpServer`。
4. 设置连接、消息和关闭回调。
5. 调用 `server.start()`。
6. 调用 `loop.loop()` 进入事件循环。

## 8. 注意事项

- 当前库仅支持 IPv4。
- `TcpServer` 默认使用 `SO_REUSEADDR` 和 `SO_REUSEPORT`。
- `TcpConnection::send()` 仅在连接处于已连接状态时发送数据。
- 线程池为空时，所有连接会在主线程 `EventLoop` 中处理。
- 日志系统为简单调试用途，非线程安全。

## 9. 后续扩展建议

如果需要继续完善，可以考虑添加：

- `TcpClient` 客户端支持
- 定时器功能
- 信号处理
- HTTP/自定义协议支持
- 日志线程安全
- IPv6 支持
