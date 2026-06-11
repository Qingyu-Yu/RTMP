#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>

#include "base/noncopyable.h"
#include "base/Logger.h"
#include "base/Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

class EventLoop : noncopyable
{
    using Functor = std::function<void()>;
    using ChannelLists = std::vector<Channel*>;
public:
    EventLoop();
    ~EventLoop();

    // 启动事件循环。此方法会阻塞，直到 quit() 被调用。
    void loop();

    // 请求事件循环退出。如果在其他线程中调用，会唤醒 loop 所在线程。
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    // 如果当前在 loop 线程，则立即执行回调；否则加入队列延后执行。
    // 该设计保证线程安全，避免跨线程直接访问 loop 内部数据。
    void runInLoop(Functor cb);

    // 将回调加入队列，在 loop 线程中执行，并在必要时唤醒 loop。
    // 这是跨线程提交任务的主要入口。
    void queueInLoop(Functor cb);

    // 通过 eventfd 唤醒 loop 线程。
    // 主要用于 another thread 调用 queueInLoop 或 quit 时唤醒正在阻塞的 poll。
    void wakeup();

    // Poller 操作
    // EventLoop 作为 Poller 的接口层，负责将 channel 注册、更新和删除请求转发给底层 Poller。
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    // 如果当前线程是 loop 所在线程，则返回 true。
    bool isInLoopThread() const { return ThreadId_ == CurrentThread::tid(); }

private:
    void handleRead(Timestamp receiveTime); // 处理 wakeup eventfd 的读事件。
    void doPendingFunctors(); // 执行队列中的回调。

    std::unique_ptr<Poller> poller_; // 事件轮询器。
    ChannelLists activeChannels_; // 本轮 poll 返回的活跃 Channel 列表。
    std::vector<Functor> pendingFunctors_; // 待执行的回调队列。

    Channel* currentActiveChannel_; // 当前正在处理的 Channel。
    int wakeupFd_; // 用于唤醒 loop 的 eventfd。
    std::unique_ptr<Channel> wakeupChannel_; // wakeup event 对应的 Channel。
    const pid_t ThreadId_; // loop 所在线程 id。
    Timestamp pollReturnTime_; // 最近一次 poll 返回的时间点。
    std::mutex mutex_; // 保护 pendingFunctors_。

    std::atomic_bool looping_; // 是否正在运行事件循环。
    std::atomic_bool quit_; // 是否已经请求退出。
    std::atomic_bool callingPendingFunctors_; // 是否正在执行回调。
};