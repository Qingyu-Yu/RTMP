#pragma once

#include <vector>
#include <memory>
#include <string>

#include "base/noncopyable.h"

class EventLoop;
class EventLoopThread;

// EventLoopThreadPool 管理一组“每线程一个 EventLoop”的 IO 线程。
// getNextLoop() 使用 round-robin，把新连接均匀分配给各个 worker loop。
class EventLoopThreadPool : noncopyable
{
public:
    EventLoopThreadPool(EventLoop* baseLoop, const std::string& nameArg);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads);
    void start();
    EventLoop* getNextLoop();
    bool started() const { return started_; }
    const std::string& name() const { return name_; }

private:
    EventLoop* baseLoop_; // Acceptor 所在的主循环，不由线程池拥有。
    std::string name_;
    bool started_;
    int numThreads_;
    int next_; // 下一次分配使用的 loops_ 下标。
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};
