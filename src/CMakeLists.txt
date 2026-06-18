# muduo 网络库目标。

set(MUDUO_SRCS
    Timestamp.cpp
    Logger.cpp
    InetAddress.cpp
    TcpServer.cpp
    TcpConnection.cpp
    Channel.cpp
    EventLoop.cpp
    Poller.cpp
    DefaultPoller.cpp
    EpollPoller.cpp
    Acceptor.cpp
    EventLoopThread.cpp
    EventLoopThreadPool.cpp
    Buffer.cpp
    CurrentThread.cpp
)

add_library(muduo_static STATIC
    ${MUDUO_SRCS}
)

target_include_directories(muduo_static
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(muduo_static PROPERTIES
    OUTPUT_NAME muduo
)

add_library(muduo_shared SHARED
    ${MUDUO_SRCS}
)

target_include_directories(muduo_shared
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(muduo_shared PROPERTIES
    OUTPUT_NAME muduo
)
