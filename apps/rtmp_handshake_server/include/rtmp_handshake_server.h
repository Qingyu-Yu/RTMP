#pragma once

#include "rtmp/handshake/handshake_session.h"

#include "net/TcpConnection.h"
#include "net/TcpServer.h"

#include <memory>
#include <mutex>
#include <unordered_map>

class Buffer;
class EventLoop;
class InetAddress;

namespace rtmp::server {

/**
 * 将 RTMP 握手状态机接入 muduo 的服务器适配类。
 *
 * 本类只负责：
 * 1. 接收 muduo 的连接和消息回调；
 * 2. 为每条连接创建、查询和销毁 HandshakeSession；
 * 3. 从 Buffer 取出已消费字节，并通过 TcpConnection 发送握手响应。
 *
 * 具体协议校验由 HandshakeSession 完成，避免网络层和协议层互相耦合。
 */
class RtmpHandshakeServer {
public:
    /**
     * 构造 RTMP 握手服务器并注册 muduo 回调。
     *
     * @param loop           muduo 主事件循环，生命周期必须长于本对象。
     * @param listen_address 服务器监听地址。
     */
    RtmpHandshakeServer(EventLoop* loop, const InetAddress& listen_address);

    // 设置 muduo I/O 工作线程数量，应在 start() 之前调用。
    void setThreadNum(int thread_count);

    // 启动监听和 muduo 线程池；调用后由 EventLoop 驱动网络事件。
    void start();

private:
    // 会话使用 shared_ptr，使 onMessage 查到会话后可以缩短互斥锁持有时间。
    using SessionPtr = std::shared_ptr<handshake::HandshakeSession>;

    // 处理连接状态变化：连接建立时创建会话，连接断开时删除会话。
    void onConnection(const TcpConnection::Ptr& connection);

    // 处理网络输入：驱动握手状态机、消费 Buffer、发送响应或关闭错误连接。
    void onMessage(const TcpConnection::Ptr& connection, Buffer* buffer);

    // 在连接关闭时从会话表中移除对应状态，防止资源泄漏。
    void onClose(const TcpConnection::Ptr& connection);

    // 实际负责监听、accept、I/O 线程调度和 TCP 收发的 muduo 服务器。
    TcpServer server_;

    // 保护 sessions_。muduo 可能在多个 I/O 线程中并发触发连接回调。
    std::mutex sessions_mutex_;

    // 连接到握手会话的映射；键只用于标识连接，不负责 TcpConnection 生命周期。
    std::unordered_map<const TcpConnection*, SessionPtr> sessions_;
};

}  // namespace rtmp::server
