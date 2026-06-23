#include <sys/epoll.h>

#include "net/Channel.h"
#include "net/EventLoop.h"
#include "base/Logger.h"

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    :loop_(loop),
     fd_(fd),
     events_(0),
     receivedEvents_(0),
     state_(State::kNew),
     tied_(false)
{
}

Channel::~Channel()
{ 
}

void Channel::handleEvent()
{
    // 在事件到来时，先检查是否已绑定到拥有者对象。
    // 这可以避免当拥有者对象销毁后继续处理事件导致悬空访问。
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard();
        }
    }
    else
    {
        handleEventWithGuard();
    }
}

// 将 Channel 与拥有者对象关联，当拥有者被销毁时避免继续处理事件。
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

void Channel::remove()
{
    // 从 EventLoop/Epoll 中移除当前 Channel，停止监听该 fd 的事件。
    if (loop_)
    {
        loop_->removeChannel(this);
    }
}

void Channel::update()
{
    // 更新当前 Channel 在 Epoll 中的注册状态。
    if (loop_)
    {
        loop_->updateChannel(this);
    }
}

void Channel::handleEventWithGuard()
{
    // 一个 fd 同一轮可能同时带有多个标志，因此这里不是互斥的 else-if。
    // 处理顺序与 muduo 一致：挂断/错误 -> 可读 -> 可写。
    if ((receivedEvents_ & EPOLLHUP) && !(receivedEvents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();
    }
    if (receivedEvents_ & EPOLLERR)
    {
        if (errorCallback_) errorCallback_();
    }
    if (receivedEvents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        // EPOLLRDHUP 表示对端关闭写端，也交给 read 回调读取；read 返回 0 后
        // TcpConnection 会进入关闭流程。
        if (readCallback_) readCallback_();
    }
    if (receivedEvents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }
    
}
