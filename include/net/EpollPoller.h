#pragma once 

#include <vector>
#include <sys/epoll.h>
#include <unistd.h>

#include "Poller.h"

// Poller 的 Linux epoll 实现。
// 一个 EpollPoller 只服务于一个 EventLoop，也只能在该 loop 线程中操作。
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

    int epollfd_;      // epoll_create1() 创建的内核 epoll 实例。
    EventList events_; // 接收 epoll_wait() 输出；容量不足时动态扩展。
    static const int kInitEventListSize = 16; // 初始事件列表容量。

    // 将 epoll 返回的事件转换为 activeChannels。
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

    // 执行 epoll_ctl 操作。
    void update(int operation, Channel* channel);
};
