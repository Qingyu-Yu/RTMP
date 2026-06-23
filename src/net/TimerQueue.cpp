#include "net/TimerQueue.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <sys/timerfd.h>
#include <unistd.h>

#include "base/Logger.h"
#include "net/Channel.h"
#include "net/EventLoop.h"

namespace
{

int createTimerFd()
{
    // timerfd 使用单调时钟等待，避免系统时间被校准时影响等待时长。
    // Timestamp 保存的是墙上时钟；reset 时先计算二者之间的相对时长。
    const int timerFd =
        ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerFd < 0)
    {
        LOG_FATAL("TimerQueue::createTimerFd failed: %s", std::strerror(errno));
    }
    return timerFd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
    int64_t microseconds =
        when.microSecondsSinceEpoch() -
        Timestamp::now().microSecondsSinceEpoch();
    // 避免过小或已经过期的时间导致 timerfd 被意外解除。
    microseconds = std::max<int64_t>(microseconds, 100);

    struct timespec value;
    value.tv_sec = static_cast<time_t>(
        microseconds / Timestamp::kMicroSecondsPerSecond);
    value.tv_nsec = static_cast<long>(
        (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
    return value;
}

void readTimerFd(int timerFd, Timestamp now)
{
    uint64_t expirations = 0;
    const ssize_t n = ::read(timerFd, &expirations, sizeof expirations);
    if (n != sizeof expirations)
    {
        LOG_ERROR("TimerQueue::readTimerFd read %zd bytes at %s",
                  n,
                  now.toString().c_str());
    }
}

void resetTimerFd(int timerFd, Timestamp expiration)
{
    // TimerQueue 只让 timerfd 关注 timers_ 中最早的一个时间点。
    // 该时间到达后，再一次性取出所有已经过期的 Timer。
    struct itimerspec newValue = {};
    newValue.it_value = howMuchTimeFromNow(expiration);
    if (::timerfd_settime(timerFd, 0, &newValue, nullptr) < 0)
    {
        LOG_ERROR("TimerQueue::resetTimerFd failed: %s", std::strerror(errno));
    }
}

} // namespace

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerFd_(createTimerFd()),
      timerChannel_(std::make_unique<Channel>(loop, timerFd_))
{
    timerChannel_->setReadCallback([this] {
        handleRead();
    });
    timerChannel_->enableReading();
}

TimerQueue::~TimerQueue()
{
    loop_->assertInLoopThread();
    timerChannel_->disableAll();
    timerChannel_->remove();
    ::close(timerFd_);

    for (const Entry& timer : timers_)
    {
        delete timer.second;
    }
}

TimerId TimerQueue::addTimer(
    TimerCallback callback,
    Timestamp when,
    double interval)
{
    // Timer 的生命周期由 TimerQueue 接管。跨线程调用时，裸指针被捕获进
    // EventLoop 的任务队列，直到 addTimerInLoop() 插入集合。
    Timer* timer = new Timer(std::move(callback), when, interval);
    loop_->runInLoop([this, timer] {
        addTimerInLoop(timer);
    });
    return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop([this, timerId] {
        cancelInLoop(timerId);
    });
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    loop_->assertInLoopThread();
    const bool earliestChanged = insert(timer);
    if (earliestChanged)
    {
        resetTimerFd(timerFd_, timer->expiration());
    }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    loop_->assertInLoopThread();
    const ActiveTimer timer(timerId.timer_, timerId.sequence_);
    const auto active = activeTimers_.find(timer);
    if (active != activeTimers_.end())
    {
        // 正常情况：Timer 还在两个集合中，从排序集合和身份集合同时删除。
        const std::size_t erased =
            timers_.erase(Entry(active->first->expiration(), active->first));
        if (erased != 1)
        {
            LOG_ERROR("TimerQueue::cancelInLoop inconsistent timer state");
        }
        delete active->first;
        activeTimers_.erase(active);
    }
    else if (callingExpiredTimers_)
    {
        // 特殊情况：Timer 的回调正在执行。它已被 getExpired() 从集合取出，
        // 此时不能 delete，否则回调返回后的 reset() 会访问悬空指针。
        // 这里只记录取消请求，由 reset() 决定不再插入并安全删除。
        cancelingTimers_.insert(timer);
    }
}

void TimerQueue::handleRead()
{
    loop_->assertInLoopThread();
    const Timestamp now(Timestamp::now());
    // 必须读取 timerfd，否则其“可读”状态不会被清除，epoll 会持续立即返回。
    readTimerFd(timerFd_, now);

    // 先把到期 Timer 从主集合移出，再执行用户回调。这样回调可以安全地
    // 添加或取消其他定时器，不会破坏当前遍历。
    const std::vector<Entry> expired = getExpired(now);
    callingExpiredTimers_ = true;
    cancelingTimers_.clear();

    for (const Entry& timer : expired)
    {
        timer.second->run();
    }

    callingExpiredTimers_ = false;
    reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    std::vector<Entry> expired;
    // set 按 pair 排序。使用最大指针作为第二排序键，可包含 expiration == now
    // 的全部 Timer，范围 [begin, end) 即所有已到期任务。
    const Entry sentinel(
        now,
        reinterpret_cast<Timer*>(std::numeric_limits<uintptr_t>::max()));
    const auto end = timers_.lower_bound(sentinel);
    std::copy(timers_.begin(), end, std::back_inserter(expired));
    timers_.erase(timers_.begin(), end);

    for (const Entry& timer : expired)
    {
        activeTimers_.erase(ActiveTimer(timer.second, timer.second->sequence()));
    }
    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    // 用户回调全部执行结束后，重复 Timer 计算下一次时间并重新入队；
    // 单次 Timer 或回调中已取消的 Timer 在这里释放。
    for (const Entry& timer : expired)
    {
        const ActiveTimer activeTimer(timer.second, timer.second->sequence());
        if (timer.second->repeat() &&
            cancelingTimers_.find(activeTimer) == cancelingTimers_.end())
        {
            timer.second->restart(now);
            insert(timer.second);
        }
        else
        {
            delete timer.second;
        }
    }

    if (!timers_.empty())
    {
        resetTimerFd(timerFd_, timers_.begin()->second->expiration());
    }
}

bool TimerQueue::insert(Timer* timer)
{
    // 只有新 Timer 成为队首时才需要重设 timerfd；否则原有最近到期时间不变。
    const bool earliestChanged =
        timers_.empty() || timer->expiration() < timers_.begin()->first;

    const auto timerInserted =
        timers_.insert(Entry(timer->expiration(), timer));
    const auto activeInserted =
        activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    if (!timerInserted.second || !activeInserted.second)
    {
        LOG_FATAL("TimerQueue::insert duplicate timer");
    }
    return earliestChanged;
}
