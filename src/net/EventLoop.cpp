#include "net/EventLoop.h"
#include "net/Channel.h"
#include "net/Poller.h"

__thread EventLoop* t_loopInThisThread = nullptr;

const int kPollTimeMs=10000;


int createEventfd()
{
    // eventfd 用于线程间唤醒。写入 eventfd 后，poller 会检测到可读事件。
    int evtfd=::eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    if(evtfd<0)
    {
        LOG_FATAL("eventfd failed");

    }
    return evtfd;
}

EventLoop::EventLoop()
    :looping_(false),
    quit_(false),
    callingPendingFunctors_(false),
    ThreadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this,wakeupFd_)),
    currentActiveChannel_(nullptr)
{
    LOG_DEBUG("Eventloop create %p in thread %d\n",this,ThreadId_);
    if(t_loopInThisThread)
    {
        LOG_FATAL("t_loopInThisThread");
    }
    else
    {
        t_loopInThisThread=this;
    }
    //设置wakeup的事件类型已经发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this, std::placeholders::_1));
    //每一eventloop都将监听wakeup channel的EPOLLIN可读事件
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread=nullptr;
}

void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop start");
    while (!quit_)
    {
        activeChannels_.clear();
        // 等待 IO 事件，如果没有事件则在超时时间后返回。
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (auto channel : activeChannels_)
        {
            // Poller 报告哪个 Channel 有事件，EventLoop 负责分发给 Channel 处理。
            channel->handleEvent(pollReturnTime_);
        }
        // 处理队列中的跨线程回调。
        doPendingFunctors();
    }
}

void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread())
    {
        wakeup();
    }
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
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        // 如果当前不在 loop 线程，或者正在执行回调，必须唤醒 loop 线程。
        wakeup();
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("wakeup failed");
    }
}
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}
void EventLoop::handleRead(Timestamp)
{
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_DEBUG("handleRead failed");
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

    // 在 loop 线程中执行 pendingFunctors_ 中的回调，避免持锁执行用户代码。
    for (const Functor& functor : functors)
    {
        functor();
    }

    callingPendingFunctors_ = false;
}
