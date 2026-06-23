#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "base/Timestamp.h"
#include "base/noncopyable.h"

using TimerCallback = std::function<void()>;

// Timer 只描述一个定时任务，不负责等待时间。
// TimerQueue 按 expiration_ 排序，并通过 timerfd 获得到期通知。
class Timer : noncopyable
{
public:
    Timer(TimerCallback callback, Timestamp when, double interval);

    void run() const { callback_(); }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int64_t sequence() const { return sequence_; }

    // 重复定时器从当前触发时间重新计算下一次到期时间。
    void restart(Timestamp now);

private:
    const TimerCallback callback_; // 到期时在 EventLoop 线程中执行。
    Timestamp expiration_;         // 下一次到期的绝对系统时间。
    const double interval_;        // 重复间隔，单位为秒；0 表示单次。
    const bool repeat_;            // 缓存 interval_ > 0 的判断。
    const int64_t sequence_;       // 唯一序号，避免仅凭裸指针误认定时器。

    static std::atomic<int64_t> s_numCreated_;
};
