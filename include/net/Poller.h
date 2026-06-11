#pragma once

#include <vector>
#include <unordered_map>

#include "base/noncopyable.h"
#include "base/Timestamp.h"

class EventLoop;
class Channel;

class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    // 构造函数，关联所属 EventLoop。
    Poller(EventLoop* loop);
    virtual ~Poller();

    // 等待 IO 事件，并将发生的 Channel 填充到 activeChannels 中。
    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

    // 注册或更新 Channel 的监听事件。
    virtual void updateChannel(Channel* channel) = 0;

    // 从 IO 多路复用器中移除 Channel。
    virtual void removeChannel(Channel* channel) = 0;

    // 判断 Channel 是否已经注册。
    bool hasChannel(Channel* channel) const;

    // 创建当前平台的默认 Poller 实现。
    static Poller* newDefaultPoller(EventLoop* loop);

protected:
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_; // 已注册的 fd 到 Channel 的映射。

private:
    EventLoop* ownerLoop_; // 拥有该 Poller 的事件循环。
};