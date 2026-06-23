# RTMP Server with Muduo

这是一个按模块组织的 C++17 RTMP 服务器，包含：

- 基于 Reactor 模型的 muduo 风格网络库；
- 支持微秒时间戳、单次/重复定时任务及跨线程取消；
- RTMP simple handshake 状态机；
- RTMP Chunk 编解码和 AMF0 编解码；
- `connect`、`createStream`、`publish`、`play` 等命令处理；
- 发布者/订阅者流路由、音视频转发和初始化消息缓存；
- RTMP 网络适配层及协议单元测试。

## 目录结构

```text
rtmp/
├── apps/
│   └── rtmp_server/                  # 只负责配置和启动的可执行程序
├── examples/
│   └── echo_server/                  # 网络库使用示例
├── include/                          # 可被其他目标引用的公共头文件
│   ├── base/
│   ├── net/
│   └── rtmp/                         # 握手、Chunk、AMF、会话和流管理接口
├── scripts/
│   └── build.sh                      # 本地构建与测试入口
├── src/                              # 库实现
│   ├── base/
│   ├── net/
│   └── rtmp/
├── tests/
│   └── rtmp/                         # 按模块组织的自动化测试
└── CMakeLists.txt                    # 顶层构建编排
```

目录职责：

- `include/` 和 `src/` 只存放可复用库的接口与实现；
- `apps/` 存放最终部署程序入口，不承载可复用协议或网络适配逻辑；
- `examples/` 不参与生产程序实现；
- `tests/` 与业务模块对应，便于后续扩展单元测试和集成测试；
- 每个独立模块维护自己的 `CMakeLists.txt`，顶层文件只负责项目级选项和子目录编排。

## RTMP 握手流程

```text
client                         server
  | -------- C0 + C1 --------> |
  | <----- S0 + S1 + S2 ------ |
  | ----------- C2 -----------> |
  |       handshake done        |
```

`HandshakeSession` 是不依赖网络库的纯协议状态机，负责：

- 校验 C0 版本和 C1 保留字段；
- 生成 S0、S1、S2；
- 校验客户端 C2；
- 处理 TCP 半包和粘包；
- 握手完成后保留同一 Buffer 中的后续 RTMP 数据。

握手完成后，连接数据依次进入以下模块：

```text
TcpServer
  -> rtmp::server（muduo 网络适配库）
      -> RtmpSession（命令与会话状态）
          -> ChunkDecoder / ChunkEncoder
          -> AMF0
      -> StreamRegistry（发布、订阅与转发关系）
```

模块职责：

- `chunk` 只处理 RTMP Chunk 分片与消息重组；
- `amf` 只处理 AMF0 值的序列化和反序列化；
- `session` 处理 RTMP 命令并输出网络响应和流事件；
- `stream` 管理发布者、订阅者和 AAC/AVC/元数据初始化消息；
- `server` 将上述模块接入 TCP 回调，不承担协议解析职责；
- `apps/rtmp_server/main.cpp` 只创建事件循环、监听地址和服务器实例。

构建目标的依赖方向：

```text
rtmp_server executable
  -> rtmp::server
      -> muduo::static
      -> rtmp::handshake
      -> rtmp::core
```

`rtmp::core` 和 `rtmp::handshake` 不依赖 muduo，可以在非 Linux 平台独立
构建和测试。`rtmp::server` 是明确的 Linux/muduo 适配层，其公共接口通过
PImpl 隐藏连接表、锁和协议会话等实现细节。

## 定时任务

`EventLoop` 提供与 muduo 一致的常用定时接口：

```cpp
TimerId once = loop.runAfter(1.0, [] {
    // 1 秒后执行一次
});

TimerId heartbeat = loop.runEvery(5.0, [] {
    // 每 5 秒执行一次
});

loop.cancel(heartbeat);
```

`runAt` 接受绝对 `Timestamp`，`runAfter` 和 `runEvery` 的时间单位为秒。
定时器可从其他线程添加或取消，实际队列操作统一在所属 `EventLoop` 线程执行。

## 阅读网络库

如果不熟悉 Reactor、epoll 或 muduo 的线程模型，建议先阅读
[`docs/muduo_reading_guide.md`](docs/muduo_reading_guide.md)。文档按连接建立、
收发数据、跨线程任务、定时器和对象生命周期拆解了完整调用流程。

## 构建

完整网络库使用 Linux 的 `epoll` 和 `eventfd`，RTMP 服务器需要在 Linux 构建：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

也可以使用统一脚本（可从任意目录执行）：

```bash
./scripts/build.sh
```

生成文件：

- `build/lib/libmuduo.a`
- `build/lib/libmuduo.so`
- `build/lib/librtmp_handshake.a`
- `build/lib/librtmp_core.a`
- `build/lib/librtmp_muduo_server.a`
- `build/bin/echo_server`
- `build/bin/handshake_session_test`
- `build/bin/rtmp_protocol_test`
- `build/bin/rtmp_server`

启动 RTMP 服务器：

```bash
./build/bin/rtmp_server
```

服务器默认监听：

```text
0.0.0.0:1935
```

FFmpeg 推流和拉流示例：

```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://127.0.0.1/live/test
ffplay rtmp://127.0.0.1/live/test
```

在 macOS 等非 Linux 平台，CMake 只构建平台无关的 RTMP 协议模块和测试。
