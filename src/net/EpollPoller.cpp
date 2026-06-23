#include <errno.h>

#include "net/EpollPoller.h"
#include "net/Channel.h"
#include "base/Logger.h"
#include <cstring>

namespace
{

// Channel::index_ 的状态机：
//   kNew     -- 从未加入 epoll
//   kAdded   -- 已通过 EPOLL_CTL_ADD 加入 epoll
//   kDeleted -- 仍由 Poller 记录，但当前没有关注事件，已从 epoll 删除
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;

} // namespace

EpollPoller::EpollPoller(EventLoop *loop)
    :Poller(loop),
     epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
     events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("EpollPoller create epollfd failed");
    }
}

EpollPoller::~EpollPoller()
{
    ::close(epollfd_);
}

Timestamp EpollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    const int numEvents = ::epoll_wait(
        epollfd_,
        events_.data(),
        static_cast<int>(events_.size()),
        timeoutMs);
    const int savedErrno = errno;
    const Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == events_.size())
        {
            // 如果返回事件数刚好等于当前数组大小，则扩大数组以接收更多事件。
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents < 0)
    {
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_ERROR("EpollPoller::poll() error");
        }
    }
    return now;
}

void EpollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    if (index == kNew || index == kDeleted)
    {
        // 新 Channel 或曾被 DEL 的 Channel，只要重新关注事件就执行 ADD。
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        if (channel->isNoneEvent())
        {
            // Channel 暂时不关心任何事件，先从 epoll 删除，但保留在 channels_，
            // 后续重新 enableReading/enableWriting 时可再次 ADD。
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            // 已在 epoll 中，只需修改关注事件。
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);
    if (channel->index() == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}
void EpollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        // update() 把 Channel 指针存入 data.ptr，这里原样取回。
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void EpollPoller::update(int operation, Channel *channel)
{
    epoll_event event = {};
    event.events = channel->events();
    // 保存 Channel 指针后，epoll_wait 返回时无需再通过 fd 查表。
    event.data.ptr = channel;
    const int fd = channel->fd();
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl failed");
        }
        else
        {
            LOG_FATAL("epoll_ctl failed");
        }
    }

}
