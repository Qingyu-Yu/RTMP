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
     revents_(0),
     index_(-1),
     tied_(false)
{
}

Channel::~Channel()
{ 
}

void Channel::handleEvent(Timestamp receiveTime)
{
    // 在事件到来时，先检查是否已绑定到拥有者对象。
    // 这可以避免当拥有者对象销毁后继续处理事件导致悬空访问。
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
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
    // 从 EventLoop/ Poller 中移除当前 Channel，停止监听该 fd 的事件。
    if (loop_)
    {
        loop_->removeChannel(this);
    }
}

void Channel::update()
{
    // 更新当前 Channel 在 Poller 中的注册状态。
    if (loop_)
    {
        loop_->updateChannel(this);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    // 根据 revents_ 调用相应回调。
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);
    }
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }
    
}
