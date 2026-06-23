#include "net/Epoll.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "base/Logger.h"
#include "net/Channel.h"

namespace
{

const char* operationName(int operation)
{
    switch (operation)
    {
        case EPOLL_CTL_ADD:
            return "ADD";
        case EPOLL_CTL_MOD:
            return "MOD";
        case EPOLL_CTL_DEL:
            return "DEL";
        default:
            return "UNKNOWN";
    }
}

} // namespace

Epoll::Epoll()
    : epollFd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitialEventCount)
{
    if (epollFd_ < 0)
    {
        LOG_FATAL("epoll_create1 failed: %s", std::strerror(errno));
    }
}

Epoll::~Epoll()
{
    ::close(epollFd_);
}

void Epoll::wait(int timeoutMs, ChannelList& activeChannels)
{
    const int eventCount = ::epoll_wait(
        epollFd_,
        events_.data(),
        static_cast<int>(events_.size()),
        timeoutMs);
    const int savedErrno = errno;

    if (eventCount > 0)
    {
        fillActiveChannels(eventCount, activeChannels);
        if (static_cast<std::size_t>(eventCount) == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (eventCount < 0 && savedErrno != EINTR)
    {
        LOG_ERROR("epoll_wait failed: %s", std::strerror(savedErrno));
    }
}

void Epoll::updateChannel(Channel* channel)
{
    const Channel::State state = channel->state();

    if (state == Channel::State::kNew ||
        state == Channel::State::kDeleted)
    {
        // 尚未注册且没有关注事件时无需调用 epoll_ctl。
        if (channel->isNoneEvent())
        {
            return;
        }

        if (state == Channel::State::kNew)
        {
            const auto inserted =
                channels_.emplace(channel->fd(), channel);
            if (!inserted.second && inserted.first->second != channel)
            {
                LOG_FATAL(
                    "fd=%d is already registered by another Channel",
                    channel->fd());
            }
        }

        updateEpoll(EPOLL_CTL_ADD, channel);
        channel->setState(Channel::State::kAdded);
        return;
    }

    if (channel->isNoneEvent())
    {
        // 暂时没有关注事件时从 epoll 删除，但仍保留在 channels_ 中。
        // 后续 enableReading/enableWriting 会重新执行 ADD。
        updateEpoll(EPOLL_CTL_DEL, channel);
        channel->setState(Channel::State::kDeleted);
    }
    else
    {
        updateEpoll(EPOLL_CTL_MOD, channel);
    }
}

void Epoll::removeChannel(Channel* channel)
{
    channels_.erase(channel->fd());

    if (channel->state() == Channel::State::kAdded)
    {
        updateEpoll(EPOLL_CTL_DEL, channel);
    }

    channel->setState(Channel::State::kNew);
}

bool Epoll::hasChannel(const Channel* channel) const
{
    const auto found = channels_.find(channel->fd());
    return found != channels_.end() && found->second == channel;
}

void Epoll::fillActiveChannels(
    int eventCount,
    ChannelList& activeChannels) const
{
    for (int i = 0; i < eventCount; ++i)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->setReceivedEvents(events_[i].events);
        activeChannels.push_back(channel);
    }
}

void Epoll::updateEpoll(int operation, Channel* channel)
{
    struct epoll_event event = {};
    event.events = channel->events();
    event.data.ptr = channel;

    if (::epoll_ctl(epollFd_, operation, channel->fd(), &event) < 0)
    {
        const int error = errno;
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR(
                "epoll_ctl %s fd=%d failed: %s",
                operationName(operation),
                channel->fd(),
                std::strerror(error));
        }
        else
        {
            LOG_FATAL(
                "epoll_ctl %s fd=%d failed: %s",
                operationName(operation),
                channel->fd(),
                std::strerror(error));
        }
    }
}
