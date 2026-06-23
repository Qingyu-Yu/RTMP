#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

#include "base/noncopyable.h"

class EventLoop;

// EventLoopThread 创建一个线程，并在该线程栈上创建、运行一个 EventLoop。
// startLoop() 会等待子线程完成初始化后再返回 EventLoop 指针。
class EventLoopThread : noncopyable
{
public:
    EventLoopThread();
    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

    std::thread thread_;
    EventLoop* loop_; // 指向子线程栈上的 EventLoop，仅在线程运行期间有效。
    std::mutex mutex_;
    std::condition_variable cond_;
};
