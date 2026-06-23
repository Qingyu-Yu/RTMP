# Muduo 网络库阅读指南

这份代码采用 Reactor 模型。理解时不要从 `epoll_ctl` 开始，建议按下面顺序阅读：

1. `EventLoop`：事件循环和线程边界；
2. `Channel`：一个 fd 对应哪些回调；
3. `Epoll`：如何等待并返回就绪事件；
4. `Socket/Acceptor/TcpServer`：socket 如何封装、连接如何建立和分配；
5. `TcpConnection/Buffer`：连接状态如何变化、数据如何收发；
6. `TimerQueue`：定时器如何复用同一套事件分发机制。

## 1. 核心对象关系

```text
EventLoop
  ├── Epoll
  │     └── 记录多个 Channel（不拥有）
  ├── wakeupChannel
  │     └── eventfd：跨线程唤醒
  ├── TimerQueue
  │     └── timerfdChannel：定时器到期通知
  └── pendingFunctors
        └── 其他线程提交的任务
```

`EventLoop` 是调度者，`Epoll` 是对 Linux epoll 的直接封装，`Channel` 是事件与
回调的连接器。

- `EventLoop` 不直接读写网络数据；
- `Epoll` 只负责等待事件和维护 fd 注册状态，不处理业务回调；
- `Channel` 不拥有文件描述符；
- `Socket` 使用 RAII 拥有文件描述符，析构时自动关闭；
- `TcpConnection` 拥有连接 Socket、Channel 和收发缓冲区。

## 2. 一轮事件循环

`EventLoop::loop()` 每轮执行三件事：

```text
epoll_wait
    ↓
得到 activeChannels
    ↓
Channel::handleEvent
    ↓
TcpConnection / Acceptor / TimerQueue 回调
    ↓
doPendingFunctors
```

Linux 把 socket、`eventfd` 和 `timerfd` 都表现为文件描述符，因此它们可以统一交给
epoll 等待：

- 连接 socket 可读：收到网络数据；
- 监听 socket 可读：有新连接可 `accept`；
- `eventfd` 可读：其他线程提交了任务；
- `timerfd` 可读：有定时器到期。

## 3. 新连接建立流程

```text
listen socket 可读
    ↓
Acceptor::handleRead
    ↓ accept()
得到 connectionFd
    ↓
TcpServer::newConnection
    ↓
线程池选择 ioLoop
    ↓
创建 TcpConnection + Channel
    ↓
ioLoop->runInLoop(connectEstablished)
    ↓
Channel 开始监听 EPOLLIN
```

主循环 `baseLoop` 负责监听和维护连接表，工作循环 `ioLoop` 负责具体连接的 IO。
一条连接选定 `ioLoop` 后，不会在工作线程之间迁移。

`Socket` 集中封装 `socket/bind/listen/accept/send/shutdown/close`。因此
`Acceptor` 主要表达“接受连接”，`TcpConnection` 主要表达“收发数据”，系统调用
错误处理不会散落在多个业务类中。

## 4. 收数据流程

```text
连接 fd 出现 EPOLLIN
    ↓
Channel::handleEvent
    ↓
TcpConnection::handleRead
    ↓
Buffer::readFd
    ↓
messageCallback
```

`Buffer` 中没有被上层 `retrieve` 的数据会继续保留。这一点用于处理 TCP 半包：
一次读取不一定得到一条完整协议消息，上层解析器可以等待后续数据。

## 5. 发数据流程

`TcpConnection::send` 有两条路径：

```text
socket 当前可写且没有积压
    ↓
直接 send
```

如果只发送了一部分，或 socket 暂时不可写：

```text
剩余数据写入 outputBuffer
    ↓
Channel 开启 EPOLLOUT
    ↓
可写事件触发 handleWrite
    ↓
继续发送
    ↓
缓冲区清空后关闭 EPOLLOUT
```

不能一直监听 `EPOLLOUT`，因为 socket 大多数时间都可写，会导致事件循环持续空转。

## 6. 为什么需要 runInLoop 和 queueInLoop

每个 `TcpConnection` 的 socket、Channel 和 Buffer 都只允许所属 `EventLoop` 线程修改。
这叫线程封闭，避免在大量字段上分别加锁。

- `runInLoop`：已经在 loop 线程就立即执行，否则排队；
- `queueInLoop`：总是排队到下一次任务处理阶段；
- `wakeup`：其他线程提交任务后，通过 `eventfd` 唤醒 `epoll_wait`。

异步任务通常捕获 `shared_ptr<TcpConnection>`，保证任务真正执行前连接对象不会析构。

只允许 loop 线程调用的函数会执行 `assertInLoopThread()`。如果错误地从其他线程操作
Channel 或 Epoll，程序会在错误发生的位置终止，而不是稍后表现为随机数据竞争。

## 7. 连接状态

`TcpConnection` 不再用一个布尔值同时表示所有生命周期，而是使用四个明确状态：

```text
kConnecting
    ↓ connectEstablished
kConnected
    ↓ shutdown
kDisconnecting
    ↓ 对端关闭或本地销毁
kDisconnected
```

- `send()` 只接受已连接状态；
- `shutdown()` 将状态切换为正在断开，并等待输出缓冲区发送完成；
- `handleClose()` 负责通知上层；
- `connectDestroyed()` 必须回到连接所属 ioLoop，最终从 Epoll 移除 Channel。

## 8. 定时器流程

`TimerQueue` 用两个集合管理 Timer：

- `timers_` 按到期时间排序，用于快速找到最近到期任务；
- `activeTimers_` 按 Timer 身份查找，用于取消任务。

只有最近到期时间会写入 `timerfd`：

```text
添加 Timer
    ↓
如果成为最早 Timer，重设 timerfd
    ↓
timerfd 到期可读
    ↓
取出全部已到期 Timer
    ↓
执行回调
    ↓
重复 Timer 计算下一次时间并重新插入
```

回调执行期间取消重复 Timer 是特殊情况：该 Timer 已暂时离开主集合，不能立即删除。
`cancelingTimers_` 记录取消请求，等本批回调结束后再安全释放。

## 9. 对象所有权

```text
TcpServer
  └── connections_[connection.name()] 持有 shared_ptr<TcpConnection>
        ├── Socket 独占并自动关闭 fd
        ├── 独占 Channel
        ├── 拥有 inputBuffer
        └── 拥有 outputBuffer
```

`Channel::tie` 保存的是 `weak_ptr`。事件发生时临时提升成 `shared_ptr`，保证整个回调期间
`TcpConnection` 存活，同时又不会形成循环引用。

关闭连接时先回到 `baseLoop` 删除连接表中的 `shared_ptr`，再清理 Channel。等所有临时
`shared_ptr` 释放后，`TcpConnection` 析构，其成员 `Socket` 自动关闭 fd。

连接名称同时作为 `connections_` 的 key，因此关闭时可以通过 `conn->name()` 直接删除，
不再遍历整个连接表。

## 10. 推荐调试断点

首次跟代码时，可按顺序在以下函数设置断点：

1. `Acceptor::handleRead`
2. `TcpServer::newConnection`
3. `TcpConnection::connectEstablished`
4. `Epoll::wait`
5. `Channel::handleEventWithGuard`
6. `TcpConnection::handleRead`
7. `TcpConnection::sendInLoop`
8. `TcpConnection::handleClose`

运行 `echo_server`，再使用 `nc 127.0.0.1 8080` 连接，可完整观察连接建立、收发和关闭过程。
