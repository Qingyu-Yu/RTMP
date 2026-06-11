#pragma once 

#include <vector>
#include <sys/epoll.h>
#include <unistd.h>

#include "Poller.h"

// 基于 epoll 的 Poller 实现。
class EpollPoller : public Poller
{
public:
    EpollPoller(EventLoop* loop);
    ~EpollPoller() override;

        // 使用 epoll_wait 等待事件。
    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;

    // 更新 Channel 的关注事件。
    void updateChannel(Channel* channel) override;

    // 从 epoll 实例中移除 Channel。
    void removeChannel(Channel* channel) override;

private:
    using EventList = std::vector<struct epoll_event>;

    int epollfd_; // epoll 实例的文件描述符。
    EventList events_; // epoll_wait 返回的事件列表。
    static const int kInitEventListSize = 16; // 初始事件列表容量。

    // 将 epoll 返回的事件转换为 activeChannels。
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

    // 执行 epoll_ctl 操作。
    void update(int operation, Channel* channel);
};