#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

#include "base/noncopyable.h"

class Channel;

// Epoll 是 Linux epoll 的直接封装。
//
// 它只做三件事：
//   1. wait() 阻塞等待已注册 fd 的事件；
//   2. updateChannel() 添加或修改 Channel 的关注事件；
//   3. removeChannel() 从 epoll 和内部索引中移除 Channel。
//
// Epoll 不拥有 Channel。所有方法都应由同一个 EventLoop 线程调用。
class Epoll : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Epoll();
    ~Epoll();

    void wait(int timeoutMs, ChannelList& activeChannels);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(const Channel* channel) const;

private:
    using EventList = std::vector<struct epoll_event>;
    using ChannelMap = std::unordered_map<int, Channel*>;

    void fillActiveChannels(int eventCount, ChannelList& activeChannels) const;
    void updateEpoll(int operation, Channel* channel);

    static const int kInitialEventCount = 16;

    int epollFd_;
    EventList events_;
    ChannelMap channels_;
};
