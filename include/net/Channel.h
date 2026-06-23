#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "base/noncopyable.h"

class EventLoop;

// Channel 是“文件描述符及其事件回调”的包装，不拥有 fd，也不执行 epoll_wait。
//
// events_  表示用户希望监听什么事件，例如 EPOLLIN；
// receivedEvents_ 表示 Epoll 本轮实际检测到什么事件；
// state_          表示该 Channel 当前是否已经注册到 epoll。
//
// Channel 修改 events_ 后通知 EventLoop，EventLoop 再转交 Epoll 调用 epoll_ctl。
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;

    enum class State
    {
        kNew,     // 从未加入，或已经被完全移除。
        kAdded,   // 当前已注册到 epoll。
        kDeleted, // 因无关注事件已 DEL，但仍保留在 Epoll 的索引中。
    };

    // 构造 Channel，绑定所属的 EventLoop 和要监听的文件描述符。
    // Channel 负责将 fd 的可读/可写事件注册到 Epoll，并在事件发生时调用回调。
    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 处理 epoll_wait 返回的事件，并调用对应回调。
    // 根据 receivedEvents_ 触发 read/write/close/error 回调。
    void handleEvent();

    // 设置不同事件的回调函数。
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 将 Channel 与拥有它的 shared_ptr 对象绑定。
    // 处理事件前会尝试提升 weak_ptr；提升失败说明拥有者已销毁，本次事件被忽略。
    void tie(const std::shared_ptr<void>& obj);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void setReceivedEvents(int events) { receivedEvents_ = events; }

    // 开启/关闭可读、可写事件监听。
    // 通过修改 events_ 并调用 update() 通知 Epoll 更新 fd 的注册状态。
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isReading() const { return events_ & kReadEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }

    State state() const { return state_; }
    void setState(State state) { state_ = state; }

    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_; // 所属事件循环。
    int fd_; // 监听的文件描述符。
    int events_; // 关注的事件类型。
    int receivedEvents_; // epoll_wait 返回的实际发生事件。
    State state_; // 当前在 Epoll 中的注册状态。

    std::weak_ptr<void> tie_; // 通常绑定 TcpConnection，防止回调期间对象析构。
    bool tied_; // 是否已经绑定了拥有者。

    EventCallback readCallback_; // 读事件回调。
    EventCallback writeCallback_; // 写事件回调。
    EventCallback closeCallback_; // 关闭事件回调。
    EventCallback errorCallback_; // 错误事件回调。
};
