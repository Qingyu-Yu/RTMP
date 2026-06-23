#include "net/EventLoop.h"
#include "net/Channel.h"
#include "net/Epoll.h"
#include "net/TimerQueue.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include "base/Logger.h"

__thread EventLoop* t_loopInThisThread = nullptr;

namespace
{

const int kPollTimeMs = 10000;

int createEventfd()
{
    // eventfd 是一个可被 epoll 监听的计数器。其他线程向它写入 8 字节整数，
    // loop 线程就会从 epoll_wait() 中醒来并处理 pendingFunctors_。
    const int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd failed");
    }
    return evtfd;
}

} // namespace

EventLoop::EventLoop()
    : epoll_(std::make_unique<Epoll>()),
      timerQueue_(std::make_unique<TimerQueue>(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_)),
      threadId_(CurrentThread::tid()),
      looping_(false),
      quit_(false),
      callingPendingFunctors_(false)
{
    LOG_DEBUG("EventLoop create %p in thread %d", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("another EventLoop already exists in this thread");
    }
    else
    {
        t_loopInThisThread = this;
    }

    // wakeupFd_ 与普通 socket 一样通过 Channel 接入 Epoll。
    // 当其他线程调用 wakeup() 时，handleRead() 负责清空 eventfd 计数。
    wakeupChannel_->setReadCallback([this] {
        handleRead();
    });
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    assertInLoopThread();
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread=nullptr;
}

void EventLoop::loop()
{
    assertInLoopThread();
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop start");
    while (!quit_)
    {
        activeChannels_.clear();

        // 阶段 1：等待 socket、timerfd 或 eventfd 变为就绪。
        epoll_->wait(kPollTimeMs, activeChannels_);

        // 阶段 2：逐个分发事件。Channel 根据 receivedEvents_ 调用对象回调。
        for (Channel* channel : activeChannels_)
        {
            channel->handleEvent();
        }

        // 阶段 3：执行跨线程任务。放在 IO 回调之后可保证本轮事件先处理完。
        doPendingFunctors();
    }
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread())
    {
        wakeup();
    }
}

TimerId EventLoop::runAt(Timestamp time, Functor cb)
{
    return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, Functor cb)
{
    return runAt(addTime(Timestamp::now(), delay), std::move(cb));
}

TimerId EventLoop::runEvery(double interval, Functor cb)
{
    return timerQueue_->addTimer(
        std::move(cb),
        addTime(Timestamp::now(), interval),
        interval);
}

void EventLoop::cancel(TimerId timerId)
{
    timerQueue_->cancel(timerId);
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        // 如果在本线程，直接执行回调。
        cb();
    }
    else
    {
        // 否则放到 pendingFunctors_，等待 loop 线程执行。
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        // 锁只用于移动任务，不允许持锁执行用户回调。
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        // 跨线程提交时，loop 可能阻塞在 epoll_wait()，必须主动唤醒。
        // 如果正在执行任务队列也要唤醒，确保新加入的任务尽快进入下一轮。
        wakeup();
    }
}

void EventLoop::wakeup()
{
    const uint64_t one = 1;
    const ssize_t n = ::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("wakeup failed");
    }
}
void EventLoop::updateChannel(Channel* channel)
{
    assertInLoopThread();
    epoll_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel* channel)
{
    assertInLoopThread();
    epoll_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel* channel)
{
    assertInLoopThread();
    return epoll_->hasChannel(channel);
}

void EventLoop::assertInLoopThread() const
{
    if (!isInLoopThread())
    {
        LOG_FATAL(
            "EventLoop %p belongs to thread %d, called from thread %d",
            this,
            threadId_,
            CurrentThread::tid());
    }
}

void EventLoop::handleRead()
{
    uint64_t counter = 0;
    const ssize_t n = ::read(wakeupFd_, &counter, sizeof counter);
    if (n != sizeof counter)
    {
        LOG_ERROR("EventLoop::handleRead read %zd bytes", n);
    }
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    // swap 后立刻释放锁：用户回调可能再次 queueInLoop()，也可能执行较长时间，
    // 均不应阻塞其他线程提交任务。
    for (const Functor& functor : functors)
    {
        functor();
    }

    callingPendingFunctors_ = false;
}
