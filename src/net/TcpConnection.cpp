#include "net/TcpConnection.h"
#include "base/Logger.h"
#include "net/EventLoop.h"

#include <cerrno>
#include <cstring>

TcpConnection::TcpConnection(EventLoop* loop,
                             std::string name,
                             int socketFd,
                             const InetAddress& peerAddress)
    : loop_(loop),
      name_(std::move(name)),
      socket_(socketFd),
      channel_(std::make_unique<Channel>(loop, socketFd)),
      localAddress_(socket_.localAddress()),
      peerAddress_(peerAddress),
      state_(State::kConnecting)
{
    // Channel 保存 this 回调，但 TcpServer 随后会通过 channel()->tie(conn)
    // 绑定 shared_ptr 生命周期，避免事件到达时 TcpConnection 已被销毁。
    channel_->setReadCallback([this] { handleRead(); });
    channel_->setWriteCallback([this] { handleWrite(); });
    channel_->setCloseCallback([this] { handleClose(); });
    channel_->setErrorCallback([this] { handleError(); });
}

bool TcpConnection::connected() const
{
    return state_.load() == State::kConnected;
}

void TcpConnection::send(const std::string& message)
{
    if (connected())
    {
        if (loop_->isInLoopThread())
        {
            // 已在所属 IO 线程，可以直接访问 socket 和 outputBuffer_。
            sendInLoop(message);
        }
        else
        {
            // TcpConnection 的 IO 状态不加锁，而是遵循“线程封闭”：
            // 所有修改都切回 loop_ 线程串行执行。
            const auto self = shared_from_this();
            loop_->queueInLoop([self, message] {
                self->sendInLoop(message);
            });
        }
    }
}

void TcpConnection::shutdown()
{
    State expected = State::kConnected;
    if (state_.compare_exchange_strong(expected, State::kDisconnecting))
    {
        if (loop_->isInLoopThread())
        {
            shutdownInLoop();
        }
        else
        {
            const auto self = shared_from_this();
            loop_->queueInLoop([self] {
                self->shutdownInLoop();
            });
        }
    }
}

void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();
    // 此函数由 TcpServer 投递到连接所属的 ioLoop，因此下面的 Channel 操作
    // 与后续读写事件始终发生在同一个线程。
    if (state_.load() != State::kConnecting)
    {
        LOG_FATAL("connection %s established from invalid state", name_.c_str());
    }
    state_ = State::kConnected;
    // 连接建立后开始监听可读事件，接收对端数据。
    channel_->enableReading();
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_.load() != State::kDisconnected)
    {
        state_ = State::kDisconnected;
        channel_->disableAll();
        if (connectionCallback_)
        {
            connectionCallback_(shared_from_this());
        }
    }
    channel_->remove();
}

void TcpConnection::handleRead()
{
    loop_->assertInLoopThread();
    // 从 socket 读取数据到输入缓冲区。
    int savedErrno = 0;
    const ssize_t n = inputBuffer_.readFd(socket_.fd(), &savedErrno);
    if (n > 0)
    {
        // 数据读取成功，通知上层应用处理。
        if (messageCallback_)
        {
            // 上层直接消费 inputBuffer_。回调返回后，未 retrieve 的数据仍保留，
            // 可用于处理 TCP 半包。
            messageCallback_(shared_from_this(), &inputBuffer_);
        }
    }
    else if (n == 0)
    {
        // 对端关闭连接。
        handleClose();
    }
    else
    {
        // 发生错误但不是正常的 EAGAIN/EWOULDBLOCK。
        if (savedErrno != EWOULDBLOCK && savedErrno != EAGAIN)
        {
            handleError();
        }
    }
}

void TcpConnection::handleWrite()
{
    loop_->assertInLoopThread();
    // Channel 可写事件触发，继续发送缓冲区中剩余的数据。
    if (outputBuffer_.readableBytes() > 0)
    {
        const ssize_t n = socket_.send(
            outputBuffer_.peek(),
            outputBuffer_.readableBytes());
        if (n > 0)
        {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            if (outputBuffer_.readableBytes() == 0)
            {
                // 数据全部发送完成，取消写事件监听。
                channel_->disableWriting();
                if (state_.load() == State::kDisconnecting)
                {
                    shutdownInLoop();
                }
                if (writeCompleteCallback_)
                {
                    writeCompleteCallback_();
                }
            }
        }
        else if (n < 0)
        {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
            {
                handleError();
            }
        }
    }
}

void TcpConnection::handleClose()
{
    loop_->assertInLoopThread();
    if (state_.exchange(State::kDisconnected) == State::kDisconnected)
    {
        return;
    }

    // 这里只发起关闭流程。closeCallback_ 通常指向 TcpServer::removeConnection，
    // 后者会回到 baseLoop 删除连接表中的 shared_ptr。
    channel_->disableAll();
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());
    }
    if (closeCallback_)
    {
        closeCallback_(shared_from_this());
    }
}

void TcpConnection::handleError()
{
    loop_->assertInLoopThread();
    const int error = socket_.socketError();
    LOG_ERROR(
        "TcpConnection %s socket error: %s",
        name_.c_str(),
        std::strerror(error));
}

void TcpConnection::sendInLoop(const std::string& message)
{
    loop_->assertInLoopThread();
    if (state_.load() == State::kDisconnected)
    {
        return;
    }

    // 快速路径：当前没有积压数据时先直接 send，避免一次缓冲区拷贝。
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        const ssize_t n = socket_.send(message.data(), message.size());
        if (n >= 0)
        {
            if (static_cast<size_t>(n) < message.size())
            {
                // 未发送完的数据写入输出缓冲区，并继续监听可写事件。
                outputBuffer_.append(message.data() + n, message.size() - static_cast<size_t>(n));
                channel_->enableWriting();
            }
            else if (writeCompleteCallback_)
            {
                writeCompleteCallback_();
            }
            return;
        }
        else if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            LOG_ERROR("TcpConnection send error: %s", strerror(errno));
            return;
        }
    }

    // 慢速路径：socket 发送缓冲区已满，或之前已有积压数据。
    // 先把数据追加到 outputBuffer_，等 EPOLLOUT 到来后由 handleWrite() 继续发送。
    outputBuffer_.append(message);
    channel_->enableWriting();
}

void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread();
    // 输出缓冲区发送完成后再关闭写端，避免丢失已经排队的数据。
    if (state_.load() == State::kDisconnecting &&
        !channel_->isWriting())
    {
        socket_.shutdownWrite();
    }
}
