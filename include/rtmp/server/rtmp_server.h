#pragma once

#include <memory>

class EventLoop;
class InetAddress;

namespace rtmp::server {

/**
 * 基于项目网络库运行 RTMP 协议栈的服务器。
 *
 * 公共接口只暴露启动和线程配置。连接管理、协议会话和流路由通过 PImpl
 * 隐藏，避免调用方依赖服务器内部使用的容器和网络类型。
 */
class RtmpServer {
public:
    /**
     * 创建服务器适配器。
     *
     * loop 和 listen_address 由调用方创建；loop 的生命周期必须覆盖本对象。
     * 构造阶段只注册回调，直到 start() 才启动线程池和监听。
     */
    RtmpServer(EventLoop* loop, const InetAddress& listen_address);
    ~RtmpServer();

    RtmpServer(const RtmpServer&) = delete;
    RtmpServer& operator=(const RtmpServer&) = delete;
    RtmpServer(RtmpServer&&) = delete;
    RtmpServer& operator=(RtmpServer&&) = delete;

    // 配置 I/O worker 数量，应在 start() 前调用。
    void setThreadNum(int thread_count);

    // 启动 TcpServer；事件分发仍由调用方执行 EventLoop::loop()。
    void start();

private:
    // 将 muduo、连接上下文和流注册表从公共头文件中隐藏。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rtmp::server
