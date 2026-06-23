#pragma once

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/Timestamp.h"
#include "base/noncopyable.h"
#include "net/Timer.h"
#include "net/TimerId.h"

class Channel;
class EventLoop;

// 每个 EventLoop 拥有一个 TimerQueue。
//
// timers_ 按到期时间排序，但内核只需知道“最近一个”到期时间：
//   TimerQueue -> 设置 timerfd -> timerfd 可读 -> Channel -> handleRead()
//
// 所有集合修改均在所属 EventLoop 线程执行。addTimer()/cancel() 虽可跨线程调用，
// 但它们会通过 runInLoop() 将实际操作切回 loop 线程。
class TimerQueue : noncopyable
{
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerId addTimer(TimerCallback callback, Timestamp when, double interval);
    void cancel(TimerId timerId);

private:
    using Entry = std::pair<Timestamp, Timer*>; // 排序键：到期时间，其次裸指针。
    using TimerList = std::set<Entry>;
    using ActiveTimer = std::pair<Timer*, int64_t>;
    using ActiveTimerSet = std::set<ActiveTimer>;

    void addTimerInLoop(Timer* timer);
    void cancelInLoop(TimerId timerId);
    void handleRead(Timestamp receiveTime);

    std::vector<Entry> getExpired(Timestamp now);
    void reset(const std::vector<Entry>& expired, Timestamp now);
    bool insert(Timer* timer);

    EventLoop* loop_;                         // TimerQueue 所属事件循环。
    const int timerfd_;                       // Linux 定时器文件描述符。
    std::unique_ptr<Channel> timerfdChannel_; // 将 timerfd 可读事件接入 Poller。
    TimerList timers_;                        // 按到期时间排序，拥有 Timer 对象。
    ActiveTimerSet activeTimers_;             // 按身份查找，用于 cancel。
    bool callingExpiredTimers_{false};        // 当前是否正在执行一批到期回调。
    ActiveTimerSet cancelingTimers_;           // 回调执行期间请求取消的重复定时器。
};
