# 与平台无关的 RTMP 握手状态机测试。
add_executable(handshake_session_test
    handshake_session_test.cpp
)

target_link_libraries(handshake_session_test
    PRIVATE
        rtmp_handshake
)

add_test(
    NAME handshake_session_test
    COMMAND handshake_session_test
)

# 回显服务器依赖 muduo 的 epoll 实现，只在 Linux 上构建。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(muduo_test
        test.cpp
    )

    target_link_libraries(muduo_test
        PRIVATE
            muduo_static
            pthread
    )
endif()
