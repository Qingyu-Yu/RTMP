#include "net/EventLoopThreadPool.h"
#include "net/EventLoopThread.h"
#include "net/EventLoop.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const std::string& nameArg)
    : baseLoop_(baseLoop),
      name_(nameArg),
      started_(false),
      numThreads_(0),
      next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
}

void EventLoopThreadPool::setThreadNum(int numThreads)
{
    numThreads_ = numThreads;
}

void EventLoopThreadPool::start()
{
    if (started_)
    {
        return;
    }
    started_ = true;

    for (int i = 0; i < numThreads_; ++i)
    {
        // startLoop() 只有在线程中的 EventLoop 就绪后才返回，因此放入 loops_
        // 的指针可以立即用于分配连接。
        std::unique_ptr<EventLoopThread> thread(new EventLoopThread());
        loops_.push_back(thread->startLoop());
        threads_.push_back(std::move(thread));
    }
    if (loops_.empty())
    {
        // 如果没有 worker 线程，则使用主线程的 baseLoop 来处理所有连接。
        loops_.push_back(baseLoop_);
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    EventLoop* loop = baseLoop_;
    if (!loops_.empty())
    {
        // round-robin 只发生在 baseLoop 线程，不需要为 next_ 加锁。
        loop = loops_[next_];
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}
