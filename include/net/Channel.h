#pragma once

#include <functional>
#include <memory>

#include "base/noncopyable.h"
#include "base/Timestamp.h"

class EventLoop;

class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    // 构造 Channel，绑定所属的 EventLoop 和要监听的文件描述符。
    // Channel 负责将 fd 的可读/可写事件注册到 Poller，并在事件发生时调用回调。
    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 处理 poller 返回的事件，并调用对应回调。
    // 这时会根据 revents_ 判断事件类型，并触发 read/write/close/error 回调。
    void handleEvent(Timestamp receiveTime);

    // 设置不同事件的回调函数。
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 绑定拥有者对象，防止对象销毁后触发事件。
    void tie(const std::shared_ptr<void>& obj);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    // 开启/关闭可读、可写事件监听。
    // 通过修改 events_ 并调用 update() 通知 Poller 更新 fd 的注册状态。
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isReading() const { return events_ & kReadEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_; // 所属事件循环。
    int fd_; // 监听的文件描述符。
    int events_; // 关注的事件类型。
    int revents_; // poller 返回的实际发生事件。
    int index_; // 在 Poller 中的状态标记。

    std::weak_ptr<void> tie_; // 与拥有者的弱引用，防止对象被销毁后继续处理事件。
    bool tied_; // 是否已经绑定了拥有者。

    ReadEventCallback readCallback_; // 读事件回调。
    EventCallback writeCallback_; // 写事件回调。
    EventCallback closeCallback_; // 关闭事件回调。
    EventCallback errorCallback_; // 错误事件回调。
};
