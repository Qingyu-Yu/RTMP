#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

#include "base/noncopyable.h"

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    EventLoopThread();
    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

    std::thread thread_;
    EventLoop* loop_;
    bool exiting_;
    std::mutex mutex_;
    std::condition_variable cond_;
};
