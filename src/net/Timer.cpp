#include "net/Timer.h"

#include <utility>

std::atomic<int64_t> Timer::s_numCreated_{0};

Timer::Timer(TimerCallback callback, Timestamp when, double interval)
    : callback_(std::move(callback)),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(++s_numCreated_)
{
}

void Timer::restart(Timestamp now)
{
    // 以上一次回调实际执行的时间 now 为基准，可避免回调耗时导致下一次时间
    // 已经落在过去；代价是重复定时器并非严格固定频率。
    expiration_ = repeat_ ? addTime(now, interval_) : Timestamp::invalid();
}
