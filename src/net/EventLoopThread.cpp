#include "net/EventLoopThread.h"
#include "net/EventLoop.h"

EventLoopThread::EventLoopThread()
    : loop_(nullptr)
{
}

EventLoopThread::~EventLoopThread()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_)
        {
            // loop() 可能阻塞在 epoll_wait，quit() 会通过 eventfd 唤醒它。
            loop_->quit();
        }
    }

    if (thread_.joinable())
    {
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_ = std::thread(&EventLoopThread::threadFunc, this);

    // loop_ 指向子线程栈上的对象，必须等子线程创建完 EventLoop 才能返回。
    // 条件变量的谓词同时处理虚假唤醒。
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });
    return loop_;
}

void EventLoopThread::threadFunc()
{
    // EventLoop 在子线程栈上创建，因此它记录的 threadId_ 正是当前子线程。
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    // 从这里开始阻塞处理 IO，直到其他线程调用 loop.quit()。
    loop.loop();
    {
        // loop() 返回后清空跨线程可见指针；随后栈对象 loop 析构。
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
}
