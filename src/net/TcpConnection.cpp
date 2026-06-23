#include "net/TcpConnection.h"
#include "base/Logger.h"
#include "net/EventLoop.h"

#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <cstring>

TcpConnection::TcpConnection(EventLoop* loop, int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
    : loop_(loop),
      channel_(new Channel(loop, sockfd)),
      name_("TcpConnection"),
      sockfd_(sockfd),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      connected_(false)
{
    // Channel 保存 this 回调，但 TcpServer 随后会通过 channel()->tie(conn)
    // 绑定 shared_ptr 生命周期，避免事件到达时 TcpConnection 已被销毁。
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
}

TcpConnection::~TcpConnection()
{
    if (sockfd_ >= 0)
    {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

void TcpConnection::send(const std::string& message)
{
    if (connected_)
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
    if (connected_)
    {
        connected_ = false;
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
    // 此函数由 TcpServer 投递到连接所属的 ioLoop，因此下面的 Channel 操作
    // 与后续读写事件始终发生在同一个线程。
    connected_ = true;
    // 连接建立后开始监听可读事件，接收对端数据。
    channel_->enableReading();
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed()
{
    if (connected_)
    {
        connected_ = false;
        channel_->disableAll();
        channel_->remove();
        if (connectionCallback_)
        {
            // 这里通知上层连接关闭，用户可以释放资源。
            connectionCallback_(shared_from_this());
        }
    }
}

void TcpConnection::handleRead(Timestamp)
{
    // 从 socket 读取数据到输入缓冲区。
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);
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
    // Channel 可写事件触发，继续发送缓冲区中剩余的数据。
    if (outputBuffer_.readableBytes() > 0)
    {
        ssize_t n = ::send(sockfd_, outputBuffer_.peek(), outputBuffer_.readableBytes(), 0);
        if (n > 0)
        {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            if (outputBuffer_.readableBytes() == 0)
            {
                // 数据全部发送完成，取消写事件监听。
                channel_->disableWriting();
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
    // 这里只发起关闭流程。closeCallback_ 通常指向 TcpServer::removeConnection，
    // 后者会回到 baseLoop 删除连接表中的 shared_ptr。
    connected_ = false;
    channel_->disableAll();
    channel_->remove();
    if (closeCallback_)
    {
        closeCallback_(shared_from_this());
    }
}

void TcpConnection::handleError()
{
    int err = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(err));
    if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
    {
        err = errno;
    }
    LOG_ERROR("TcpConnection socket error: %s", strerror(err));
}

void TcpConnection::sendInLoop(const std::string& message)
{
    // 快速路径：当前没有积压数据时先直接 send，避免一次缓冲区拷贝。
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        ssize_t n = ::send(sockfd_, message.data(), message.size(), 0);
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
    // 关闭连接的写端，通知对端不再发送数据。
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("TcpConnection shutdown error: %s", strerror(errno));
    }
}
