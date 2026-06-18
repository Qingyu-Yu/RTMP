cmake_minimum_required(VERSION 3.20)

project(rtmp_muduo_server VERSION 0.1 LANGUAGES CXX)

# RTMP 握手模块使用 std::byte 等 C++17 类型，因此整个项目统一使用 C++17。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# muduo 当前基于 epoll/eventfd，只能在 Linux 上构建。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(src)
endif()

# RTMP 握手协议层不依赖操作系统；完整服务器目标仅在 Linux 上生成。
add_subdirectory(src/rtmp)

enable_testing()
add_subdirectory(test)
