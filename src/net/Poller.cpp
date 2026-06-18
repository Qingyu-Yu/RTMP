#include "net/Poller.h"
#include "net/Channel.h"


Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

Poller::~Poller()
{
}

bool Poller::hasChannel(Channel *channel) const
{
    // 检查 channel 是否已经注册到当前 Poller 中。
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}
