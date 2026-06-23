#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "base/Timestamp.h"
#include "base/noncopyable.h"
#include "net/CurrentThread.h"
#include "net/TimerId.h"

class Channel;
class Epoll;
class TimerQueue;

// EventLoop 是 Reactor 模型的核心：一个线程只能运行一个 EventLoop。
//
// 一次循环的工作顺序：
//   1. Epoll 阻塞等待文件描述符就绪；
//   2. 将就绪事件分发给对应的 Channel；
//   3. 执行其他线程通过 queueInLoop() 提交的任务。
//
// EventLoop 不直接读写 socket。它负责调度，真正的 IO 由 Channel 回调指向的
// Acceptor、TcpConnection、TimerQueue 等对象完成。
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;
    using ChannelList = std::vector<Channel*>;

    EventLoop();
    ~EventLoop();

    // 启动事件循环。此方法会阻塞，直到 quit() 被调用。
    void loop();

    // 请求事件循环退出。如果在其他线程中调用，会唤醒 loop 所在线程。
    void quit();

    // 定时任务接口。这些方法允许跨线程调用，TimerQueue 会将实际修改切换到
    // EventLoop 所在线程，避免多个线程同时操作定时器集合。
    // runAt 使用绝对时间，runAfter/runEvery 的参数单位为秒。
    TimerId runAt(Timestamp time, Functor cb);
    TimerId runAfter(double delay, Functor cb);
    TimerId runEvery(double interval, Functor cb);
    void cancel(TimerId timerId);

    // 如果调用者就在本 EventLoop 线程中，立即执行；否则加入任务队列。
    // 适用于“能立即执行就立即执行”的内部操作。
    void runInLoop(Functor cb);

    // 无论从哪个线程调用，都先加入任务队列，稍后由 loop() 执行。
    // 这是跨线程修改连接状态的主要入口。
    void queueInLoop(Functor cb);

    // 向 eventfd 写入数据，使正在 epoll_wait() 中休眠的 loop 立即返回。
    void wakeup();

    // Channel 通过以下接口修改自己在 Epoll 中关注的事件。
    // 调用方应当位于当前 EventLoop 线程。
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    // 如果当前线程是 loop 所在线程，则返回 true。
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    // 核心对象采用线程封闭，不为每个字段加锁。调用位置错误时立即终止，
    // 比静默产生数据竞争或跨线程操作 epoll 更容易定位。
    void assertInLoopThread() const;

private:
    void handleRead(); // 读取并清空 wakeup eventfd。
    void doPendingFunctors(); // 执行队列中的回调。

    std::unique_ptr<Epoll> epoll_;            // Linux epoll 的直接封装。
    std::unique_ptr<TimerQueue> timerQueue_;  // 通过 timerfd 接入同一个 Epoll。
    ChannelList activeChannels_;              // 本轮发生事件的 Channel，不拥有对象。
    std::vector<Functor> pendingFunctors_;     // 等待在 loop 线程执行的任务。

    int wakeupFd_;                            // 跨线程唤醒使用的 eventfd。
    std::unique_ptr<Channel> wakeupChannel_;  // 把 wakeupFd_ 接入 Epoll。
    const pid_t threadId_;                    // 创建并拥有该 EventLoop 的线程。
    std::mutex mutex_;                        // 仅保护 pendingFunctors_。

    std::atomic_bool looping_;                // loop() 是否正在运行。
    std::atomic_bool quit_;                   // 是否请求退出 loop()。
    std::atomic_bool callingPendingFunctors_; // 是否正在执行任务队列。
};
