#include "rtmp_handshake_server.h"

#include "net/EventLoop.h"
#include "net/InetAddress.h"

int main() {
    // 主线程事件循环负责监听连接，并把已接受连接分配给 I/O 线程。
    EventLoop loop;

    // RTMP 默认端口为 1935；0.0.0.0 表示监听本机全部 IPv4 网卡。
    InetAddress listen_address(1935, "0.0.0.0");

    // 服务器对象生命周期覆盖整个 EventLoop，确保已注册回调中的 this 有效。
    rtmp::server::RtmpHandshakeServer server(&loop, listen_address);

    // 使用 4 个 I/O 工作线程处理客户端连接。
    server.setThreadNum(4);

    // 启动监听后进入事件循环。loop() 会持续阻塞并分发网络事件。
    server.start();
    loop.loop();
    return 0;
}
