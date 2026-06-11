cmake_minimum_required(VERSION 3.20)

project(muduo VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ================================
# 输出目录
# ================================

# 静态库 (.a)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_SOURCE_DIR}/build/lib)

# 动态库 (.so)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_SOURCE_DIR}/build/lib)

# 可执行文件
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_SOURCE_DIR}/build/bin)


# ================================
# 源文件
# ================================

set(MUDUO_SRCS
    src/Timestamp.cpp
    src/Logger.cpp
    src/InetAddress.cpp
    src/TcpServer.cpp
    src/TcpConnection.cpp
    src/Channel.cpp
    src/EventLoop.cpp
    src/Poller.cpp
    src/DefaultPoller.cpp
    src/EpollPoller.cpp
    src/Acceptor.cpp
    src/EventLoopThread.cpp
    src/EventLoopThreadPool.cpp
    src/Buffer.cpp
    src/CurrentThread.cpp
)


# ================================
# 静态库
# ================================

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


# ================================
# 动态库
# ================================

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


# ================================
# 测试程序
# ================================

add_executable(muduo_test
    test/test.cpp
)

target_link_libraries(muduo_test
    PRIVATE
        muduo_static
)