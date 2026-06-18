# RTMP Server with Muduo

这是一个整合后的单目录 C++ 项目，包含：

- 基于 Reactor 模型的 muduo 风格网络库；
- RTMP simple handshake 状态机；
- RTMP 握手服务器的 muduo 适配层；
- 网络库回显测试和 RTMP 握手单元测试。

当前 RTMP 部分只实现握手，不包含 Chunk、AMF、命令消息和音视频转发。

## 目录结构

```text
muduo/
├── include/
│   ├── base/                         # 日志、时间戳等基础组件
│   ├── net/                          # muduo 网络库接口
│   └── rtmp/
│       ├── handshake/
│       │   └── handshake_session.h   # RTMP 握手状态机接口
│       └── server/
│           └── rtmp_handshake_server.h
├── src/
│   ├── *.cpp                         # muduo 网络库实现
│   └── rtmp/
│       ├── handshake_session.cpp     # 握手协议实现
│       ├── rtmp_handshake_server.cpp # muduo 适配层
│       ├── main.cpp                  # RTMP 服务器入口
│       └── CMakeLists.txt
├── test/
│   ├── test.cpp                      # muduo 回显服务器
│   ├── handshake_session_test.cpp    # RTMP 握手单元测试
│   └── CMakeLists.txt
└── CMakeLists.txt                    # 项目唯一顶层构建配置
```

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

`RtmpHandshakeServer` 负责把该状态机接入 muduo，并为每条 TCP 连接维护独立会话。

## 构建

完整网络库使用 Linux 的 `epoll` 和 `eventfd`，RTMP 服务器需要在 Linux 构建：

```bash
cd muduo
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

生成文件：

- `build/lib/libmuduo.a`
- `build/lib/libmuduo.so`
- `build/lib/librtmp_handshake.a`
- `build/bin/muduo_test`
- `build/bin/handshake_session_test`
- `build/bin/rtmp_handshake_server`

启动 RTMP 握手服务器：

```bash
./build/bin/rtmp_handshake_server
```

服务器默认监听：

```text
0.0.0.0:1935
```

在 macOS 等非 Linux 平台，CMake 只构建与平台无关的 RTMP 握手状态机和测试。
