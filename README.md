# RTMP Server with Muduo

这是一个按模块组织的 C++ 项目，包含：

- 基于 Reactor 模型的 muduo 风格网络库；
- RTMP simple handshake 状态机；
- RTMP 握手服务器的 muduo 适配层；
- 网络库回显测试和 RTMP 握手单元测试。

当前 RTMP 部分只实现握手，不包含 Chunk、AMF、命令消息和音视频转发。

## 目录结构

```text
rtmp/
├── apps/
│   └── rtmp_handshake_server/        # 可部署的 RTMP 服务程序
├── examples/
│   └── echo_server/                  # 网络库使用示例
├── include/                          # 可被其他目标引用的公共头文件
│   ├── base/
│   ├── net/
│   └── rtmp/handshake/
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
- `apps/` 存放最终部署程序及其私有代码；
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

`RtmpHandshakeServer` 负责把该状态机接入 muduo，并为每条 TCP 连接维护独立会话。

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
- `build/bin/echo_server`
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
