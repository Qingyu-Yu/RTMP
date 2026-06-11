#include "net/EventLoopThread.h"
#include "net/EventLoop.h"

EventLoopThread::EventLoopThread()
    : loop_(nullptr),
      exiting_(false)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (thread_.joinable())
    {
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_ = std::thread(&EventLoopThread::threadFunc, this);
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });
    return loop_;
}

void EventLoopThread::threadFunc()
{
    // 每个线程创建一个独立的 EventLoop，并在该线程中运行。
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    loop.loop();
    loop_ = nullptr;
}
