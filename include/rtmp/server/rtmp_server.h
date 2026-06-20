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
    RtmpServer(EventLoop* loop, const InetAddress& listen_address);
    ~RtmpServer();

    RtmpServer(const RtmpServer&) = delete;
    RtmpServer& operator=(const RtmpServer&) = delete;
    RtmpServer(RtmpServer&&) = delete;
    RtmpServer& operator=(RtmpServer&&) = delete;

    void setThreadNum(int thread_count);
    void start();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rtmp::server
