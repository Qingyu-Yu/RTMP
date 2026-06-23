#pragma once

#include <cstdint>

class Timer;
class TimerQueue;

// TimerId 是供调用方取消定时器使用的不透明句柄。
// 调用方不拥有 timer_，也不应解引用它；真正的查找与删除由 TimerQueue 完成。
class TimerId
{
public:
    TimerId() = default;

private:
    TimerId(Timer* timer, int64_t sequence)
        : timer_(timer),
          sequence_(sequence)
    {
    }

    Timer* timer_{nullptr}; // 仅作为身份标识，不表示对象仍然存活。
    int64_t sequence_{0};   // 配合指针防止地址复用导致误取消。

    friend class TimerQueue;
};
